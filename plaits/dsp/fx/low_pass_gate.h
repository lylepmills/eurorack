// Copyright 2014 Emilie Gillet.
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
// Approximative low pass gate.

#ifndef PLAITS_DSP_FX_LOW_PASS_GATE_H_
#define PLAITS_DSP_FX_LOW_PASS_GATE_H_

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {
  
class LowPassGate {
 public:
  LowPassGate() { }
  ~LowPassGate() { }
  
  void Init() {
    previous_gain_ = 0.0f;
    state_1_ = state_2_ = 0.0f;
  }
  
  void Process(
      float gain,
      float frequency,
      float hf_bleed,
      float* in_out,
      size_t size) {
    stmlib::ParameterInterpolator gain_modulation(&previous_gain_, gain, size);
    const Coefficients c(frequency);
    float state_1 = state_1_;
    float state_2 = state_2_;
    while (size--) {
      const float s = *in_out * gain_modulation.Next();
      const float lp = c.LowPass(s, &state_1, &state_2);
      *in_out++ = lp + (s - lp) * hf_bleed;
    }
    state_1_ = state_1;
    state_2_ = state_2;
  }
  
  void Process(
      float gain,
      float frequency,
      float hf_bleed,
      float* in,
      short* out,
      size_t size,
      size_t stride) {
    BufferSource source = { in };
    ProcessSource(gain, frequency, hf_bleed, &source, out, size, stride);
  }

  struct BufferSource {
    const float* in;
    inline float Next() { return *in++; }
  };

  // As above, reading each input sample from source->Next(), so that the
  // caller can compute it on the fly instead of storing it first.
  template<typename Source>
  inline void ProcessSource(
      float gain,
      float frequency,
      float hf_bleed,
      Source* source,
      short* out,
      size_t size,
      size_t stride) {
    stmlib::ParameterInterpolator gain_modulation(&previous_gain_, gain, size);
    const StateSpace c(frequency, hf_bleed);
    float state_1 = state_1_;
    float state_2 = state_2_;
    while (size--) {
      const float s = source->Next() * gain_modulation.Next();
      const float y = c.y_in * s + c.y_1 * state_1 + c.y_2 * state_2;
      const float next_1 = c.s1_in * s + c.s1_1 * state_1 + c.s1_2 * state_2;
      const float next_2 = c.s2_in * s + c.s2_1 * state_1 + c.s2_2 * state_2;
      state_1 = next_1;
      state_2 = next_2;
      *out = stmlib::Clip16(1 + static_cast<int32_t>(y));
      out += stride;
    }
    state_1_ = state_1;
    state_2_ = state_2;
  }
  
 private:
  // stmlib::Svf set up as set_f_q<FREQUENCY_DIRTY>(frequency, 0.4f) and run
  // as Process<FILTER_MODE_LOW_PASS>, with the same arithmetic, but with its
  // coefficients and state in locals for the block. Held in an Svf member,
  // every sample reloaded and stored them, since the buffer being written
  // could alias them. Every Process sets the coefficients before use, so only
  // the state persists between blocks.
  struct Coefficients {
    explicit Coefficients(float f) {
      g = stmlib::OnePole::tan<stmlib::FREQUENCY_DIRTY>(f);
      r = 1.0f / 0.4f;
      h = 1.0f / (1.0f + r * g + g * g);
    }
    inline float LowPass(float in, float* state_1, float* state_2) const {
      float hp, bp, lp;
      hp = (in - r * *state_1 - g * *state_1 - *state_2) * h;
      bp = g * hp + *state_1;
      *state_1 = g * hp + bp;
      lp = g * bp + *state_2;
      *state_2 = g * bp + lp;
      return lp;
    }
    float g;
    float r;
    float h;
  };

  // The same filter and HF bleed as Coefficients::LowPass followed by
  // lp + (s - lp) * hf_bleed, written as a state-space system: the output and
  // both new states are each one short sum over (s, state_1, state_2), so
  // they no longer wait on one another. In the form above every sample was a
  // chain of about seven dependent multiply-adds, which on the module stalled
  // the FPU (~2.4 cycles per instruction). The states keep their meaning, so
  // the two forms can hand over mid-note; the output differs only in rounding.
  struct StateSpace {
    StateSpace(float f, float hf_bleed) {
      const Coefficients c(f);
      const float gh = c.g * c.h;
      const float k = c.r + c.g;
      // hp = h * (s - k * state_1 - state_2)
      // bp = g * hp + state_1,   state_1' = g * hp + bp
      // lp = g * bp + state_2,   state_2' = g * bp + lp
      const float bp_in = gh;
      const float bp_1 = 1.0f - gh * k;
      const float bp_2 = -gh;
      const float lp_in = c.g * bp_in;
      const float lp_1 = c.g * bp_1;
      const float lp_2 = 1.0f + c.g * bp_2;
      s1_in = 2.0f * gh;
      s1_1 = 1.0f - 2.0f * gh * k;
      s1_2 = -2.0f * gh;
      s2_in = lp_in + c.g * bp_in;
      s2_1 = lp_1 + c.g * bp_1;
      s2_2 = lp_2 + c.g * bp_2;
      const float dry = 1.0f - hf_bleed;
      y_in = hf_bleed + dry * lp_in;
      y_1 = dry * lp_1;
      y_2 = dry * lp_2;
    }
    float y_in, y_1, y_2;
    float s1_in, s1_1, s1_2;
    float s2_in, s2_1, s2_2;
  };

  float previous_gain_;
  float state_1_;
  float state_2_;
  
  DISALLOW_COPY_AND_ASSIGN(LowPassGate);
};

}  // namespace plaits

#endif  // PLAITS_DSP_FX_LOW_PASS_GATE_H_
