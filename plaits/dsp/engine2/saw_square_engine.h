// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SAW_SQUARE: a variable saw and a variable square, driven from the
// same TIMBRE knob and crossfaded by COLOR.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderSawSquare driving
// two AnalogOscillator instances (OSC_SHAPE_VARIABLE_SAW, OSC_SHAPE_SQUARE).
//
// PARAMETER MAPPING, with line numbers, because misreading which suffix means
// which parameter has already cost one engine in this port a rewrite (see
// fold_engine.h's own note on the same trap).
//
//   macro_oscillator.cc:131-132  analog_oscillator_[0].set_parameter(parameter_[0]);
//                                 analog_oscillator_[1].set_parameter(parameter_[0]);
//     BOTH oscillators take parameter_[0] -- TIMBRE -- as their shape
//     parameter. There is no separate shape knob per oscillator; one TIMBRE
//     knob drives both, in OPPOSITE directions (see below).
//   macro_oscillator.cc:133-134  set_pitch(pitch_) on both -- same note, no
//     detuning between them anywhere in this model.
//   macro_oscillator.cc:136-137  osc[0] shape = VARIABLE_SAW, osc[1] = SQUARE.
//   macro_oscillator.cc:145-153, parameter_interpolation.h:69-81
//     BEGIN_INTERPOLATE_PARAMETER_1 / INTERPOLATE_PARAMETER_1 interpolate
//     previous_parameter_[1] -> parameter_[1] -- that is COLOR, not "the
//     first parameter" the "_1" suffix suggests. `balance = parameter_1 << 1`
//     (:148) is COLOR. `attenuated_square = square * 148 >> 8` (:149-150) is
//     a FIXED 148/256 = 0.578125 attenuation Braids never exposes to a knob.
//     `Mix(saw, attenuated_square, balance)` (:151): balance 0 is pure saw,
//     balance max is (almost exactly) pure attenuated square.
//   analog_oscillator.cc:337-422 RenderVariableSaw: parameter_ (TIMBRE) sets
//     `pw = parameter_ << 16` (:354), i.e. pw_norm = parameter_ / 65536,
//     clamped `parameter_ >= 1024` (:344-346) -> pw_norm >= 1024/65536.
//     TIMBRE = 0 -> narrowest pw (~0.0156); TIMBRE = 1 -> pw ~ 0.5.
//   analog_oscillator.cc:188-272 RenderSquare: parameter_ (TIMBRE) clamped
//     `<= 32000` (:194-196), `pw = (32768 - parameter_) << 16` (:206) ->
//     pw_norm = (32768 - parameter_) / 65536. TIMBRE = 0 -> pw ~ 0.5 (50%
//     duty); TIMBRE = 1 -> pw ~ 0.0117 (thin low notch, mostly high).
//
// So TIMBRE is a SINGLE shared "shape" control that opens the saw's pw as it
// closes the square's, and COLOR crossfades which of the two dominates the
// output. Verified against the module by deriving the exact naive (pre-BLEP)
// waveform from the same accumulation terms Braids sums into next_sample_ --
// algebra independent of Braids' internal fixed-point BLEP scaling, so it
// does not depend on reproducing that bit-for-bit (R8) -- and cross-checking
// the closed form against a direct re-evaluation of those terms: the variable
// saw is TWO ramps of slope 2 (in cycles/sample terms) each punctuated by a
// unit downward jump, at TIMBRE = 1 (pw = 0.5) audibly doubling to what reads
// as an octave-up sawtooth; the square is the plain two-level pulse the name
// implies. See tests/ab.json for the measured comparison.
//
// LINEAGE. Plaits' neighbour is Virtual Analog: two of Plaits' own variable
// oscillators, mixed by MACRO. The two are not the same instrument. Virtual
// Analog's compiled variant (VA_VARIANT 2) gives TIMBRE and MORPH independent
// pulse widths on an uncoupled square and saw, adds hard sync, a HARMONICS
// detune, and a tent-shaped saw_pw curve unrelated to Braids' linear one --
// it is a bigger, more elaborate duo-oscillator with no single-knob "morph"
// gesture. SAW_SQUARE's whole character is the coupling: ONE TIMBRE knob
// morphing a complementary pulse-width pair (open the saw as the square
// narrows, or the reverse), which VA's independent TIMBRE/MORPH controls do
// not reproduce, plus the specific 0.578125 fixed balance constant, which VA
// has no equivalent of at all.
//
// RATE. MacroOscillator::RenderSawSquare has no `size -= 2` (macro_oscillator.cc:146,
// a plain `while (size--)`), and neither RenderVariableSaw nor RenderSquare
// oversamples internally -- each advances `phase_ += phase_increment` exactly
// ONCE per output sample (analog_oscillator.cc:376, :227), unlike
// RenderTriangleFold/RenderSineFold/RenderTriangle/RenderSine, which split
// the increment in two and run at an internal 2x. A polyBLEP correction is
// expressed as a fraction of ONE sample period (t = crossing_offset /
// phase_increment), which is rate-relative by construction -- it needs no
// oversampling and no rate-derived constant to carry from 96 kHz Braids
// hardware to a 48 kHz port. (Contrast fold_engine.h's R5, which DOES need a
// rate conversion because its source function oversamples.) `csaw_engine.cc`
// already ported a sibling of this same non-oversampled AnalogOscillator
// family the same way.
//
// Two controls Braids does not have, both stock at their model-matching value:
//   MORPH sets a static phase offset between the saw and the square, held
//   constant except when MORPH itself moves (applied once per block, not
//   smoothed intra-block -- a deliberate small step on a knob move, not a
//   per-sample slew). At MORPH's centre the offset is zero and the pair is
//   phase-locked exactly as Braids always renders it -- the two AnalogOscillator
//   instances share the same phase_increment_ stream and never diverge, so
//   Braids has no way to reach any other relative phase. Off centre, the
//   saw's and square's discontinuities land at different points of the cycle,
//   which detuning (Virtual Analog's HARMONICS) does not produce.
//   MACRO reveals the 148/256 constant as a range: attenuation from silence
//   (0.0) to unattenuated (1.0), stock at 0.578125. Braids hand-picked that
//   ratio and never let the player move it.
//
// OUT: the Braids crossfade -- saw*(1-COLOR) + attenuated_square*COLOR. AUX
// (mono): the complementary crossfade, always favouring whichever oscillator
// OUT is not -- same convention as fold_engine.h. In stereo OUT/AUX become
// L/R and the two sides take a small opposing offset on the crossfade weight,
// again matching fold's stereo pattern.
//
// No table is embedded (R4 in the phase-2 guide): both oscillators are
// polyBLEP, not lookup-driven.
//
// Declared deviations from Braids:
//   - pw is additionally clamped to [2*frequency, 1 - 2*frequency] once the
//     note is high enough that Braids' own fixed floor (1024/65536 for the
//     saw, 768/65536 for the square) would otherwise let the BLEP crossing
//     time run outside one sample. Braids has no such guard at all -- its
//     int BLEP divides by `phase_increment >> 16` and simply produces a
//     bad `t` up there -- and at 96 kHz the crossing has twice the room, so
//     the clamp bites here at notes Braids renders unclamped. It is
//     MEASURABLE, not cosmetic: at note 84 with TIMBRE 0.95 the square's pw
//     goes 0.025 (Braids) -> 0.0437 (2*frequency), worth +2.4 dB AC RMS and
//     3.7 dB spectrum against the module (tests/ab.json `clamp-square`).
//     Matches the guard built into VariableSawOscillator /
//     VariableShapeOscillator, which every Plaits oscillator relies on.
//   - Braids' shape-change re-Init is not reproduced. AnalogOscillator::Render
//     calls Init() when shape_ != previous_shape_ (analog_oscillator.cc:75-78),
//     and Init() resets pitch_ to 60 << 7 (analog_oscillator.h:77) AFTER
//     RenderSawSquare has already called set_pitch -- so the module's very
//     first 24-sample block renders at MIDI 60 rather than the played note,
//     and the phase error that leaves persists for the life of the note:
//     24 * (f60 - fnote) / 96000 cycles, small near MIDI 60 and larger in
//     either direction away from it. This port renders the played note from
//     sample 0. It costs nothing measurable HERE because in this model the
//     quirk is common-mode: macro_oscillator.cc:136-137 changes BOTH
//     oscillators' shapes on the same first Render, so both re-Init in the
//     same block and the pair stays in exact relative lock -- verified by
//     cross-correlation, zero samples of relative offset between the
//     reference's saw-alone and square-alone renders (tests/ab.json). An
//     absolute phase offset shared by both oscillators moves neither AC RMS
//     nor the magnitude spectrum.
//   - the two oscillators are float polyBLEP rather than Braids' int16
//     fixed-point BLEP (R8): voiced reproduction, not bit-identical.
//   - AUX did not exist in Braids' SAW_SQUARE (mono-only algorithm); it is
//     manufactured as the complementary crossfade.

