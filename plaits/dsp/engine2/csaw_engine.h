// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' CSAW: a sawtooth with a notch cut into the start of every cycle.
//
// The algorithm is Emilie Gillet's AnalogOscillator::RenderCSaw plus the
// DC-shift and make-up stage MacroOscillator::RenderCSaw wraps around it. The
// emblematic Braids waveform, and the one model the module is most recognised
// for.
//
// Two controls Braids does not have:
//   MORPH bends the sawtooth segment, concave through straight to convex.
//   MACRO tilts the notch plateau. At +1 the value step at the notch edge
//   CANCELS, leaving a pure slope discontinuity; at -1 it doubles.
//
// Because MACRO can cancel the value step, a value-BLEP alone would leave the
// engine aliasing worst exactly where the new control is most interesting, so
// the slope discontinuities carry integrated BLEP as well.
//
// OUT: the notched saw. AUX: the same waveform with the notch depth taken
// from the MIRRORED harmonics position, so a deep notch on one output pairs
// with a shallow one on the other.
//
// Declared deviations from Braids:
//   - AUX mirrors the notch depth rather than negating it. Negating puts the
//     plateau at -0.375 and the output at 1.42 before the registered gain,
//     which clips under a positive gain and breaks Pattern A's matched-gain
//     stereo. Mirroring keeps AUX inside OUT's own bounds.
//   - Braids' analog oscillator has no bend or tilt, so only the MORPH and
//     MACRO detents reproduce it.

#ifndef PLAITS_DSP_ENGINE2_CSAW_ENGINE_H_
#define PLAITS_DSP_ENGINE2_CSAW_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' `pw = static_cast<uint32_t>(parameter_) * 49152` over a 15-bit
// parameter tops out at 0.375 of a cycle, not 1.0.
const float kCSawMaxPulseWidth = 0.375f;

// `discontinuity_depth_ = -2048 + (aux_parameter_ >> 2)` on Braids' 16384
// scale: -0.125 at the bottom of the knob, +0.5 at the top.
const float kCSawDepthOffset = -0.125f;
const float kCSawDepthRange = 0.5f;

// MacroOscillator::RenderCSaw adds `shift = -(parameter_[1] - 32767) >> 4`
// BEFORE the 13/8 make-up, so the DC term is 1.625 * 2047 / 32768 at the
// bottom of HARMONICS -- not 2047/16384 * 0.5, which is where a re-derivation
// lands 4.2 dB low and makes the HARMONICS sweep pump.
const float kCSawDcShift = 2047.0f / 32768.0f;
const float kCSawMakeUp = 1.625f;

class CSawEngine : public Engine {
 public:
  CSawEngine() { }
  ~CSawEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: OUT and AUX share one phase and differ only in notch depth,
  // so they are decorrelated at matched gain with no second render path.
  virtual bool stereo_capable() const { return true; }

 private:
  float phase_;
  float frequency_;
  float pw_;
  float bend_;
  float tilt_;
  float previous_pw_;
  bool high_;

  // Braids latches the notch depth only inside the wrap branch, so the
  // plateau level is constant within a cycle by construction. Updating it at
  // block rate mid-plateau would insert a step no BLEP corrects.
  float depth_;
  float depth_aux_;

  float next_sample_;
  float next_sample_aux_;

  DISALLOW_COPY_AND_ASSIGN(CSawEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_CSAW_ENGINE_H_
