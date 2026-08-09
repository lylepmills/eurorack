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
// Simple sine oscillator (wavetable) + fast sine oscillator (magic circle).
//
// The fast implementation might glitch a bit under heavy modulations of the
// frequency.

#ifndef PLAITS_DSP_OSCILLATOR_SINE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_SINE_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/rsqrt.h"

#include "plaits/resources.h"

namespace plaits {
  
const float kSineLUTSize = 512.0f;
const size_t kSineLUTQuadrature = 128;
const size_t kSineLUTBits = 9;

// Safe for phase >= 0.0f, will wrap.
inline float Sine(float phase) {
  return stmlib::InterpolateWrap(lut_sine, phase, kSineLUTSize);
}

// Potentially unsafe, if phase >= 1.25.
inline float SineNoWrap(float phase) {
  return stmlib::Interpolate(lut_sine, phase, kSineLUTSize);
}

// With positive of negative phase modulation up to an index of 32.
inline float SinePM(uint32_t phase, float pm) {
  const float max_uint32 = 4294967296.0f;
  const int max_index = 32;
  const float offset = float(max_index);
  const float scale = max_uint32 / float(max_index * 2);

  phase += static_cast<uint32_t>((pm + offset) * scale) * max_index * 2;
  
  uint32_t integral = phase >> (32 - kSineLUTBits);
  float fractional = static_cast<float>(phase << kSineLUTBits) / float(max_uint32);
  float a = lut_sine[integral];
  float b = lut_sine[integral + 1];
  return a + (b - a) * fractional;
}

// Direct lookup without interpolation.
inline float SineRaw(uint32_t phase) {
  return lut_sine[phase >> (32 - kSineLUTBits)];
}

class SineOscillator {
 public:
  SineOscillator() { }
  ~SineOscillator() { }

  void Init() {
    phase_ = 0.0f;
    frequency_ = 0.0f;
    amplitude_ = 0.0f;
  }
  
  inline float Next(float frequency) {
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }
    
    phase_ += frequency;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }
    
    return SineNoWrap(phase_);
  }
  
  inline void Next(float frequency, float amplitude, float* sin, float* cos) {
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }
    
    phase_ += frequency;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }
    
    *sin = amplitude * SineNoWrap(phase_);
    *cos = amplitude * SineNoWrap(phase_ + 0.25f);
  }
  
  void Render(float frequency, float amplitude, float* out, size_t size) {
    RenderInternal<true>(frequency, amplitude, out, size);
  }
  
  void Render(float frequency, float* out, size_t size) {
    RenderInternal<false>(frequency, 1.0f, out, size);
  }

 private:
  template<bool additive>
  void RenderInternal(
      float frequency, float amplitude, float* out, size_t size) {
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }
    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    stmlib::ParameterInterpolator am(&amplitude_, amplitude, size);

    while (size--) {
      phase_ += fm.Next();
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      }
      float s = SineNoWrap(phase_);
      if (additive) {
        *out++ += am.Next() * s;
      } else {
        *out++ = s;
      }
    }
  }
  
  // Oscillator state.
  float phase_;

  // For interpolation of parameters.
  float frequency_;
  float amplitude_;
  
  DISALLOW_COPY_AND_ASSIGN(SineOscillator);
};

class FastSineOscillator {
 public:
  FastSineOscillator() { }
  ~FastSineOscillator() { }

  void Init() {
    x_ = 1.0f;
    y_ = 0.0f;
    epsilon_ = 0.0f;
    amplitude_ = 0.0f;
  }
  
  enum Mode {
    NORMAL,
    ADDITIVE,
    QUADRATURE
  };
  
  static inline float Fast2Sin(float f) {
    // In theory, epsilon = 2 sin(pi f)
    // Here, to avoid the call to sinf, we use a 3rd order polynomial
    // approximation, which looks like a Taylor expansion, but with a
    // correction term to give a good trade-off between average error
    // (1.13 cents) and maximum error (7.33 cents) when generating sinewaves
    // in the 16 Hz to 16kHz range (with sr = 48kHz).
    const float f_pi = f * float(M_PI);
    return f_pi * (2.0f - (2.0f * 0.96f / 6.0f) * f_pi * f_pi);
  }
  
  void Render(float frequency, float* out, size_t size) {
    RenderInternal<NORMAL, false>(
        frequency, 1.0f, out, NULL, size, 0, 1);
  }

  void Render(
      float frequency, float* out, size_t size, uint32_t hard_sync) {
    if (hard_sync) {
      RenderInternal<NORMAL, true>(
          frequency, 1.0f, out, NULL, size, hard_sync, 1);
    } else {
      RenderInternal<NORMAL, false>(
          frequency, 1.0f, out, NULL, size, 0, 1);
    }
  }
  
  void Render(float frequency, float amplitude, float* out, size_t size) {
    RenderInternal<ADDITIVE, false>(
        frequency, amplitude, out, NULL, size, 0, 1);
  }

  void Render(
      float frequency,
      float amplitude,
      float* out,
      size_t size,
      uint32_t hard_sync) {
    if (hard_sync) {
      RenderInternal<ADDITIVE, true>(
          frequency, amplitude, out, NULL, size, hard_sync, 1);
    } else {
      RenderInternal<ADDITIVE, false>(
          frequency, amplitude, out, NULL, size, 0, 1);
    }
  }

