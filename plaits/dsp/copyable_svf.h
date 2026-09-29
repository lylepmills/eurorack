// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// stmlib::Svf's state-variable filter, with the same arithmetic, as a plain
// copyable struct.
//
// stmlib::Svf cannot be copied, and a per-sample loop that writes floats
// through a pointer makes the compiler reload and store a member filter's
// coefficients and state on every sample, since the store might alias them.
// On the module that cost real time in several engines. Copying this struct
// into a local for the loop (and back afterwards) lets them live in
// registers; ProcessBuffer does that itself.

#ifndef PLAITS_DSP_COPYABLE_SVF_H_
#define PLAITS_DSP_COPYABLE_SVF_H_

#include <stddef.h>

#include "stmlib/dsp/filter.h"

namespace plaits {

struct CopyableSvf {
  // stmlib::Svf::Init().
  inline void Init() {
    set_f_q<stmlib::FREQUENCY_DIRTY>(0.01f, 100.0f);
    Reset();
  }

  inline void Reset() {
    state_1 = state_2 = 0.0f;
  }

  template<stmlib::FrequencyApproximation approximation>
  inline void set_f_q(float f, float resonance) {
    g = stmlib::OnePole::tan<approximation>(f);
    r = 1.0f / resonance;
    h = 1.0f / (1.0f + r * g + g * g);
  }

  // stmlib::Svf::Process<mode>(float).
  template<stmlib::FilterMode mode>
  inline float Process(float in) {
    const float hp = (in - r * state_1 - g * state_1 - state_2) * h;
    const float bp = g * hp + state_1;
    state_1 = g * hp + bp;
    const float lp = g * bp + state_2;
    state_2 = g * bp + lp;
    if (mode == stmlib::FILTER_MODE_LOW_PASS) {
      return lp;
    } else if (mode == stmlib::FILTER_MODE_BAND_PASS) {
      return bp;
    } else if (mode == stmlib::FILTER_MODE_BAND_PASS_NORMALIZED) {
      return bp * r;
    } else {
      return hp;
    }
  }

  // stmlib::Svf::Process<mode>(in, out, size), on a local copy.
  template<stmlib::FilterMode mode>
  inline void ProcessBuffer(const float* in, float* out, size_t size) {
    CopyableSvf f = *this;
    while (size--) {
      *out++ = f.Process<mode>(*in++);
    }
    *this = f;
  }

  float g;
  float r;
  float h;
  float state_1;
  float state_2;
};

}  // namespace plaits

#endif  // PLAITS_DSP_COPYABLE_SVF_H_
