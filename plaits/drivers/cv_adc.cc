// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Drivers for the 16-bit SDADC scanning the CVs.

#include "plaits/drivers/cv_adc.h"

#include <stm32f37x_conf.h>

#include "plaits/build_config.h"

namespace plaits {

struct ChannelConfiguration {
  CvAdcChannel map_to;
  uint32_t channel;
  GPIO_TypeDef* gpio;
  uint16_t pin;
};

struct ConverterConfiguration {
  SDADC_TypeDef* sdadc;
  DMA_Channel_TypeDef* dma_channel;
  int num_channels;
  ChannelConfiguration channel[3];
};

const ConverterConfiguration converter_configuration[3] = {
  {
    SDADC1, DMA2_Channel3, 3, {
      { CV_ADC_CHANNEL_TIMBRE, SDADC_Channel_4, GPIOB, GPIO_Pin_2 },
      { CV_ADC_CHANNEL_MODEL, SDADC_Channel_5, GPIOB, GPIO_Pin_1 },
      { CV_ADC_CHANNEL_TRIGGER, SDADC_Channel_6, GPIOB, GPIO_Pin_0 }
    }
  },
  {
    SDADC2, DMA2_Channel4, 2, {
      { CV_ADC_CHANNEL_FM, SDADC_Channel_7, GPIOE, GPIO_Pin_9 },
      { CV_ADC_CHANNEL_LEVEL, SDADC_Channel_8, GPIOE, GPIO_Pin_8 }
    }
  },
  {
    SDADC3, DMA2_Channel5, 3, {
      { CV_ADC_CHANNEL_HARMONICS, SDADC_Channel_6, GPIOD, GPIO_Pin_8 },
      { CV_ADC_CHANNEL_MORPH, SDADC_Channel_7, GPIOB, GPIO_Pin_15 },
      { CV_ADC_CHANNEL_V_OCT, SDADC_Channel_8, GPIOB, GPIO_Pin_14 }
    }
  },
};

void CvAdc::Init() {
  for (int i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
    channel_map_[i] = 0;
    values_[i] = 0;
  }
#if PLAITS_BUILD_FAST_FM
  for (size_t i = 0; i < kAudioRateFmBufferSize; ++i) {
    audio_rate_fm_[i] = 0;
  }
  audio_rate_fm_resampler_.Init();
  audio_rate_fm_overruns_ = 0;
  audio_rate_fm_acknowledged_resyncs_ = 0;
  audio_rate_fm_acknowledged_underflows_ = 0;
  audio_rate_fm_acknowledged_excess_lag_ = 0;
#endif

  // Power all the SDADCs.
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
  PWR_SDADCAnalogCmd(PWR_SDADCAnalog_1, ENABLE);
  PWR_SDADCAnalogCmd(PWR_SDADCAnalog_2, ENABLE);
  PWR_SDADCAnalogCmd(PWR_SDADCAnalog_3, ENABLE);

  // Enable SDADC clock.
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SDADC1, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SDADC2, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SDADC3, ENABLE);
  RCC_SDADCCLKConfig(RCC_SDADCCLK_SYSCLK_Div12);
  
  // Enable DMA2 clock.
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
  
