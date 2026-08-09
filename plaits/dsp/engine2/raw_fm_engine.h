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
// oversampling and a 0.05 one-pole on the feedback path; this runs neither,
// at 48 kHz. Both engines DO tame the index with a note-72 ramp -- two-op-fm
// squares its version, this one carries Braids' own linear one (see
// kRawFmAttenuatorSlope) -- so the taming is not part of the difference; the
// oversampling and the unfiltered feedback are. That rawness is the reason to
// keep it next to two-op-fm rather than instead of it. If it should be
// cleaned up, the fix is 4x oversampling with previous_sample updated once
// per OUTPUT sample, which preserves the one-sample loop delay exactly -- but
// that is a different engine from the one Braids has.
//
// The residual against hardware is now the missing oversampling, and it has
// TWO faces, not one. Every A/B case is inside tolerance. Most of what is left
// is a top-octave EXCESS -- fbfm-index-attenuator +2.77 dB in 10-20k and
// +18.33 dB in 20.5-24k, on 14.3% and 2.4% of the energy; fbfm-index-max
// +1.31 dB AC RMS, which is the 48 kHz loop's ~24 kHz limit cycle where the
// module's 96 kHz loop puts the same cycle at ~48 kHz and its decimator
// removes it. But fbfm-stock, the third-largest toleranced residual at
// 1.06 dB, is the other face and is NOT above 10 kHz: -0.98 dB at 1.3-2.6k on
// 13.1% of the energy, -2.30 at 2.6-5.1k on 10.3%, -4.80 at 5.1-10k on 6.2%.
// That one is DARKER than the module, because the module's loop makes
// harmonics up to 48 kHz and keeps the ones under 24 kHz through its
// decimator, where this loop never makes them at all. Do not describe the
// remaining error as top-octave aliasing alone.
//
// The WTFM modulator glide is KEPT, against the spec's instruction to remove
// it. Braids' `129 + (previous >> 9)` centre is 129/256 = 0.504, so its
// modulator does slide an octave across the top half of MORPH -- but that
// octave IS the WTFM sound. Recentring the feedback on 1.0 for continuity
// measured 43 dB away from hardware and moved the perceived fundamental a
// full octave, which would hollow out the one thing this engine has that
// two-op-fm cannot reach. The glide is named in the manual line instead,
// which was the spec's own stated alternative. The `>> 9` in that same
// expression is an arithmetic shift on a signed int16, so the feedback is a
// 128-step staircase with a downward bias rather than a continuous ramp; the
// port floors it the same way (kRawFmWtfmFeedbackSteps), which is why a WTFM
// patch's chaotic trajectory is Braids' trajectory and not a smoother
// neighbour of it.
//
// WHAT IS STILL NOT BIT-EXACT, all of it below the measurement floor of the
// A/B and none of it audible:
//   * Braids quantizes pitch_ and its ratio offset to 1/128 semitone before
//     the attenuator ramp reads them; this reads a continuous note, worth at
//     most 512/32768 * 1/128 = 0.012% of the index.
//   * `parameter_0 * attenuation >> 15` floors the attenuated index onto
//     1/32768 steps; this multiplies in float.
//   * WTFM's `modulator_phase_increment >> 8` floors the increment onto
//     multiples of 256 in a 2^32 phase accumulator. That floor is a function
//     of the module's 96 kHz increment, so it has no rate-independent form to
//     port. Its size is 128 / increment, so it GROWS as the modulator goes
//     down: a downward bias of 0.05 cents at note 45 (where every WTFM A/B
//     case sits), 0.2 cents an octave and a half below that, and ~1.2 cents
//     at the bottom of the note range with the ratio knob at -12. Inaudible
//     everywhere, but it is not one number.

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

// WTFM's feedback term is `previous_sample >> 9` on a signed int16 -- an
// arithmetic shift, so a FLOOR onto 1/256 steps of the multiplier, running
// -64..+63 around the centre. previous_sample * 32768 >> 9 is floor of
// previous_sample * 64 once the sample is a float in [-1, 1].
const float kRawFmWtfmFeedbackSteps = 64.0f;

// FBFM's index attenuator (digital_oscillator.cc:756-759, applied at :777).
// `32767 - 4 * (pitch_ - (72 << 7) + ratio_offset)` with pitch_ in 1/128
// semitone, then `>> 15`: unity at or below note 72 with a ratio at or under
// 1:1, falling 512/32768 = 1.5625% of the index per semitone of MODULATOR
// pitch above the pivot, and clipped to zero 64 semitones up. The clip is at
// 32767/32768, not 1.0, because Braids divides by 32768 but clips at 32767.
const float kRawFmAttenuatorPivotNote = 72.0f;
const float kRawFmAttenuatorSlope = 512.0f;
const float kRawFmAttenuatorFull = 32767.0f / 32768.0f;

// ComputePhaseIncrement pins its argument at kPitchTableStart - 1 =
// (128 << 7) - 1 (digital_oscillator.cc:51-54), and all three shapes feed it
// (12 << 7) + pitch_ + ratio_offset then halve the result. So the modulator
// saturates at 128 - 1/128 - 12 semitones, ~6.6 kHz, and the top of the ratio
// axis is a plateau above carrier note 116 rather than a ramp.
const float kRawFmModulatorNoteCeiling = 128.0f - 1.0f / 128.0f - 12.0f;

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
  virtual bool linear_tzfm_capable() const { return true; }
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
