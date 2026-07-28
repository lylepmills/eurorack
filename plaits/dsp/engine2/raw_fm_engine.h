// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' three FM models -- FM, FBFM and WTFM -- merged into one slot.
//
// The algorithms are Emilie Gillet's RenderFm, RenderFeedbackFm and
// RenderChaoticFeedbackFm. They differ in one line each: plain FM has no
// feedback, FBFM feeds the output back into the modulator's PHASE, and WTFM
// feeds it into the modulator's FREQUENCY. MORPH puts all three on one axis.
//
// WHY THIS IS NOT two-op-fm. The shipped `two-op-fm` already maps HARMONICS
// to the same ratio quantizer, TIMBRE to index and MORPH to a feedback
// topology axis, so three of four knobs overlap and Lyle should A/B before
// this ships. What genuinely differs, and what the copy has to lead on:
// unfiltered full-bandwidth feedback with no one-pole on the path; NO
// oversampling, where two-op-fm runs 4x; a LINEAR index law where two-op-fm
// squares it; WTFM's chaotic frequency feedback, which two-op-fm cannot
// reach at all; the modulator on AUX; and MACRO as feedback depth.
//
// DECLARED, because it is the point rather than an oversight: this engine
// ALIASES MORE THAN two-op-fm. Emilie's own milder engine runs 4x
// oversampling, a squared taming ramp and a 0.05 one-pole on the feedback;
// this runs none of them at 48 kHz. That rawness is the reason to keep it
// next to two-op-fm rather than instead of it. If it should be cleaned up,
// the fix is 4x oversampling with previous_sample updated once per OUTPUT
// sample, which preserves the one-sample loop delay exactly -- but that is a
// different engine from the one Braids has.
//
// The WTFM modulator glide is KEPT, against the spec's instruction to remove
// it. Braids' `129 + (previous >> 9)` centre is 129/256 = 0.504, so its
// modulator does slide an octave across the top half of MORPH -- but that
// octave IS the WTFM sound. Recentring the feedback on 1.0 for continuity
// measured 43 dB away from hardware and moved the perceived fundamental a
// full octave, which would hollow out the one thing this engine has that
// two-op-fm cannot reach. The glide is named in the manual line instead,
// which was the spec's own stated alternative.

#ifndef PLAITS_DSP_ENGINE2_RAW_FM_ENGINE_H_
#define PLAITS_DSP_ENGINE2_RAW_FM_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' plain FM reaches a full cycle of deviation (`<< 2`); both feedback
// variants reach half that (`<< 1`).
const float kRawFmCentreIndex = 1.0f;
const float kRawFmEdgeIndex = 0.5f;

// `previous_sample << 14` into a 32-bit phase: a full-scale int16 shifted
// left 14 is 2^29 of 2^32, so an EIGHTH of a cycle, not a half.
const float kRawFmPhaseFeedback = 0.125f;

// WTFM's frequency feedback, `(increment >> 8) * (129 + (previous >> 9))`,
// spans +-64/256 around its centre.
const float kRawFmFrequencyFeedback = 0.25f;

// Sine() is InterpolateWrap, whose `index -= (int32_t)index` TRUNCATES TOWARD
// ZERO -- so it wraps positive arguments and reads BELOW the table for
// negative ones. Phase modulation reaches -0.25 here (carrier phase 0, plus
// the wav_sine quarter-period offset of 0.75, minus a full cycle of
// deviation), which walks off the front of lut_sine. Every phase handed to
// Sine carries this offset so the argument can never go negative; the extra
// whole cycles cost nothing because InterpolateWrap discards them.
const float kRawFmPhaseOffset = 4.75f;

// Braids' 129/256 modulator centre in the chaotic region.
const float kRawFmWtfmCentre = 129.0f / 256.0f;

class RawFmEngine : public Engine {
 public:
  RawFmEngine() { }
  ~RawFmEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: carrier on OUT, modulator on AUX.
  virtual bool stereo_capable() const { return true; }

 private:
  float phase_;
  float modulator_phase_;
  float previous_sample_;

  DISALLOW_COPY_AND_ASSIGN(RawFmEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_RAW_FM_ENGINE_H_