  // Enable GPIO clock.
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOD, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOE, ENABLE);
  
  // Init SDADC
  SDADC_VREFSelect(SDADC_VREF_Ext);
  
  DMA_InitTypeDef dma_init;
  GPIO_InitTypeDef gpio_init;
  SDADC_AINStructTypeDef sdadc_ain;
  
  // Fill structures with the settings common to all channels/pins.
  gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
  gpio_init.GPIO_OType = GPIO_OType_PP;
  gpio_init.GPIO_Mode = GPIO_Mode_AN;

  sdadc_ain.SDADC_InputMode = SDADC_InputMode_SEZeroReference;
  sdadc_ain.SDADC_Gain = SDADC_Gain_1;
  sdadc_ain.SDADC_CommonMode = SDADC_CommonMode_VDDA_2;
  sdadc_ain.SDADC_Offset = 0;
  
  int current_channel = 0;
  
  // Configure all SDADCs, all their input channels, and all the DMA channels.
  for (int i = 0; i < 3; ++i) {
    const ConverterConfiguration& config = converter_configuration[i];
    const bool audio_rate_fm = PLAITS_BUILD_FAST_FM && i == 1;

    // With one continuous channel, FAST mode performs the first conversion in
    // 360 SDADC cycles and every subsequent conversion in 120 cycles: exactly
    // 50 kHz from Plaits' 6 MHz SDADC clock. FAST must be selected while the
    // peripheral is disabled (or in initialization mode).
#if PLAITS_BUILD_FAST_FM
    if (audio_rate_fm) {
      SDADC_FastConversionCmd(config.sdadc, ENABLE);
    }
#endif

    // Wait for SDADC to stabilize.
    SDADC_Cmd(config.sdadc, ENABLE);
    while (SDADC_GetFlagStatus(config.sdadc, SDADC_FLAG_STABIP) == SET);
  
    // Configure GPIO pins.
    for (int j = 0; j < config.num_channels; ++j) {
      gpio_init.GPIO_Pin = config.channel[j].pin;
      GPIO_Init(config.channel[j].gpio, &gpio_init);
    }
    
    // SDADC enters initialization mode.
    SDADC_InitModeCmd(config.sdadc, ENABLE);
    while (SDADC_GetFlagStatus(config.sdadc, SDADC_FLAG_INITRDY) == RESET);
    
    // Configure DMA to read injected values into a slice of the
    // values_ array.
    dma_init.DMA_PeripheralBaseAddr = audio_rate_fm
        ? (uint32_t)&(config.sdadc->RDATAR)
        : (uint32_t)&(config.sdadc->JDATAR);
    dma_init.DMA_MemoryBaseAddr = audio_rate_fm
        ? (uint32_t)(&audio_rate_fm_[0])
        : (uint32_t)(&values_[current_channel]);
    dma_init.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma_init.DMA_BufferSize = audio_rate_fm
        ? kAudioRateFmBufferSize
        : config.num_channels;
    dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma_init.DMA_Mode = DMA_Mode_Circular;
    dma_init.DMA_Priority = DMA_Priority_High;
    dma_init.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(config.dma_channel, &dma_init);

    // Create a configuration and assign it to all channels used by this SDADC.
    SDADC_AINInit(config.sdadc, SDADC_Conf_0, &sdadc_ain);
    uint32_t channels = 0;
    for (int j = 0; j < config.num_channels; ++j) {
      channel_map_[config.channel[j].map_to] = current_channel++;
      SDADC_ChannelConfig(
          config.sdadc, config.channel[j].channel, SDADC_Conf_0);
      // Fast FM uses the regular single-channel group. Keep LEVEL configured in
      // the injected group but never start it: the hardware cannot interleave a
      // second channel without interrupting the continuous FM stream.
      if (!audio_rate_fm ||
          config.channel[j].map_to == CV_ADC_CHANNEL_LEVEL) {
        channels |= config.channel[j].channel;
      }
    }
    
    // Select injected channels.
    SDADC_InjectedChannelSelect(config.sdadc, channels);

#if PLAITS_BUILD_FAST_FM
    if (audio_rate_fm) {
      SDADC_ChannelSelect(config.sdadc, SDADC_Channel_7);
      SDADC_ContinuousModeCmd(config.sdadc, ENABLE);
    }
#endif
    
    // Control-rate converters are restarted once per UI poll. SDADC2 instead
    // free-runs its sole FM channel; timer-triggered one-shot conversions would
    // take 360 cycles each and reach only 16.7 kHz.
    SDADC_InjectedContinuousModeCmd(config.sdadc, DISABLE);
    
    // Terminate initialization sequence.
    SDADC_CalibrationSequenceConfig(config.sdadc, SDADC_CalibrationSequence_3);
    SDADC_InitModeCmd(config.sdadc, DISABLE);
    while (SDADC_GetFlagStatus(config.sdadc, SDADC_FLAG_INITRDY) == SET);
    
    // Run calibration sequence.
    SDADC_StartCalibration(config.sdadc);
    while (SDADC_GetFlagStatus(config.sdadc, SDADC_FLAG_EOCAL) == RESET);

    // Enable DMA.
    DMA_Cmd(config.dma_channel, ENABLE);
    SDADC_DMAConfig(
        config.sdadc,
        audio_rate_fm
            ? SDADC_DMATransfer_Regular
            : SDADC_DMATransfer_Injected,
        ENABLE);
  }

