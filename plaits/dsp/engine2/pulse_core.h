// Copyright 2026 Combust.
// SPDX-License-Identifier: MIT
//
// Shared pulse core for the 1-bit engine family. Renders superposed trains of
// narrow rectangular pulses ("pins"). Band-limiting is applied to pulse
// *edges* only (2-sample polyBLEP pair) — the spectrum is never low-passed;
// fold-down from narrow pins is the intended timbre. Each channel also
// exposes the raw aliased value so an engine can present the un-band-limited
// bus as its nasty alternate output.

#ifndef PLAITS_DSP_ENGINE2_PULSE_CORE_H_
#define PLAITS_DSP_ENGINE2_PULSE_CORE_H_

#include "plaits/dsp/dsp.h"

namespace plaits {
namespace pulse {

// PolyBLEP residual for a unit step at phase 0, transition width dt.
inline float PolyBlep(float t, float dt) {
  if (t < dt) {
    const float x = t / dt;
    return x + x - x * x - 1.0f;
  } else if (t > 1.0f - dt) {
    const float x = (t - 1.0f) / dt;
    return x * x + x + x + 1.0f;
  }
  return 0.0f;
}

// One pin train: unipolar rectangular pulses of width `duty` (as a fraction
// of the period), DC-removed so superposition stays centered. `clean` output
// has polyBLEP'd edges; `naive` is the raw aliased rectangle.
class PinChannel {
 public:
  void Init() { phase_ = 0.0f; }
  void Reset() { phase_ = 0.0f; }

  // dt: normalized frequency (phase increment / sample), caller-clamped.
  // duty: pulse width as a phase fraction, caller-clamped to [dt, 0.45].
  inline float Next(float dt, float duty, float* naive_out) {
    phase_ += dt;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }
    const float t = phase_;
    const float naive = t < duty ? 1.0f - duty : -duty;
    float tf = t - duty;
    if (tf < 0.0f) {
      tf += 1.0f;
    }
    *naive_out = naive;
    return naive + PolyBlep(t, dt) - PolyBlep(tf, dt);
  }

  inline float phase() const { return phase_; }

 private:
  float phase_;
};

// The register-write grid: all internal modulation (arps, echo taps, duty
// animation) quantizes to this tick; external CV stays continuous.
class FrameTick {
 public:
  void Init(int32_t period_samples) {
    period_ = period_samples;
    counter_ = 1;
  }
  void Reset() { counter_ = 1; }

  // True when this sample lands on a tick boundary.
  inline bool Next() {
    if (--counter_ <= 0) {
      counter_ = period_;
      return true;
    }
    return false;
  }

 private:
  int32_t period_;
  int32_t counter_;
};

// 50 Hz at Plaits' corrected 48 kHz rate.
const int32_t kFrameTickPeriod = 957;

}  // namespace pulse
}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_PULSE_CORE_H_
