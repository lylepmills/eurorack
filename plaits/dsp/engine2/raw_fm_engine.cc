// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' three FM models -- FM, FBFM and WTFM -- merged into one slot.

#include "plaits/dsp/engine2/raw_fm_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void RawFmEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void RawFmEngine::Reset() {
  phase_ = 0.0f;
  modulator_phase_ = 0.0f;
  previous_sample_ = 0.0f;
}

void RawFmEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
    modulator_phase_ = 0.0f;
    previous_sample_ = 0.0f;
  }

  const float frequency = NoteToFrequency(parameters.note);

  // HARMONICS steps the ratio through Braids' own quantizer. Note that the
  // plateau POSITIONS differ from Braids': its table spans -36..+36 semitones
  // in 25 steps where Plaits' spans -12..+36 in 23, so every ratio sits at a
  // different knob angle even though the set of ratios is nearly the same.
  const float ratio = Interpolate(
      lut_fm_frequency_quantizer, parameters.harmonics, 128.0f);

  // Braids builds the modulator increment as
  //   ComputePhaseIncrement((12 << 7) + pitch_ + ratio_offset) >> 1
  // (digital_oscillator.cc:726, :762, :796) and ComputePhaseIncrement pins its
  // argument at kPitchTableStart - 1 = (128 << 7) - 1 (:51-54). The `>> 1` is
  // an exact octave down, so in note terms the modulator SATURATES at
  // 128 - 1/128 - 12 semitones -- about 6.6 kHz -- however much further the
  // ratio knob turns. Reproduce the ceiling, then keep the Nyquist guard
  // behind it as a safety net for hosts running below 13.3 kHz.
  float modulator_note = parameters.note + ratio;
  if (modulator_note > kRawFmModulatorNoteCeiling) {
    modulator_note = kRawFmModulatorNoteCeiling;
  }
  const float modulator_frequency = min(
      NoteToFrequency(modulator_note), 0.5f);

  // FBFM, and only FBFM, tames its index with a pitch- and ratio-dependent
  // ramp (digital_oscillator.cc:756-759, applied at :777):
  //   attenuation = clip(32767 - 4 * (pitch_ - (72 << 7) + ratio_offset))
  //   p = parameter_0 * attenuation >> 15
  // pitch_ and ratio_offset are in 1/128 semitone, so the slope is
  // 4 * 128 = 512 parts of 32768 -- 1.5625% of the index per semitone of
  // MODULATOR pitch above note 72. Unity below the pivot, zero 64 semitones
  // above it.
  float attenuation = (32767.0f - kRawFmAttenuatorSlope * \
      (parameters.note - kRawFmAttenuatorPivotNote + ratio)) * (1.0f / 32768.0f);
  CONSTRAIN(attenuation, 0.0f, kRawFmAttenuatorFull);

  // MORPH: FBFM below noon, plain FM at noon, WTFM above.
  const float centre = 1.0f - fabsf(2.0f * parameters.morph - 1.0f);
  const float phase_feedback = max(1.0f - 2.0f * parameters.morph, 0.0f);
  const float frequency_feedback = max(2.0f * parameters.morph - 1.0f, 0.0f);

  // MACRO scales how hard the feedback path drives itself. The detent is
  // Braids' own depth.
  const float depth = ApplyMacro(1.0f, 0.0f, 1.5f, parameters.macro);

  // Braids' index law is LINEAR, unlike two-op-fm's squared curve, and plain
  // FM reaches a full cycle where the feedback variants reach half. The
  // attenuator above belongs to FBFM alone, so it rides in on phase_feedback:
  // full at MORPH 0, gone by noon, absent in the chaotic half, exactly as the
  // three Braids models have it.
  const float index = parameters.timbre * \
      (kRawFmEdgeIndex + (kRawFmCentreIndex - kRawFmEdgeIndex) * centre) * \
      (1.0f + (attenuation - 1.0f) * phase_feedback);

  for (size_t i = 0; i < size; ++i) {
    phase_ += frequency;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }

    // WTFM feeds the output into the modulator's FREQUENCY, around Braids'
    // 129/256 centre. The centre ramps in with the feedback so plain FM at
    // noon is unaffected and MORPH 1 is Braids exactly; the octave that
    // arrives with it is the WTFM sound, not an artefact to design out.
    const float centre_ratio = 1.0f + \
        (kRawFmWtfmCentre - 1.0f) * frequency_feedback;
    // Braids reads the feedback as `129 + (previous_sample >> 9)`
    // (digital_oscillator.cc:814-815). `>> 9` on a signed int16 is an
    // arithmetic shift, i.e. a FLOOR, so the multiplier is not continuous: it
    // is a 128-step staircase of 1/256 each, biased downward by up to one
    // step. In float terms previous_sample_ * 32768 >> 9 == floor of
    // previous_sample_ * 64, and int16 tops out at +63 steps.
    float feedback_steps = floorf(previous_sample_ * kRawFmWtfmFeedbackSteps);
    CONSTRAIN(feedback_steps, -kRawFmWtfmFeedbackSteps,
        kRawFmWtfmFeedbackSteps - 1.0f);
    float increment = modulator_frequency * (centre_ratio + \
        kRawFmFrequencyFeedback * frequency_feedback * depth * \
        feedback_steps * (1.0f / kRawFmWtfmFeedbackSteps));
    // Braids accumulates this into a uint32, which wraps for free at any
    // magnitude. A float phase with a single-subtract wrap does not: once the
    // chaotic branch drives the increment past 1.0 the phase runs away, and
    // Sine's int32 truncation then reads far outside lut_sine. Clamping the
    // increment to a real frequency keeps one subtract sufficient.
    CONSTRAIN(increment, 0.0f, 0.5f);
    modulator_phase_ += increment;
    if (modulator_phase_ >= 1.0f) {
      modulator_phase_ -= 1.0f;
    }

    // FBFM feeds it into the modulator's PHASE instead.
    const float modulator = Sine(modulator_phase_ + kRawFmPhaseOffset + \
        kRawFmPhaseFeedback * phase_feedback * depth * previous_sample_);

    previous_sample_ = Sine(phase_ + kRawFmPhaseOffset + index * modulator);
    out[i] = previous_sample_;
    aux[i] = modulator;
  }
}

}  // namespace plaits