#ifndef PLAITS_DSP_ENGINE2_SAW_SQUARE_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SAW_SQUARE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// analog_oscillator.cc:344-346 `parameter_ < 1024` floor, expressed as a
// fraction of the 32-bit phase register (parameter_ << 16 / 2^32).
const float kSawSquareSawPwMin = 1024.0f / 65536.0f;

// analog_oscillator.cc:194-196 `parameter_ > 32000` ceiling folds into the
// square's `pw = (32768 - parameter_) << 16` (:206) as a floor of its own:
// 32768 - 32000 = 768.
const float kSawSquareSquarePwMin = 768.0f / 65536.0f;

// macro_oscillator.cc:149-150 `square * 148 >> 8`.
const float kSawSquareAttenuationStock = 148.0f / 256.0f;

// MORPH's phase offset spans a full cycle, morph 0 -> -0.5, morph 1 -> +0.5;
// -0.5 and +0.5 land on the same relative phase (mod 1), so this covers every
// distinguishable relative position exactly once.
const float kSawSquareMaxPhaseOffset = 1.0f;

// The two channels take opposing crossfade offsets in stereo, matching
// fold_engine.h's kFoldStereoBlend: small, because the crossfade is the
// model's own gesture and a wide split would turn the pair into two
// different-sounding instruments rather than a stereo image of one.
const float kSawSquareStereoBlend = 0.12f;

class SawSquareEngine : public Engine {
 public:
  SawSquareEngine() { }
  ~SawSquareEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_SAW_SQUARE; }

 private:
  // The saw. Its own hand-rolled polyBLEP state (Braids' RenderVariableSaw is
  // not the same waveform as Plaits' own VariableSawOscillator -- see the
  // header comment above -- so it is not reused here).
  float phase_;
  float frequency_;
  float pw_saw_;
  float previous_pw_saw_;
  bool high_saw_;
  float next_sample_saw_;

  // The square. Same technique, independent phase so MORPH's offset can
  // separate the two.
  float phase_square_;
  float pw_square_;
  float previous_pw_square_;
  bool high_square_;
  float next_sample_square_;

  // The last applied MORPH phase offset, so a block-to-block change can be
  // applied to phase_square_ as a one-time nudge (see the header comment).
  float offset_;

  float atten_;

  // The COLOR crossfade, interpolated across the block the way Braids
  // interpolates parameter_[1] (macro_oscillator.cc:145-153).
  float blend_;

  DISALLOW_COPY_AND_ASSIGN(SawSquareEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SAW_SQUARE_ENGINE_H_
