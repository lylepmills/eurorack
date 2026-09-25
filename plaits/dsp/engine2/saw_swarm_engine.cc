// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SAW SWARM: seven detuned naive sawtooths, a soft-clip stage, and a
// note-tracking resonant filter.

#include "plaits/dsp/engine2/saw_swarm_engine.h"
#include "plaits/build_config.h"

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
// tanh(2x) / tanh(2) over x in [-1, 1], 257 points -- Braids' own shaper is a
// 257-entry table of this curve (braids/resources/waveshapers.py), read with
// linear interpolation. Replaces a libm tanhf per sample, which was a fifth
// of this engine's cost (on-module overrun sweep, 2026-09-24); the
// interpolation error is under 3e-5 of full scale.
const float kSawSwarmShaperTable[257] = {
  -1.000000000f, -0.998837472f, -0.997639431f, -0.996404837f, -0.995132622f, -0.993821690f,
  -0.992470915f, -0.991079144f, -0.989645193f, -0.988167846f, -0.986645859f, -0.985077954f,
  -0.983462823f, -0.981799124f, -0.980085482f, -0.978320489f, -0.976502704f, -0.974630649f,
  -0.972702813f, -0.970717649f, -0.968673576f, -0.966568974f, -0.964402188f, -0.962171526f,
  -0.959875261f, -0.957511626f, -0.955078818f, -0.952574996f, -0.949998281f, -0.947346758f,
  -0.944618472f, -0.941811432f, -0.938923608f, -0.935952935f, -0.932897309f, -0.929754589f,
  -0.926522600f, -0.923199130f, -0.919781931f, -0.916268722f, -0.912657187f, -0.908944979f,
  -0.905129718f, -0.901208993f, -0.897180367f, -0.893041370f, -0.888789511f, -0.884422270f,
  -0.879937107f, -0.875331461f, -0.870602750f, -0.865748379f, -0.860765737f, -0.855652201f,
  -0.850405143f, -0.845021926f, -0.839499914f, -0.833836469f, -0.828028960f, -0.822074765f,
  -0.815971273f, -0.809715892f, -0.803306050f, -0.796739201f, -0.790012829f, -0.783124457f,
  -0.776071645f, -0.768852004f, -0.761463193f, -0.753902933f, -0.746169006f, -0.738259266f,
  -0.730171645f, -0.721904156f, -0.713454905f, -0.704822092f, -0.696004023f, -0.686999116f,
  -0.677805907f, -0.668423056f, -0.658849358f, -0.649083749f, -0.639125311f, -0.628973284f,
  -0.618627068f, -0.608086236f, -0.597350536f, -0.586419901f, -0.575294456f, -0.563974524f,
  -0.552460632f, -0.540753518f, -0.528854137f, -0.516763666f, -0.504483510f, -0.492015306f,
  -0.479360930f, -0.466522495f, -0.453502363f, -0.440303138f, -0.426927677f, -0.413379088f,
  -0.399660730f, -0.385776214f, -0.371729405f, -0.357524417f, -0.343165615f, -0.328657611f,
  -0.314005258f, -0.299213652f, -0.284288121f, -0.269234221f, -0.254057734f, -0.238764653f,
  -0.223361182f, -0.207853720f, -0.192248857f, -0.176553360f, -0.160774166f, -0.144918367f,
  -0.128993199f, -0.113006030f, -0.096964346f, -0.080875737f, -0.064747885f, -0.048588545f,
  -0.032405537f, -0.016206724f,  0.000000000f,  0.016206724f,  0.032405537f,  0.048588545f,
   0.064747885f,  0.080875737f,  0.096964346f,  0.113006030f,  0.128993199f,  0.144918367f,
   0.160774166f,  0.176553360f,  0.192248857f,  0.207853720f,  0.223361182f,  0.238764653f,
   0.254057734f,  0.269234221f,  0.284288121f,  0.299213652f,  0.314005258f,  0.328657611f,
   0.343165615f,  0.357524417f,  0.371729405f,  0.385776214f,  0.399660730f,  0.413379088f,
   0.426927677f,  0.440303138f,  0.453502363f,  0.466522495f,  0.479360930f,  0.492015306f,
   0.504483510f,  0.516763666f,  0.528854137f,  0.540753518f,  0.552460632f,  0.563974524f,
   0.575294456f,  0.586419901f,  0.597350536f,  0.608086236f,  0.618627068f,  0.628973284f,
   0.639125311f,  0.649083749f,  0.658849358f,  0.668423056f,  0.677805907f,  0.686999116f,
   0.696004023f,  0.704822092f,  0.713454905f,  0.721904156f,  0.730171645f,  0.738259266f,
   0.746169006f,  0.753902933f,  0.761463193f,  0.768852004f,  0.776071645f,  0.783124457f,
   0.790012829f,  0.796739201f,  0.803306050f,  0.809715892f,  0.815971273f,  0.822074765f,
   0.828028960f,  0.833836469f,  0.839499914f,  0.845021926f,  0.850405143f,  0.855652201f,
   0.860765737f,  0.865748379f,  0.870602750f,  0.875331461f,  0.879937107f,  0.884422270f,
   0.888789511f,  0.893041370f,  0.897180367f,  0.901208993f,  0.905129718f,  0.908944979f,
   0.912657187f,  0.916268722f,  0.919781931f,  0.923199130f,  0.926522600f,  0.929754589f,
   0.932897309f,  0.935952935f,  0.938923608f,  0.941811432f,  0.944618472f,  0.947346758f,
   0.949998281f,  0.952574996f,  0.955078818f,  0.957511626f,  0.959875261f,  0.962171526f,
   0.964402188f,  0.966568974f,  0.968673576f,  0.970717649f,  0.972702813f,  0.974630649f,
   0.976502704f,  0.978320489f,  0.980085482f,  0.981799124f,  0.983462823f,  0.985077954f,
   0.986645859f,  0.988167846f,  0.989645193f,  0.991079144f,  0.992470915f,  0.993821690f,
   0.995132622f,  0.996404837f,  0.997639431f,  0.998837472f,  1.000000000f
};

