// Copyright 2026 Rubato Audio.

#include "plaits/drivers/sync_input.h"

#include "plaits/build_config.h"

#if PLAITS_BUILD_ENABLE_SYNC_INPUT

#include <stm32f37x_conf.h>

#include "plaits/drivers/audio_rate_timer.h"

namespace plaits {

// The Plaits MODEL conditioner is inverting:
//   ADC voltage = 1.65 V - 0.33 * input voltage.
// With a 3.3 V ADC reference these are +1.0 V rising and +0.5 V release
// thresholds at the jack. The hysteresis rejects noise around an edge while
// accepting ordinary gates, triggers, and bipolar oscillator outputs.
static const uint16_t kRisingThreshold = 1638;
static const uint16_t kReleaseThreshold = 1843;
static const uint16_t kAdcMaximum = 4095;

SyncInput* SyncInput::instance_;

uint32_t SyncInput::CycleCount() {
  return DWT->CYCCNT;
}

uint32_t SyncInput::ReadEvents(size_t block_size) {
  return enabled_ ? queue_.ReadBlock(CycleCount(), block_size) : 0;
}

void SyncInput::Init() {
  instance_ = this;
  enabled_ = false;
  armed_for_rising_edge_ = true;

  // Do not reset CYCCNT: the optional CPU probe shares it. Merely make sure it
  // is available when sync is used without a probe build.
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  queue_.Init(CycleCount());

  // PB1 / ADC channel 9 is already in analog mode for SDADC1's MODEL scan.
  // ADC1 reads the same pin through its injected group. Injected conversions
  // preempt the regular seven-pot DMA sequence, so sync builds shorten the pot
  // acquisition time enough for both groups to finish inside one sample (see
  // pots_adc.cc).
  ADC_InjectedSequencerLengthConfig(ADC1, 1);
  ADC_InjectedChannelConfig(
      ADC1, ADC_Channel_9, 1, ADC_SampleTime_7Cycles5);
  ADC_ExternalTrigInjectedConvConfig(
      ADC1, ADC_ExternalTrigInjecConv_T2_TRGO);
  ADC_AnalogWatchdogSingleChannelConfig(ADC1, ADC_Channel_9);
  ADC_AnalogWatchdogCmd(ADC1, ADC_AnalogWatchdog_None);
  ADC_ExternalTrigInjectedConvCmd(ADC1, DISABLE);
  ADC_ITConfig(ADC1, ADC_IT_AWD, DISABLE);
  ADC_ClearITPendingBit(ADC1, ADC_IT_AWD);

  // The edge timestamp must be captured even if the audio renderer is busy.
  // Its ISR only queues a timestamp and changes two thresholds, so it safely
  // preempts the block callback. This priority assignment is made here rather
  // than globally: stock/default builds that never select sync are unchanged.
  NVIC_SetPriority(ADC1_IRQn, 0);
  NVIC_DisableIRQ(ADC1_IRQn);
}

void SyncInput::ArmRisingEdge() {
  // The conditioner inverts the signal, so the rising jack edge crosses the
  // watchdog's lower threshold.
  ADC_AnalogWatchdogThresholdsConfig(
      ADC1, kAdcMaximum, kRisingThreshold);
  armed_for_rising_edge_ = true;
}

void SyncInput::ArmRelease() {
  // Rearm only after the jack falls below +0.5 V, which makes the ADC reading
  // rise above this upper threshold.
  ADC_AnalogWatchdogThresholdsConfig(
      ADC1, kReleaseThreshold, 0);
  armed_for_rising_edge_ = false;
}

void SyncInput::SetEnabled(bool enabled) {
  if (enabled == enabled_) {
    return;
  }

  if (enabled) {
    queue_.Init(CycleCount());
    ArmRisingEdge();
    // Let the short watchdog ISR preempt the audio callback; otherwise its DWT
    // timestamp would describe callback completion rather than the actual edge.
    NVIC_SetPriority(DMA1_Channel5_IRQn, 1);
    enabled_ = true;
    ADC_ClearITPendingBit(ADC1, ADC_IT_AWD);
    NVIC_ClearPendingIRQ(ADC1_IRQn);
    NVIC_EnableIRQ(ADC1_IRQn);
    ADC_AnalogWatchdogCmd(ADC1, ADC_AnalogWatchdog_SingleInjecEnable);
    ADC_ITConfig(ADC1, ADC_IT_AWD, ENABLE);
    ADC_ExternalTrigInjectedConvCmd(ADC1, ENABLE);
    AudioRateTimer::SetClientActive(
        AudioRateTimer::CLIENT_SYNC_INPUT, true);
  } else {
    ADC_ExternalTrigInjectedConvCmd(ADC1, DISABLE);
    ADC_ITConfig(ADC1, ADC_IT_AWD, DISABLE);
    ADC_AnalogWatchdogCmd(ADC1, ADC_AnalogWatchdog_None);
    AudioRateTimer::SetClientActive(
        AudioRateTimer::CLIENT_SYNC_INPUT, false);
    NVIC_DisableIRQ(ADC1_IRQn);
    ADC_ClearITPendingBit(ADC1, ADC_IT_AWD);
    NVIC_ClearPendingIRQ(ADC1_IRQn);
    enabled_ = false;
    // Once the edge detector is off there is no priority-0 interrupt that the
    // renderer must yield to, so restore the stock audio interrupt priority.
    NVIC_SetPriority(DMA1_Channel5_IRQn, 0);
  }
}

void SyncInput::HandleInterrupt() {
  if (ADC_GetITStatus(ADC1, ADC_IT_AWD) != RESET) {
    if (enabled_) {
      if (armed_for_rising_edge_) {
        queue_.PushFromInterrupt(CycleCount());
        ArmRelease();
      } else {
        ArmRisingEdge();
      }
    }
    ADC_ClearITPendingBit(ADC1, ADC_IT_AWD);
  }
}

}  // namespace plaits

extern "C" {

void ADC1_IRQHandler(void) {
  plaits::SyncInput* sync_input = plaits::SyncInput::GetInstance();
  if (sync_input) {
    sync_input->HandleInterrupt();
  } else {
    ADC_ClearITPendingBit(ADC1, ADC_IT_AWD);
  }
}

}

#endif  // PLAITS_BUILD_ENABLE_SYNC_INPUT