#if PLAITS_BUILD_FAST_FM
  // One software start launches the perpetual 50 kHz regular conversion
  // stream. Convert() must never restart SDADC2 in this build mode.
  SDADC_ClearFlag(SDADC2, SDADC_FLAG_ROVR | SDADC_FLAG_JOVR);
  SDADC_SoftwareStartConv(SDADC2);
#endif
  Convert();
}

void CvAdc::DeInit() {
  for (int i = 0; i < 3; ++i) {
    const ConverterConfiguration& config = converter_configuration[i];
    SDADC_Cmd(config.sdadc, DISABLE);
    SDADC_DMAConfig(config.sdadc, SDADC_DMATransfer_Regular, DISABLE);
    SDADC_DMAConfig(config.sdadc, SDADC_DMATransfer_Injected, DISABLE);
    DMA_Cmd(config.dma_channel, DISABLE);
  }
}

void CvAdc::Convert() {
  SDADC_SoftwareStartInjectedConv(SDADC1);
#if PLAITS_BUILD_FAST_FM
  // Cache a scalar reading for normalization detection and for engines that
  // deliberately fall back to control-rate FM. The continuous DMA remains the
  // source for fast-capable engines; LEVEL is unavailable in this build mode.
  const size_t next = (
      kAudioRateFmBufferSize - DMA_GetCurrDataCounter(DMA2_Channel4)) %
      kAudioRateFmBufferSize;
  values_[channel_map_[CV_ADC_CHANNEL_FM]] =
      audio_rate_fm_[(next + kAudioRateFmBufferSize - 1) %
                     kAudioRateFmBufferSize];
  values_[channel_map_[CV_ADC_CHANNEL_LEVEL]] = 0;
#else
  SDADC_SoftwareStartInjectedConv(SDADC2);
#endif
  SDADC_SoftwareStartInjectedConv(SDADC3);
}

void CvAdc::CopyAudioRateFm(float* destination, size_t size) {
#if PLAITS_BUILD_FAST_FM
  if (SDADC_GetFlagStatus(SDADC2, SDADC_FLAG_ROVR) == SET ||
      SDADC_GetFlagStatus(SDADC2, SDADC_FLAG_JOVR) == SET) {
    ++audio_rate_fm_overruns_;
    SDADC_ClearFlag(SDADC2, SDADC_FLAG_ROVR | SDADC_FLAG_JOVR);
  }
  // CNDTR points to the next DMA write. DMA stores the sample before it
  // decrements CNDTR; the barrier keeps the following ring reads ordered after
  // this producer snapshot.
  const size_t next = (
      kAudioRateFmBufferSize - DMA_GetCurrDataCounter(DMA2_Channel4)) %
      kAudioRateFmBufferSize;
  __DMB();
  audio_rate_fm_resampler_.Process(
      audio_rate_fm_, next, destination, size);
#else
  const float value =
      static_cast<float>(values_[channel_map_[CV_ADC_CHANNEL_FM]]) /
      32768.0f;
  for (size_t i = 0; i < size; ++i) {
    destination[i] = value;
  }
#endif
}

void CvAdc::RealignAudioRateFmAfterKnownPause(size_t block_size) {
#if PLAITS_BUILD_FAST_FM
  (void)block_size;
  // The caller invokes this immediately after a known blocking operation. A
  // JOVR flag present now was caused by that deliberate pause and must not
  // become a fault.
  SDADC_ClearFlag(SDADC2, SDADC_FLAG_ROVR | SDADC_FLAG_JOVR);
  const size_t next = (
      kAudioRateFmBufferSize - DMA_GetCurrDataCounter(DMA2_Channel4)) %
      kAudioRateFmBufferSize;
  __DMB();
  audio_rate_fm_resampler_.Restart(next);
  // Any re-seed observed before this known pause completed belongs to the
  // transition. Preserve the raw lifetime count for debugging, but establish
  // a new diagnostic baseline. Restart() waits for a fresh ring lead, so
  // immediately pending callbacks simply receive the newest scalar sample.
  audio_rate_fm_acknowledged_resyncs_ =
      audio_rate_fm_resampler_.resync_count();
  audio_rate_fm_acknowledged_underflows_ =
      audio_rate_fm_resampler_.underflow_count();
  audio_rate_fm_acknowledged_excess_lag_ =
      audio_rate_fm_resampler_.excess_lag_count();
#else
  (void)block_size;
#endif
}

}  // namespace plaits
