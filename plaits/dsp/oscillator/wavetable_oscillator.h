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
// Integrated wavetable synthesis.

#ifndef PLAITS_DSP_OSCILLATOR_WAVETABLE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_WAVETABLE_OSCILLATOR_H_

#include <algorithm>
#include "plaits/dsp/extended_tzfm.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/oscillator/oscillator.h"

namespace plaits {

class Differentiator {
 public:
  Differentiator() { }
  ~Differentiator() { }

  void Init() {
    previous_ = 0.0f;
    lp_ = 0.0f;
  }
  
  float Process(float coefficient, float s) {
    ONE_POLE(lp_, s - previous_, coefficient);
    previous_ = s;
    return lp_;
  }
 private:
  float lp_;
  float previous_;

  DISALLOW_COPY_AND_ASSIGN(Differentiator);
};

template<typename T>
inline float InterpolateWave(
    const T* table,
    int32_t index_integral,
    float index_fractional) {
  float a = static_cast<float>(table[index_integral]);
  float b = static_cast<float>(table[index_integral + 1]);
  float t = index_fractional;
  return a + (b - a) * t;
}

template<typename T>
inline float InterpolateWaveHermite(
    const T* table,
    int32_t index_integral,
    float index_fractional) {
  const float xm1 = static_cast<float>(table[index_integral]);
  const float x0 = static_cast<float>(table[index_integral + 1]);
  const float x1 = static_cast<float>(table[index_integral + 2]);
  const float x2 = static_cast<float>(table[index_integral + 3]);
  const float c = (x1 - xm1) * 0.5f;
  const float v = x0 - x1;
  const float w = c + v;
  const float a = w + v + (x2 - x0) * 0.5f;
  const float b_neg = w + a;
  const float f = index_fractional;
  return (((a * f) - b_neg) * f + c) * f + x0;
}

template<
    size_t wavetable_size,
    size_t num_waves,
    bool approximate_scale=true,
    bool attenuate_high_frequencies=true>
class WavetableOscillator {
 public:
  WavetableOscillator() { }
  ~WavetableOscillator() { }

  void Init() {
    phase_ = 0.0f;
    frequency_ = 0.0f;
    amplitude_ = 0.0f;
    waveform_ = 0.0f;
    lp_ = 0.0f;
    differentiator_.Init();
  }
  
  void Render(
      float frequency,
      float amplitude,
      float waveform,
      const int16_t* const* wavetable,
      float* out,
      size_t size,
      const float* root_frequency_offset = NULL,
      float frequency_offset_scale = 0.0f) {
    CONSTRAIN(frequency, 0.0000001f, kMaxFrequency)

    if (attenuate_high_frequencies) {
      amplitude *= 1.0f - 2.0f * frequency;
    }
    if (approximate_scale) {
      amplitude *= 1.0f / (frequency * 131072.0f);
    }

    stmlib::ParameterInterpolator frequency_modulation(
        &frequency_,
        frequency,
        size);
    stmlib::ParameterInterpolator amplitude_modulation(
        &amplitude_,
        amplitude,
        size);
    stmlib::ParameterInterpolator waveform_modulation(
        &waveform_,
        waveform * float(num_waves - 1.0001f),
        size);
    
    float lp = lp_;
    float phase = phase_;
    while (size--) {
      float f0 = frequency_modulation.Next();
      if (root_frequency_offset) {
        f0 += *root_frequency_offset++ * frequency_offset_scale;
        CONSTRAIN(f0, PLAITS_BUILD_EXTENDED_TZFM ? -kMaxFrequency : 0.0000001f, kMaxFrequency);
      }
      const float cutoff = std::min(float(wavetable_size) * (PLAITS_BUILD_EXTENDED_TZFM ? fabsf(f0) : f0), 1.0f);
      const float scale = approximate_scale ? 1.0f : 1.0f / ((PLAITS_BUILD_EXTENDED_TZFM ? std::max(fabsf(f0), 1.0e-7f) : f0) * 131072.0f);
      
      phase += f0;
      if (PLAITS_BUILD_EXTENDED_TZFM && phase < 0.0f) phase += 1.0f;
      if (phase >= 1.0f) {
        phase -= 1.0f;
      }
      
      const float waveform = waveform_modulation.Next();
      MAKE_INTEGRAL_FRACTIONAL(waveform);
      
      const float p = phase * float(wavetable_size);
      MAKE_INTEGRAL_FRACTIONAL(p);
      
      const float x0 = InterpolateWave(
          wavetable[waveform_integral], p_integral, p_fractional);
      const float x1 = InterpolateWave(
          wavetable[waveform_integral + 1], p_integral, p_fractional);
      
      float s;
#if PLAITS_BUILD_EXTENDED_TZFM
      if (root_frequency_offset) {
        // Average the derivative of the integrated table over the traversed
        // phase interval. This retains its box-filter anti-aliasing in either
        // direction. Near zero use a fixed spatial derivative, avoiding both
        // cancellation and division by zero without flipping output polarity.
        const float epsilon = std::max(fabsf(f0) * 0.5f, 1.0f / 65536.0f);
        const float center = phase - f0 * 0.5f;
        const float pa = TzfmWrap(center - epsilon) * wavetable_size;
        const float pb = TzfmWrap(center + epsilon) * wavetable_size;
        const int ia = int(pa), ib = int(pb);
        const float d0 = InterpolateWave(wavetable[waveform_integral], ib, pb - ib) -
            InterpolateWave(wavetable[waveform_integral], ia, pa - ia);
        const float d1 = InterpolateWave(wavetable[waveform_integral + 1], ib, pb - ib) -
            InterpolateWave(wavetable[waveform_integral + 1], ia, pa - ia);
        s = (d0 + (d1 - d0) * waveform_fractional) / (2.0f * epsilon);
        s *= approximate_scale ? frequency : 1.0f / 131072.0f;
        lp = s;
      } else
#endif
      s = differentiator_.Process(
          cutoff,
          (x0 + (x1 - x0) * waveform_fractional) * scale);
      ONE_POLE(lp, s, cutoff);
      *out++ += amplitude_modulation.Next() * lp;
    }
    lp_ = lp;
    phase_ = phase;
  }

 private:
  // Oscillator state.
  float phase_;

  // For interpolation of parameters.
  float frequency_;
  float amplitude_;
  float waveform_;
  float lp_;
  
  Differentiator differentiator_;
  
  DISALLOW_COPY_AND_ASSIGN(WavetableOscillator);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_WAVETABLE_OSCILLATOR_H_
