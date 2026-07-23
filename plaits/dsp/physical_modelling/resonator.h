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
      r[i] = 1.0f / q[i];
      h[i] = 1.0f / (1.0f + r[i] * g[i] + g[i] * g[i]);
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

  // Same filter bank, but each mode is multiply-accumulated into two output
  // buffers with its own pair of gains.
  template<stmlib::FilterMode mode, bool add>
  void ProcessStereo(
      const float* f,
      const float* q,
      const float* left_gain,
      const float* right_gain,
      const float* in,
      float* left,
      float* right,
      size_t size) {
    float g[batch_size];
    float r[batch_size];
    float r_plus_g[batch_size];
    float h[batch_size];
    float state_1[batch_size];
    float state_2[batch_size];
    float left_gains[batch_size];
    float right_gains[batch_size];
    for (int i = 0; i < batch_size; ++i) {
      g[i] = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
      r[i] = 1.0f / q[i];
      h[i] = 1.0f / (1.0f + r[i] * g[i] + g[i] * g[i]);
      r_plus_g[i] = r[i] + g[i];
      state_1[i] = state_1_[i];
      state_2[i] = state_2_[i];
      left_gains[i] = left_gain[i];
      right_gains[i] = right_gain[i];
    }

    while (size--) {
      float s_in = *in++;
      float s_left = 0.0f;
      float s_right = 0.0f;
      for (int i = 0; i < batch_size; ++i) {
        const float hp = (s_in - r_plus_g[i] * state_1[i] - state_2[i]) * h[i];
        const float bp = g[i] * hp + state_1[i];
        state_1[i] = g[i] * hp + bp;
        const float lp = g[i] * bp + state_2[i];
        state_2[i] = g[i] * bp + lp;
        const float s = (mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp;
        s_left += left_gains[i] * s;
        s_right += right_gains[i] * s;
      }
      if (add) {
        *left++ += s_left;
        *right++ += s_right;
      } else {
        *left++ = s_left;
        *right++ = s_right;
      }
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
  // mode remains audible on both channels.
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