inline float SawSwarmShape(float x) {
  CONSTRAIN(x, -1.0f, 1.0f);
  const float position = (x + 1.0f) * 128.0f;
  int integral = static_cast<int>(position);
  if (integral > 255) integral = 255;
  const float fractional = position - static_cast<float>(integral);
  const float a = kSawSwarmShaperTable[integral];
  const float b = kSawSwarmShaperTable[integral + 1];
  return a + (b - a) * fractional;
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
  stereo_allpass_.Init();
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
  cutoff_frequency_ = 440.0f / kSampleRate;
  resonance_ = kSawSwarmResonanceStock;
  morph_ = 0.5f;
  svf_.Reset();
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
    // Clamped here, per block, so the per-sample path needs no float
    // compares when there is no FM offset (the interpolated value between
    // two in-range targets stays in range).
    CONSTRAIN(target_frequency[i], -0.49f, 0.49f);
  }

  // Braids' COLOR: HP filter cutoff, tracking the note with a steeper slope
  // below the pivot than above it (digital_oscillator.cc:186-196).
  const float color = parameters.harmonics;
  const float cutoff_offset = color <= kSawSwarmColorPivot
      ? (color - kSawSwarmColorPivot) * kSawSwarmColorSlopeBelow
      : (color - kSawSwarmColorPivot) * kSawSwarmColorSlopeAbove;
  float target_cutoff_note = parameters.note + cutoff_offset;
  CONSTRAIN(target_cutoff_note, 0.0f, kSawSwarmCutoffNoteMax);
  float target_cutoff_hz = 440.0f * SemitonesToRatioSafe(
      target_cutoff_note - 69.0f);
  if (target_cutoff_hz > kSawSwarmCutoffHzMax) {
    target_cutoff_hz = kSawSwarmCutoffHzMax;
  }
  const float target_cutoff_frequency = target_cutoff_hz / kSampleRate;

  const float target_morph = parameters.morph;
  const float target_resonance = ApplyMacro(
      kSawSwarmResonanceStock, kSawSwarmResonanceCalm, kSawSwarmResonancePeak,
      parameters.macro);

  ParameterInterpolator freq_mod[kNumSawSwarmVoices];
  // Each voice's share of an FM offset: invariant across the block, so it is
  // computed here -- dividing inside the sample loop cost 84 divisions a
  // block (on-module overrun sweep, 2026-09-24).
  float offset_ratio[kNumSawSwarmVoices];
  const float inverse_f0 = 1.0f / (f0 > 1.0e-9f ? f0 : 1.0e-9f);
  for (int i = 0; i < kNumSawSwarmVoices; ++i) {
    freq_mod[i].Init(&frequency_[i], target_frequency[i], size);
    offset_ratio[i] = target_frequency[i] * inverse_f0;
  }
  // The filter coefficients stay per sample: computing them once per block
  // (Braids' own rate) saved only a tenth of the render and moved a COLOR
  // sweep by -49 dB, where keeping them leaves every scenario within one
  // 16-bit step of the previous render.
  ParameterInterpolator cutoff_modulation(
      &cutoff_frequency_, target_cutoff_frequency, size);
  ParameterInterpolator resonance_modulation(
      &resonance_, target_resonance, size);
  ParameterInterpolator morph_modulation(&morph_, target_morph, size);

