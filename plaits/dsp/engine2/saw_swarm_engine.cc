// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SAW SWARM: seven detuned naive sawtooths, a soft-clip stage, and a
// note-tracking resonant filter.

#include "plaits/dsp/engine2/saw_swarm_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Crossfades the filter's three simultaneous taps: LP at m=0, Braids' own HP
// at m=0.5 (the module's own operating point), BP at m=1.
inline float SawSwarmFilterMix(float m, float lp, float hp, float bp) {
  if (m < 0.5f) {
    const float mix = m * 2.0f;
    return lp + (hp - lp) * mix;
  } else {
    const float mix = (m - 0.5f) * 2.0f;
    return hp + (bp - hp) * mix;
  }
}

// digital_oscillator.cc:228 reads Braids' `ws_moderate_overdrive` table
// (tanh(2x), braids/resources/waveshapers.py:40). Substituted as the formula
// per SPEC R4 -- see saw_swarm_engine.h for the measured deviation.
inline float SawSwarmShape(float x) {
  return tanhf(2.0f * x) / kSawSwarmShaperPeak;
}

// Runs one ZDF Svf update and returns its three simultaneous taps. HP and BP
// come from one state update (stmlib::Svf has no 3-output Process); LP is
// recovered from the filter's own identity `input = HP + r*BP + LP` rather
// than a second update -- see THE FILTER in the header for the derivation.
inline void SawSwarmFilterTaps(
    Svf* svf, float input, float* lp, float* hp, float* bp) {
  svf->Process<FILTER_MODE_HIGH_PASS, FILTER_MODE_BAND_PASS>(input, hp, bp);
  *lp = input - *hp - svf->r() * (*bp);
}

}  // namespace

void SawSwarmEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  svf_.Init();
  svf_aux_.Init();
  Reset();
}

void SawSwarmEngine::Reset() {
  // digital_oscillator.h:246-257: DigitalOscillator::Init() -- which Render
  // calls on every shape change (digital_oscillator.cc:111-115), i.e. every
  // time SAW SWARM is selected -- zeroes the shared `phase_` member and sets
  // `strike_`, so the FIRST block randomizes state_.saw.phase[0..5]
  // (digital_oscillator.cc:180-185). Braids therefore never runs this model
  // with all seven voices phase-aligned; leaving them at 0 here made a
  // fresh, untriggered drone start on maximum constructive interference and
  // disperse over as long as ~13 s at low detune (rank -3..+3 beat period at
  // TIMBRE 0.05, note 48), which showed up as a level and low-band error in
  // the A/B. Mirrored exactly: rank -3 (index 0) is Braids' zeroed `phase_`,
  // ranks -2..+3 (indices 1..6) are its six randomized state_.saw.phase[].
  phase_[0] = 0.0f;
  frequency_[0] = 0.01f;
  for (int i = 1; i < kNumSawSwarmVoices; ++i) {
    phase_[i] = Random::GetFloat();
    frequency_[i] = 0.01f;
  }
  // A neutral starting cutoff (A4) rather than 0 (near-DC): ramping the
  // very first block's cutoff up from silence would pass the raw,
  // unfiltered swarm through for a few samples -- a startup transient
  // Braids' own reference never shows.
  cutoff_note_ = 69.0f;
  resonance_ = kSawSwarmResonanceStock;
  morph_ = 0.5f;
  svf_.Reset();
  svf_aux_.Reset();
}

void SawSwarmEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // Braids randomizes six of the seven voices' phase on Strike()
    // (digital_oscillator.cc:180-185) and leaves the seventh continuous --
    // an artefact of sharing the base class's `phase_` member across many
    // models (see the header). This port randomizes all seven uniformly.
    for (int i = 0; i < kNumSawSwarmVoices; ++i) {
      phase_[i] = Random::GetFloat();
    }
  }

  const bool stereo = PLAITS_STEREO_SAW_SWARM && parameters.stereo;

  const float f0 = NoteToFrequency(parameters.note);

  // Braids' TIMBRE: detune spread (digital_oscillator.cc:168-179).
  const float timbre = parameters.timbre;
  const float detune_k = 32.0f * timbre + 1.0f;
  const float detune_semitones = detune_k * detune_k * kSawSwarmDetuneScale;

  float target_frequency[kNumSawSwarmVoices];
  for (int i = 0; i < kNumSawSwarmVoices; ++i) {
    const float rank = static_cast<float>(i - 3);
    target_frequency[i] = f0 * SemitonesToRatio(rank * detune_semitones);
  }

  // Braids' COLOR: HP filter cutoff, tracking the note with a steeper slope
  // below the pivot than above it (digital_oscillator.cc:186-196).
  const float color = parameters.harmonics;
  const float cutoff_offset = color <= kSawSwarmColorPivot
      ? (color - kSawSwarmColorPivot) * kSawSwarmColorSlopeBelow
      : (color - kSawSwarmColorPivot) * kSawSwarmColorSlopeAbove;
  float target_cutoff_note = parameters.note + cutoff_offset;
  CONSTRAIN(target_cutoff_note, 0.0f, kSawSwarmCutoffNoteMax);

  const float target_morph = parameters.morph;
  const float target_resonance = ApplyMacro(
      kSawSwarmResonanceStock, kSawSwarmResonanceCalm, kSawSwarmResonancePeak,
      parameters.macro);

  ParameterInterpolator freq_mod[kNumSawSwarmVoices];
  for (int i = 0; i < kNumSawSwarmVoices; ++i) {
    freq_mod[i].Init(&frequency_[i], target_frequency[i], size);
  }
  ParameterInterpolator cutoff_modulation(
      &cutoff_note_, target_cutoff_note, size);
  ParameterInterpolator resonance_modulation(
      &resonance_, target_resonance, size);
  ParameterInterpolator morph_modulation(&morph_, target_morph, size);

  while (size--) {
    const float cutoff_note = cutoff_modulation.Next();
    const float resonance = resonance_modulation.Next();
    const float morph = morph_modulation.Next();

    // Cutoff-to-note mapping re-derived directly (SPEC R5); the topology
    // that carries the rate-dependence is the filter itself -- see THE
    // FILTER in the header.
    float cutoff_hz = 440.0f * SemitonesToRatioSafe(cutoff_note - 69.0f);
    if (cutoff_hz > kSawSwarmCutoffHzMax) {
      cutoff_hz = kSawSwarmCutoffHzMax;
    }
    const float f_norm = cutoff_hz / kSampleRate;
    svf_.set_f_q<FREQUENCY_FAST>(f_norm, resonance);

    if (stereo) {
      float left_sum = 0.0f;
      float right_sum = 0.0f;
      for (int i = 0; i < kNumSawSwarmVoices; ++i) {
        phase_[i] += freq_mod[i].Next();
        if (phase_[i] >= 1.0f) {
          phase_[i] -= 1.0f;
        }
        const float voice = 2.0f * phase_[i] - 1.0f;
        float left_gain, right_gain;
        StereoPanGains(kSawSwarmPan[i], &left_gain, &right_gain);
        left_sum += voice * left_gain;
        right_sum += voice * right_gain;
      }

      const float input_l = SawSwarmShape(left_sum * kSawSwarmSumGain);
      const float input_r = SawSwarmShape(right_sum * kSawSwarmSumGain);

      svf_aux_.set_f_q<FREQUENCY_FAST>(f_norm, resonance);

      float lp_l, hp_l, bp_l;
      SawSwarmFilterTaps(&svf_, input_l, &lp_l, &hp_l, &bp_l);
      float out_l = SawSwarmFilterMix(morph, lp_l, hp_l, bp_l);

      float lp_r, hp_r, bp_r;
      SawSwarmFilterTaps(&svf_aux_, input_r, &lp_r, &hp_r, &bp_r);
      float out_r = SawSwarmFilterMix(morph, lp_r, hp_r, bp_r);

      *out++ = out_l;
      *aux++ = out_r;
    } else {
      float sum = 0.0f;
      for (int i = 0; i < kNumSawSwarmVoices; ++i) {
        phase_[i] += freq_mod[i].Next();
        if (phase_[i] >= 1.0f) {
          phase_[i] -= 1.0f;
        }
        sum += 2.0f * phase_[i] - 1.0f;
      }

      const float input = SawSwarmShape(sum * kSawSwarmSumGain);

      float lp, hp, bp;
      SawSwarmFilterTaps(&svf_, input, &lp, &hp, &bp);

      float out_main = SawSwarmFilterMix(morph, lp, hp, bp);
      float out_comp = SawSwarmFilterMix(1.0f - morph, lp, hp, bp);

      *out++ = out_main;
      *aux++ = out_comp;
    }
  }
}

}  // namespace plaits
