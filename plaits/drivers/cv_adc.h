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
// Drivers for the 16-bit SDADC scanning the CV inputs.

#ifndef PLAITS_DRIVERS_CV_ADC_H_
#define PLAITS_DRIVERS_CV_ADC_H_

#include <stddef.h>

#include "stmlib/stmlib.h"

#include "plaits/drivers/audio_rate_fm_resampler.h"

namespace plaits {

enum CvAdcChannel {
  CV_ADC_CHANNEL_MODEL,
  CV_ADC_CHANNEL_V_OCT,
  CV_ADC_CHANNEL_FM,
  CV_ADC_CHANNEL_HARMONICS,
  CV_ADC_CHANNEL_TIMBRE,
  CV_ADC_CHANNEL_MORPH,
  CV_ADC_CHANNEL_TRIGGER,
  CV_ADC_CHANNEL_LEVEL,
  CV_ADC_CHANNEL_LAST
};

class CvAdc {
 public:
  enum FmAcquisitionMode {
    FM_ACQUISITION_CONTROL_RATE,
    FM_ACQUISITION_FAST,
    FM_ACQUISITION_FAST_WITH_LEVEL
  };

  static const size_t kAudioRateFmBufferSize =
      AudioRateFmResampler::kRingBufferSize;

  CvAdc() { }
  ~CvAdc() { }
  
  void Init();
  void DeInit();
  void Convert();
  // The TZFM qualification firmware assigns one acquisition strategy to each
  // bank. Ordinary firmware never changes this after Init().
  void SetFmAcquisitionMode(FmAcquisitionMode mode);
  inline FmAcquisitionMode fm_acquisition_mode() const {
    return fm_acquisition_mode_;
  }
  inline bool level_available() const {
    return fm_acquisition_mode_ != FM_ACQUISITION_FAST;
  }
  inline bool fm_is_audio_rate() const {
    return fm_acquisition_mode_ != FM_ACQUISITION_CONTROL_RATE;
  }
  // Resamples the continuous 50 kHz FM conversion stream onto the exact Plaits
  // synthesis clock. In a regular build this repeats the latest control-rate
  // reading, keeping the driver API and class layout recipe-independent.
  void CopyAudioRateFm(float* destination, size_t size);
  // State persistence and engine initialization can block the audio consumer
  // while the DMA producer keeps running. Restart acquisition after either
  // planned pause without latching a false fault.
  void RealignAudioRateFmAfterKnownPause(size_t block_size);
  inline uint32_t audio_rate_fm_faults() const {
    return audio_rate_fm_overruns_ +
        audio_rate_fm_resampler_.resync_count() -
        audio_rate_fm_acknowledged_resyncs_;
  }
  inline uint32_t audio_rate_fm_overruns() const {
    return audio_rate_fm_overruns_;
  }
  inline uint32_t audio_rate_fm_resyncs() const {
    return audio_rate_fm_resampler_.resync_count() -
        audio_rate_fm_acknowledged_resyncs_;
  }
  inline uint32_t audio_rate_fm_underflows() const {
    return audio_rate_fm_resampler_.underflow_count() -
        audio_rate_fm_acknowledged_underflows_;
  }
  inline uint32_t audio_rate_fm_excess_lag() const {
    return audio_rate_fm_resampler_.excess_lag_count() -
        audio_rate_fm_acknowledged_excess_lag_;
  }

  inline int16_t value(CvAdcChannel channel) const {
    return values_[channel_map_[channel]];
  }
  
  inline float float_value(CvAdcChannel channel) const {
    return static_cast<float>(value(channel)) / 32768.0f;
  }
  
 private:
  void ConfigureSdadc2(FmAcquisitionMode mode);
  size_t AudioRateFmWriteIndex() const;

  int channel_map_[CV_ADC_CHANNEL_LAST];
  int16_t values_[CV_ADC_CHANNEL_LAST];
  volatile int16_t audio_rate_fm_[kAudioRateFmBufferSize];
  AudioRateFmResampler audio_rate_fm_resampler_;
  uint32_t audio_rate_fm_overruns_;
  uint32_t audio_rate_fm_acknowledged_resyncs_;
  uint32_t audio_rate_fm_acknowledged_underflows_;
  uint32_t audio_rate_fm_acknowledged_excess_lag_;
  FmAcquisitionMode fm_acquisition_mode_;
  uint8_t level_injection_divider_;
  bool level_injection_pending_;
  
  DISALLOW_COPY_AND_ASSIGN(CvAdc);
};

}  // namespace plaits

#endif  // PLAITS_DRIVERS_CV_ADC_H_
