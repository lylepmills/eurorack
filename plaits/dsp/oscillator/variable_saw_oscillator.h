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
// Saw with variable slope or notch

#ifndef PLAITS_DSP_OSCILLATOR_VARIABLE_SAW_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_VARIABLE_SAW_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include <algorithm>

#include "plaits/dsp/oscillator/oscillator.h"

namespace plaits {

const float kVariableSawNotchDepth = 0.2f;

class VariableSawOscillator {
 public:
  VariableSawOscillator() { }
  ~VariableSawOscillator() { }

  void Init() {
    phase_ = 0.0f;
    next_sample_ = 0.0f;
    previous_pw_ = 0.5f;
    high_ = false;
  
    frequency_ = 0.01f;
    pw_ = 0.5f;
    waveshape_ = 0.0f;
  }
  
  void Render(
      float frequency,
      float pw,
      float waveshape,
      float* out,
      size_t size,
      uint32_t hard_sync = 0) {
    // Most blocks contain no external reset. Select the original zero-event
    // loop once here rather than paying for a mask test and shift per sample.
    if (hard_sync) {
      RenderInternal<true>(
          frequency, pw, waveshape, out, size, hard_sync);
    } else {
      RenderInternal<false>(
          frequency, pw, waveshape, out, size, 0);
    }
  }

  // Signed per-sample displacement used only by the experimental linear-TZFM
  // path. The ordinary bandlimited positive-frequency renderer above remains
  // untouched; this loop trades some high-frequency antialiasing for correct
  // phase reversal at zero.
  void RenderLinearFm(
      float frequency,
      float pw,
      float waveshape,
      const float* frequency_offset,
      float* out,
      size_t size,
      uint32_t hard_sync = 0) {
    if (!frequency_offset) {
      Render(frequency, pw, waveshape, out, size, hard_sync);
      return;
    }

    CONSTRAIN(frequency, -kMaxFrequency, kMaxFrequency);
    const float maximum_frequency = fabsf(frequency);
    if (maximum_frequency >= 0.25f) {
      pw = 0.5f;
    } else {
      CONSTRAIN(
          pw,
          maximum_frequency * 2.0f,
          1.0f - 2.0f * maximum_frequency);
    }

    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    stmlib::ParameterInterpolator pwm(&pw_, pw, size);
    stmlib::ParameterInterpolator waveshape_modulation(
        &waveshape_, waveshape, size);

    while (size--) {
      float f = fm.Next() + *frequency_offset++;
      CONSTRAIN(f, -kMaxFrequency, kMaxFrequency);
      const float pw = pwm.Next();
      const float waveshape = waveshape_modulation.Next();
      const float triangle_amount = waveshape;
      const float notch_amount = 1.0f - waveshape;
      const float slope_up = 1.0f / pw;
      const float slope_down = 1.0f / (1.0f - pw);

      if (hard_sync & 1) {
        phase_ = 0.0f;
      }
      hard_sync >>= 1;

      phase_ += f;
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      } else if (phase_ < 0.0f) {
        phase_ += 1.0f;
      }
      high_ = phase_ >= pw;
      const float sample = ComputeNaiveSample(
          phase_,
          pw,
          slope_up,
          slope_down,
          triangle_amount,
          notch_amount);
      *out++ = (2.0f * sample - 1.0f) /
          (1.0f + kVariableSawNotchDepth);
      previous_pw_ = pw;
      next_sample_ = sample;
    }
  }

 private:
  template<bool process_hard_sync>
  void RenderInternal(
      float frequency,
      float pw,
      float waveshape,
      float* out,
      size_t size,
      uint32_t hard_sync) {
    if (frequency >= kMaxFrequency) {
      frequency = kMaxFrequency;
    }
    
    if (frequency >= 0.25f) {
      pw = 0.5f;
    } else {
      CONSTRAIN(pw, frequency * 2.0f, 1.0f - 2.0f * frequency);
    }

    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    stmlib::ParameterInterpolator pwm(&pw_, pw, size);
    stmlib::ParameterInterpolator waveshape_modulation(
        &waveshape_, waveshape, size);

    float next_sample = next_sample_;
    
    while (size--) {
      float this_sample = next_sample;
      next_sample = 0.0f;
    
      const float frequency = fm.Next();
      const float pw = pwm.Next();
      const float waveshape = waveshape_modulation.Next();
      const float triangle_amount = waveshape;
      const float notch_amount = 1.0f - waveshape;
      const float slope_up = 1.0f / (pw);
      const float slope_down = 1.0f / (1.0f - pw);

      if (process_hard_sync) {
        if (hard_sync & 1) {
          // Reset at the requested sample boundary, with a causal polyBLEP for
          // the arbitrary waveform-value discontinuity.
          const float value = ComputeNaiveSample(
              phase_,
              pw,
              slope_up,
              slope_down,
              triangle_amount,
              notch_amount);
          this_sample -= value * stmlib::ThisBlepSample(1.0f);
          next_sample -= value * stmlib::NextBlepSample(1.0f);
          phase_ = 0.0f;
          high_ = false;
        }
        hard_sync >>= 1;
      }

      phase_ += frequency;
      
      if (!high_ && phase_ >= pw) {
        const float triangle_step = (slope_up + slope_down) * frequency * triangle_amount;
        const float notch = (kVariableSawNotchDepth + 1.0f - pw) * notch_amount;
        const float t = (phase_ - pw) / (previous_pw_ - pw + frequency);
        this_sample += notch * stmlib::ThisBlepSample(t);
        next_sample += notch * stmlib::NextBlepSample(t);
        this_sample -= triangle_step * stmlib::ThisIntegratedBlepSample(t);
        next_sample -= triangle_step * stmlib::NextIntegratedBlepSample(t);
        high_ = true;
      } else if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
        const float triangle_step = (slope_up + slope_down) * frequency * triangle_amount;
        const float notch = (kVariableSawNotchDepth + 1.0f) * notch_amount;
        const float t = phase_ / frequency;
        this_sample -= notch * stmlib::ThisBlepSample(t);
        next_sample -= notch * stmlib::NextBlepSample(t);
        this_sample += triangle_step * stmlib::ThisIntegratedBlepSample(t);
        next_sample += triangle_step * stmlib::NextIntegratedBlepSample(t);
        high_ = false;
      }
    
      next_sample += ComputeNaiveSample(
          phase_,
          pw,
          slope_up,
          slope_down,
          triangle_amount,
          notch_amount);
      previous_pw_ = pw;

      *out++ = (2.0f * this_sample - 1.0f) / (1.0f + kVariableSawNotchDepth);
    }
    
    next_sample_ = next_sample;
  }


 private:
  inline float ComputeNaiveSample(
      float phase,
      float pw,
      float slope_up,
      float slope_down,
      float triangle_amount,
      float notch_amount) const {
    float notch_saw = phase < pw ? phase : 1.0f + kVariableSawNotchDepth;
    float triangle = phase < pw
        ? phase * slope_up
        : 1.0f - (phase - pw) * slope_down;
    return notch_saw * notch_amount + triangle * triangle_amount;
  }

  // Oscillator state.
  float phase_;
  float next_sample_;
  float previous_pw_;
  bool high_;

  // For interpolation of parameters.
  float frequency_;
  float pw_;
  float waveshape_;

  DISALLOW_COPY_AND_ASSIGN(VariableSawOscillator);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_VARIABLE_SAW_OSCILLATOR_H_