#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  size_t sample_index = 0;
  const bool has_offset = parameters.frequency_offset != NULL;
#else
  const bool has_offset = false;
#endif
  while (size--) {
    const float f_norm = cutoff_modulation.Next();
    const float resonance = resonance_modulation.Next();
    const float morph = morph_modulation.Next();

    // Cutoff-to-note mapping re-derived directly (SPEC R5); the topology
    // that carries the rate-dependence is the filter itself -- see THE
    // FILTER in the header.
    svf_.set_f_q<FREQUENCY_FAST>(f_norm, resonance);

    float sum = 0.0f;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    const float root_offset = parameters.frequency_offset
        ? parameters.frequency_offset[sample_index]
        : 0.0f;
#else
    const float root_offset = 0.0f;
#endif
    if (has_offset) {
      for (int i = 0; i < kNumSawSwarmVoices; ++i) {
        float frequency = freq_mod[i].Next() + root_offset * offset_ratio[i];
        CONSTRAIN(frequency, -0.49f, 0.49f);
        phase_[i] += frequency;
        if (phase_[i] >= 1.0f) {
          phase_[i] -= 1.0f;
        } else if (phase_[i] < 0.0f) {
          phase_[i] += 1.0f;
        }
        sum += 2.0f * phase_[i] - 1.0f;
      }
    } else {
      // No offset: frequencies are in (0, 0.49], so a phase only ever wraps
      // upward.
      for (int i = 0; i < kNumSawSwarmVoices; ++i) {
        phase_[i] += freq_mod[i].Next();
        if (phase_[i] >= 1.0f) {
          phase_[i] -= 1.0f;
        }
        sum += 2.0f * phase_[i] - 1.0f;
      }
    }

    const float input = SawSwarmShape(sum * kSawSwarmSumGain);

    float lp, hp, bp;
    SawSwarmFilterTaps(&svf_, input, &lp, &hp, &bp);

    float out_main = SawSwarmFilterMix(morph, lp, hp, bp);
    float out_comp = SawSwarmFilterMix(1.0f - morph, lp, hp, bp);

    *out++ = out_main;
    *aux++ = stereo ? stereo_allpass_.Process(out_main) : out_comp;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    ++sample_index;
#endif
  }
}

}  // namespace plaits