  void RenderLinearFm(
      float frequency,
      float amplitude,
      const float* frequency_offset,
      float* out,
      size_t size,
      uint32_t hard_sync = 0) {
    if (!frequency_offset) {
      Render(frequency, amplitude, out, size, hard_sync);
      return;
    }

    stmlib::ParameterInterpolator am(&amplitude_, amplitude, size);
    float x = x_;
    float y = y_;
    const float norm = x * x + y * y;
    if (norm <= 0.5f || norm >= 2.0f) {
      const float scale = stmlib::fast_rsqrt_carmack(norm);
      x *= scale;
      y *= scale;
    }
    while (size--) {
      if (hard_sync & 1) {
        x = 1.0f;
        y = 0.0f;
      }
      hard_sync >>= 1;
      float f = frequency + *frequency_offset++;
      CONSTRAIN(f, -0.249999f, 0.249999f);
      const float e = Fast2Sin(f);
      x += e * y;
      y -= e * x;
      const float level = am.Next() * (1.0f - fabsf(f) * 4.0f);
      *out++ += level * x;
    }
    x_ = x;
    y_ = y;
    epsilon_ = Fast2Sin(frequency);
  }

  void RenderQuadrature(
      float frequency, float amplitude, float* x, float* y, size_t size) {
    RenderInternal<QUADRATURE, false>(
        frequency, amplitude, x, y, size, 0, 1);
  }

  void RenderQuadratureLinearFm(
      float frequency,
      float amplitude,
      const float* frequency_offset,
      float* out,
      float* out_2,
      size_t size,
      uint32_t hard_sync = 0,
      size_t sync_sample_span = 1) {
    if (!frequency_offset) {
      RenderQuadrature(
          frequency,
          amplitude,
          out,
          out_2,
          size,
          hard_sync,
          sync_sample_span);
      return;
    }

    stmlib::ParameterInterpolator am(&amplitude_, amplitude, size);
    float x = x_;
    float y = y_;
    const float norm = x * x + y * y;
    if (norm <= 0.5f || norm >= 2.0f) {
      const float scale = stmlib::fast_rsqrt_carmack(norm);
      x *= scale;
      y *= scale;
    }

    size_t sync_subsample = 0;
    while (size--) {
      if (sync_subsample == 0) {
        if (hard_sync & 1) {
          x = 1.0f;
          y = 0.0f;
        }
        hard_sync >>= 1;
      }
      if (++sync_subsample >= sync_sample_span) {
        sync_subsample = 0;
      }

      float f = frequency + *frequency_offset++;
      CONSTRAIN(f, -0.249999f, 0.249999f);
      const float e = Fast2Sin(f);
      x += e * y;
      y -= e * x;
      const float level = am.Next() * (1.0f - fabsf(f) * 4.0f);
      *out++ = x * level;
      *out_2++ = y * level;
    }
    x_ = x;
    y_ = y;
    epsilon_ = Fast2Sin(frequency);
  }

  // hard_sync contains one bit per OUTPUT sample. sync_sample_span maps each
  // bit to this many oscillator samples, which lets an oversampled engine reset
  // before the first sub-sample without expanding a 24-bit event mask to 48.
  void RenderQuadrature(
      float frequency,
      float amplitude,
      float* x,
      float* y,
      size_t size,
      uint32_t hard_sync,
      size_t sync_sample_span) {
    if (hard_sync) {
      RenderInternal<QUADRATURE, true>(
          frequency,
          amplitude,
          x,
          y,
          size,
          hard_sync,
          sync_sample_span);
    } else {
      RenderInternal<QUADRATURE, false>(
          frequency, amplitude, x, y, size, 0, 1);
    }
  }
  
 private:
  template<Mode mode, bool process_hard_sync>
  void RenderInternal(
      float frequency,
      float amplitude,
      float* out,
      float* out_2,
      size_t size,
      uint32_t hard_sync,
      size_t sync_sample_span) {
    if (frequency >= 0.25f) {
      frequency = 0.25f;
      amplitude = 0.0f;
    } else {
      amplitude *= 1.0f - frequency * 4.0f;
    }
    
    stmlib::ParameterInterpolator epsilon(&epsilon_, Fast2Sin(frequency), size);
    stmlib::ParameterInterpolator am(&amplitude_, amplitude, size);
    float x = x_;
    float y = y_;
    
    const float norm = x * x + y * y;
    if (norm <= 0.5f || norm >= 2.0f) {
      const float scale = stmlib::fast_rsqrt_carmack(norm);
      x *= scale;
      y *= scale;
    }

    size_t sync_subsample = 0;
    
    while (size--) {
      if (process_hard_sync) {
        if (sync_subsample == 0) {
          if (hard_sync & 1) {
            // Phase zero for the magic-circle oscillator is (1, 0). Reset
            // only the trajectory; parameter interpolation stays continuous.
            x = 1.0f;
            y = 0.0f;
          }
          hard_sync >>= 1;
        }
        if (++sync_subsample >= sync_sample_span) {
          sync_subsample = 0;
        }
      }
      const float e = epsilon.Next();
      x += e * y;
      y -= e * x;
      if (mode == ADDITIVE) {
        *out++ += am.Next() * x;
      } else if (mode == NORMAL) {
        *out++ = x;
      } else if (mode == QUADRATURE) {
        const float amplitude = am.Next();
        *out++ = x * amplitude;
        *out_2++ = y * amplitude;
      }
    }
    x_ = x;
    y_ = y;
  }
     
  // Oscillator state.
  float x_;
  float y_;

  // For interpolation of parameters.
  float epsilon_;
  float amplitude_;
  
  DISALLOW_COPY_AND_ASSIGN(FastSineOscillator);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_SINE_OSCILLATOR_H_
