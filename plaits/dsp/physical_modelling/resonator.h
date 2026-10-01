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

#ifndef PLAITS_DSP_PHYSICAL_MODELLING_RESONATOR_H_
#define PLAITS_DSP_PHYSICAL_MODELLING_RESONATOR_H_

#include "stmlib/dsp/filter.h"

namespace plaits {

const int kMaxNumModes = 24;
const int kModeBatchSize = 4;

// We render 4 modes simultaneously since there are enough registers to hold
// all state variables.
template<int batch_size>
class ResonatorSvf {
 public:
  ResonatorSvf() { }
  ~ResonatorSvf() { }
  
  void Init() {
    for (int i = 0; i < batch_size; ++i) {
      state_1_[i] = state_2_[i] = 0.0f;
    }
  }
  
  template<stmlib::FilterMode mode, bool add>
  void Process(
      const float* f,
      const float* q,
      const float* gain,
      const float* in,
      float* out,
      size_t size) {
    float g[batch_size];
    float r[batch_size];
    float r_plus_g[batch_size];
    float h[batch_size];
    float state_1[batch_size];
    float state_2[batch_size];
    float gains[batch_size];
    for (int i = 0; i < batch_size; ++i) {
      g[i] = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
      // One divide where there were two (14 cycles each, FPU stalled, for
      // every mode on every block): with d = q + g + q g^2,
      // r = 1 / q = d / (q d) and h = 1 / (1 + r g + g^2) = q^2 / (q d).
      // Same coefficients up to rounding; q >= 1, so q d cannot underflow.
      const float d = q[i] + g[i] + q[i] * g[i] * g[i];
      const float inverse = 1.0f / (q[i] * d);
      r[i] = d * inverse;
      h[i] = q[i] * q[i] * inverse;
      r_plus_g[i] = r[i] + g[i];
      state_1[i] = state_1_[i];
      state_2[i] = state_2_[i];
      gains[i] = gain[i];
    }
    
    while (size--) {
      float s_in = *in++;
      float s_out = 0.0f;
      for (int i = 0; i < batch_size; ++i) {
        const float hp = (s_in - r_plus_g[i] * state_1[i] - state_2[i]) * h[i];
        const float bp = g[i] * hp + state_1[i];
        state_1[i] = g[i] * hp + bp;
        const float lp = g[i] * bp + state_2[i];
        state_2[i] = g[i] * bp + lp;
        s_out += gains[i] * ((mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp);
      }
      if (add) {
        *out++ += s_out;
      } else {
        *out++ = s_out;
      }
    }
    for (int i = 0; i < batch_size; ++i) {
      state_1_[i] = state_1[i];
      state_2_[i] = state_2[i];
    }
  }

  // Same filter bank, but the modes at even batch positions accumulate into
  // `even` and those at odd positions into `odd`, each with one gain as in
  // Process(). Resonator::ProcessStereo pans by mode parity with one fixed
  // gain pair per parity, so it applies the panning to the two sums once per
  // sample instead of carrying a second gain and accumulator per mode, which
  // ran out of FPU registers and nearly doubled the per-mode cost.
  template<stmlib::FilterMode mode>
  void ProcessEvenOdd(
      const float* f,
      const float* q,
      const float* gain,
      const float* in,
      float* even,
      float* odd,
      size_t size) {
    float g[batch_size];
    float r[batch_size];
    float r_plus_g[batch_size];
    float h[batch_size];
    float state_1[batch_size];
    float state_2[batch_size];
    float gains[batch_size];
    for (int i = 0; i < batch_size; ++i) {
      g[i] = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
      // One divide where there were two (14 cycles each, FPU stalled, for
      // every mode on every block): with d = q + g + q g^2,
      // r = 1 / q = d / (q d) and h = 1 / (1 + r g + g^2) = q^2 / (q d).
      // Same coefficients up to rounding; q >= 1, so q d cannot underflow.
      const float d = q[i] + g[i] + q[i] * g[i] * g[i];
      const float inverse = 1.0f / (q[i] * d);
      r[i] = d * inverse;
      h[i] = q[i] * q[i] * inverse;
      r_plus_g[i] = r[i] + g[i];
      state_1[i] = state_1_[i];
      state_2[i] = state_2_[i];
      gains[i] = gain[i];
    }

    while (size--) {
      float s_in = *in++;
      float s_even = 0.0f;
      float s_odd = 0.0f;
      for (int i = 0; i < batch_size; ++i) {
        const float hp = (s_in - r_plus_g[i] * state_1[i] - state_2[i]) * h[i];
        const float bp = g[i] * hp + state_1[i];
        state_1[i] = g[i] * hp + bp;
        const float lp = g[i] * bp + state_2[i];
        state_2[i] = g[i] * bp + lp;
        const float s = (mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp;
        if (i & 1) {
          s_odd += gains[i] * s;
        } else {
          s_even += gains[i] * s;
        }
      }
      *even++ += s_even;
      *odd++ += s_odd;
    }
    for (int i = 0; i < batch_size; ++i) {
      state_1_[i] = state_1[i];
      state_2_[i] = state_2[i];
    }
  }

 private:
  float state_1_[batch_size];
  float state_2_[batch_size];
  
  DISALLOW_COPY_AND_ASSIGN(ResonatorSvf);
};

class Resonator {
 public:
  Resonator() { }
  ~Resonator() { }
  
  void Init(float position, int resolution);
  void Process(
      float f0,
      float structure,
      float brightness,
      float damping,
      const float* in,
      float* out,
      size_t size);
  // alt firmware: stereo variant - even-numbered modes lean left and
  // odd-numbered modes lean right, with equal-power gains, so that every
  // mode remains audible on both channels. Unlike Process(), it does not
  // add to `left`/`right`: both must be zero on entry (it sums the two
  // parities there, then pans them in place).
  void ProcessStereo(
      float f0,
      float structure,
      float brightness,
      float damping,
      const float* in,
      float* left,
      float* right,
      size_t size);

 private:
  int resolution_;
  
  float mode_amplitude_[kMaxNumModes];
  ResonatorSvf<kModeBatchSize> mode_filters_[kMaxNumModes / kModeBatchSize];
  
  DISALLOW_COPY_AND_ASSIGN(Resonator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_PHYSICAL_MODELLING_RESONATOR_H_
