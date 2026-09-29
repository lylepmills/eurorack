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
// Resonator, taken from Rings' code but with fixed position.

#include "plaits/dsp/physical_modelling/resonator.h"

#include <algorithm>

#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/engine/engine.h"
#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void Resonator::Init(float position, int resolution) {
  resolution_ = min(resolution, kMaxNumModes);
  
  CosineOscillator amplitudes;
  amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(position);
  
  for (int i = 0; i < resolution; ++i) {
    mode_amplitude_[i] = amplitudes.Next() * 0.25f;
  }
  
  for (int i = 0; i < kMaxNumModes / kModeBatchSize; ++i) {
    mode_filters_[i].Init();
  }
}

inline float NthHarmonicCompensation(int n, float stiffness) {
  float stretch_factor = 1.0f;
  for (int i = 0; i < n - 1; ++i) {
    stretch_factor += stiffness;
    if (stiffness < 0.0f) {
      stiffness *= 0.93f;
    } else {
      stiffness *= 0.98f;
    }
  }
  return 1.0f / stretch_factor;
}

// True when all four modes of a batch are clamped at 0.499. Such modes are
// scaled by mode_attenuation = 0.002 and all ring on one tone near 21 kHz; at
// the top of the keyboard they are most of the modes, and filtering them cost
// about half of Modal's block. Skipping them (and clearing their state, so a
// mode that comes back below the clamp starts from rest) changes the output
// by about -39 dB at 20-22 kHz and -60 dB below 16 kHz at note 108.
static inline bool AllAtNyquist(const float* f) {
  return f[0] >= 0.499f && f[1] >= 0.499f && f[2] >= 0.499f && f[3] >= 0.499f;
}

void Resonator::Process(
    float f0,
    float structure,
    float brightness,
    float damping,
    const float* in,
    float* out,
    size_t size) {
  float stiffness = Interpolate(lut_stiffness, structure, 64.0f);
  f0 *= NthHarmonicCompensation(3, stiffness);
  
  float harmonic = f0;
  float stretch_factor = 1.0f;
  float q_sqrt = SemitonesToRatio(damping * 79.7f);
  float q = 500.0f * q_sqrt * q_sqrt;
  brightness *= 1.0f - structure * 0.3f;
  brightness *= 1.0f - damping * 0.3f;
  float q_loss = brightness * (2.0f - brightness) * 0.85f + 0.15f;
  
  float mode_q[kModeBatchSize];
  float mode_f[kModeBatchSize];
  float mode_a[kModeBatchSize];
  int batch_counter = 0;
  ResonatorSvf<kModeBatchSize>* batch_processor = &mode_filters_[0];
  for (int i = 0; i < resolution_; ++i) {
    float mode_frequency = harmonic * stretch_factor;
    if (mode_frequency >= 0.499f) {
      mode_frequency = 0.499f;
    }
    const float mode_attenuation = 1.0f - mode_frequency * 2.0f;
    
    mode_f[batch_counter] = mode_frequency;
    mode_q[batch_counter] = 1.0f + mode_frequency * q;
    mode_a[batch_counter] = mode_amplitude_[i] * mode_attenuation;
    ++batch_counter;
    
    if (batch_counter == kModeBatchSize) {
      batch_counter = 0;
      if (AllAtNyquist(mode_f)) {
        batch_processor->Init();
      } else {
        batch_processor->Process<FILTER_MODE_BAND_PASS, true>(
            mode_f,
            mode_q,
            mode_a,
            in,
            out,
            size);
      }
      ++batch_processor;
    }
    
    stretch_factor += stiffness;
    if (stiffness < 0.0f) {
      // Make sure that the partials do not fold back into negative frequencies.
      stiffness *= 0.93f;
    } else {
      // This helps adding a few extra partials in the highest frequencies.
      stiffness *= 0.98f;
    }
    harmonic += f0;
    q *= q_loss;
  }
}

void Resonator::ProcessStereo(
    float f0,
    float structure,
    float brightness,
    float damping,
    const float* in,
    float* left,
    float* right,
    size_t size) {
  float stiffness = Interpolate(lut_stiffness, structure, 64.0f);
  f0 *= NthHarmonicCompensation(3, stiffness);

  float harmonic = f0;
  float stretch_factor = 1.0f;
  float q_sqrt = SemitonesToRatio(damping * 79.7f);
  float q = 500.0f * q_sqrt * q_sqrt;
  brightness *= 1.0f - structure * 0.3f;
  brightness *= 1.0f - damping * 0.3f;
  float q_loss = brightness * (2.0f - brightness) * 0.85f + 0.15f;

  // Exact binary32 results of sqrtf(0.83f) and sqrtf(0.17f). The positions are
  // mirror images, so the odd-mode gains are the even-mode gains swapped.
  // Keeping these fixed values avoids four square roots on every audio block.
  const float even_left = 0x1.d27446p-1f;
  const float even_right = 0x1.a634bep-2f;
  const float odd_left = even_right;
  const float odd_right = even_left;

  // Batches start at mode multiples of kModeBatchSize, so a mode's parity is
  // its position's parity within the batch: the even modes are summed into
  // `left` and the odd ones into `right`, and the two sums are panned below.
  float mode_q[kModeBatchSize];
  float mode_f[kModeBatchSize];
  float mode_a[kModeBatchSize];
  int batch_counter = 0;
  ResonatorSvf<kModeBatchSize>* batch_processor = &mode_filters_[0];
  for (int i = 0; i < resolution_; ++i) {
    float mode_frequency = harmonic * stretch_factor;
    if (mode_frequency >= 0.499f) {
      mode_frequency = 0.499f;
    }
    const float mode_attenuation = 1.0f - mode_frequency * 2.0f;

    mode_f[batch_counter] = mode_frequency;
    mode_q[batch_counter] = 1.0f + mode_frequency * q;
    mode_a[batch_counter] = mode_amplitude_[i] * mode_attenuation;
    ++batch_counter;

    if (batch_counter == kModeBatchSize) {
      batch_counter = 0;
      if (AllAtNyquist(mode_f)) {
        batch_processor->Init();
      } else {
        batch_processor->ProcessEvenOdd<FILTER_MODE_BAND_PASS>(
            mode_f,
            mode_q,
            mode_a,
            in,
            left,
            right,
            size);
      }
      ++batch_processor;
    }

    stretch_factor += stiffness;
    if (stiffness < 0.0f) {
      // Make sure that the partials do not fold back into negative frequencies.
      stiffness *= 0.93f;
    } else {
      // This helps adding a few extra partials in the highest frequencies.
      stiffness *= 0.98f;
    }
    harmonic += f0;
    q *= q_loss;
  }

  // left and right hold the even- and odd-mode sums (they start at zero: the
  // voice clears both before rendering). Pan them to the two sides.
  for (size_t n = 0; n < size; ++n) {
    const float even = left[n];
    const float odd = right[n];
    left[n] = even_left * even + odd_left * odd;
    right[n] = even_right * even + odd_right * odd;
  }
}

}  // namespace plaits
