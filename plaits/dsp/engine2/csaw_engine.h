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
// OUT: the notched saw.
//
// AUX (mono): a variable-width PULSE -- -1 across the notch plateau, +1 across
// the saw segment -- BLEP'd off the same two transitions OUT already computes,
// with its duty-dependent mean removed at sample rate. A saw against a PWM
// square is the oldest dual-VCO pairing there is, and TIMBRE sweeps the duty
// for free because the plateau width IS the pulse width. Being piecewise
// constant it needs value BLEP only, so it is cheaper than the waveform it
// replaced.
//
// In stereo OUT/AUX become L/R and AUX reverts to the same waveform with the
// notch depth taken from the MIRRORED harmonics position -- a deep notch on
// one side pairing with a shallow one on the other -- PLUS a constant bend
// offset on its saw segment. The bend offset is not decoration: the depth
// mirror is its own fixed point at the knob centre, where the two channels
// were bit-identical and the image collapsed to mono, and no depth remapping
// can remove that. See kCSawStereoBend for why the second axis is required and
// why a constant offset removes the collapse everywhere rather than moving it.
//
// Declared deviations from Braids:
//   - the stereo channel MIRRORS the notch depth rather than negating it.
//     Negating puts the plateau at -0.375 and the output at 1.42 before the
//     registered gain, which clips under a positive gain and breaks the
//     matched-gain L/R pair. Mirroring keeps it inside OUT's own bounds.
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

// The mono AUX pulse sits at -1 on the plateau and +1 on the saw segment, so
// every transition is a step of 2.
const float kCSawPulseStep = 2.0f;

// What stops the STEREO pair collapsing. Mirroring the notch depth alone is
// its own fixed point at the knob centre -- `target_depth == target_depth_aux`
// there, and so does the DC term that travels with it, so the two channels
// were bit-identical at HARMONICS noon and the image narrowed to mono.
//
// No remapping of the depth alone can fix that. A continuous f mapping the
// depth range into itself with f(x) != x everywhere would need f(x) > x for
// all x (impossible: f(max) > max) or f(x) < x for all x (impossible at the
// minimum) -- so a fixed point is guaranteed, and any "better" mirror only
// MOVES the collapse rather than removing it.
//
// So the channels are separated on a second axis instead. BendSegment is
// AFFINE in bend -- BendSegment(u, a) - BendSegment(u, b) = (a - b)(u^2 - u) --
// so a constant, non-zero bend difference makes the two saw segments differ at
// every interior point of the segment, at every knob position, with no fixed
// point anywhere. The segment always exists (pw tops out at 0.375), so the two
// channels can never coincide.
//
// The offset deliberately carries the aux bend OUTSIDE the [-1, 1] the knob
// reaches: clamping it back would reintroduce a fixed point at the extreme.
// At the top of MORPH the aux bend hits 1.35, where the segment dips 2.3%
// below its start before rising -- a continuous wiggle, not a new
// discontinuity, and the integrated BLEP stays exact because
// BendSegmentSlope is the true derivative at any bend. OUT is untouched.
const float kCSawStereoBend = 0.35f;

// Mean-removed, a pulse of duty (1 - pw) has an RMS of 2*sqrt(pw*(1-pw)).
// Measured over the parameter grid this lands AUX within 0.1 dB of OUT, so
// aux_gain stays equal to out_gain.
const float kCSawPulseGain = 0.70f;

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
  // Pattern B: mono AUX is a pulse off the same transitions; the stereo branch
  // replaces it with the mirrored-notch waveform so L/R stay a matched pair.
  virtual bool stereo_capable() const { return PLAITS_STEREO_CSAW; }

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
