// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' GRANULAR_CLOUD: four windowed sine grains, respawned by a coin flip
// with a random pitch offset each time.

#include "plaits/dsp/engine2/granular_cloud_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' wav_sine is NOT Plaits' lut_sine: it starts at -32512 and crosses
// zero a quarter of the way through, so it is -cos(2*pi*phase) where Plaits'
// Sine() is sin(2*pi*phase). Here the grain phase is free-running and is never
// reset (digital_oscillator.cc:2030 sets the increment and leaves `phase`
// alone), so the quarter-cycle is a constant rotation of the whole signal
// rather than a change of waveform -- but the port carries it anyway, because
// the cost is an add and the alternative is asserting an inaudibility this
// engine has not measured.
const float kBraidsSinePhaseOffset = 0.75f;

// wav_sine is also not unit-amplitude or zero-mean: fitted against the table
// extracted from braids/resources.cc, it is 32638 * -cos(2*pi*i/256) + 127
// (max residual 1 LSB of 32768), a -0.0343 dB level and a +127/32768 DC
// pedestal on every sample. Measured below with and without reproducing it;
// see the declared-deviation note at the end of this file for the verdict.
const float kBraidsSineAmplitude = 32638.0f / 32768.0f;
const float kBraidsSineDc = 127.0f / 32768.0f;

inline float BraidsSine(float phase) {
  phase += kBraidsSinePhaseOffset;
  if (phase >= 1.0f) {
    phase -= 1.0f;
  }
  return Sine(phase) * kBraidsSineAmplitude + kBraidsSineDc;
}

// Braids' lut_granular_envelope is hanning(257) * 32767 followed by 256 zeros
// (braids/resources/lookup_tables.py:120-127), indexed by envelope_phase >> 16
// with no interpolation. `position` here is that index normalized: the window
// runs over [0, 1) and the padded zeros are everything past it.
//
// Substituting Plaits' sine LUT for the table reproduces it to a maximum of
// 3.03e-5, which is 0.99 LSB of 32767, worst at index 10 -- measured against
// the table extracted from braids/resources.cc, not asserted.
//
// `peak` slides where the window crests. At 0.5 the warp is the identity and
// this is Braids' window exactly.
inline float GrainEnvelope(float position, float peak) {
  if (position >= 1.0f) {
    return 0.0f;
  }
  const float warped = position < peak
      ? 0.5f * position / peak
      : 0.5f + 0.5f * (position - peak) / (1.0f - peak);
  return 0.5f - 0.5f * Sine(warped + 0.25f);
}

}  // namespace

// Braids' generator, verbatim from stmlib/utils/random.h: an LCG on a uint32
// with the multiplier and increment of Numerical Recipes' ranqd1.
inline uint32_t GranularCloudEngine::NextWord() {
  rng_state_ = rng_state_ * 1664525L + 1013904223L;
  return rng_state_;
}

void GranularCloudEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  // Seeded HERE and not in Reset(): the host renderer calls Init() then
  // Reset() then renders, so the A/B is reproducible, while a live voice
  // switching engines calls only Reset() and so keeps drawing from wherever
  // the generator had got to -- which is how the module behaves, its generator
  // being global and shared with every other shape.
  rng_state_ = kGranularCloudSeed;
  Reset();
}

void GranularCloudEngine::Reset() {
  // Braids clears the whole grain array in DigitalOscillator::Init()
  // (digital_oscillator.h:247), which leaves every grain with a zero envelope
  // increment -- the second half of the respawn test at line 2024, so all four
  // are eligible on the first block.
  for (int i = 0; i < kGranularCloudNumGrains; ++i) {
    grain_[i].phase = 0.0f;
    grain_[i].phase_increment = 0.0f;
    grain_[i].envelope_phase = 0;
    grain_[i].envelope_phase_increment = 0;
  }
  aux_phase_ = 0.0f;
  peak_position_ = 0.5f;
  scheduler_countdown_ = 0;
}

// digital_oscillator.cc:2020-2040, re-derived for 48 kHz.
void GranularCloudEngine::ScheduleGrains(
    int timbre_code,
    int color_code,
    uint32_t spawn_threshold,
    float base_increment) {
  for (int i = 0; i < kGranularCloudNumGrains; ++i) {
    Grain* g = &grain_[i];
    if (g->envelope_phase <= kGranularCloudEnvelopeEnd &&
        g->envelope_phase_increment != 0) {
      continue;
    }
    g->envelope_phase_increment = 0;
    // One word per test, exactly as Braids spends it, so the two generators
    // stay in step.
    if ((NextWord() & 0xffff) >= spawn_threshold) {
      continue;
    }

    // lut_granular_envelope_rate[timbre >> 7] << 3 at 96 kHz. The table is
    // 2^(i/64) * 2048, reproduced by SemitonesToRatio at 0 LSB across all 257
    // entries; the shift is one greater here because this port runs at half
    // the rate and the endpoint (1<<24) has not moved.
    const int index = timbre_code >> 7;
    const uint32_t rate = static_cast<uint32_t>(
        kGranularCloudRateBase * SemitonesToRatio(
            static_cast<float>(index) * kGranularCloudRateSemitonesPerStep));
    g->envelope_phase_increment = rate << kGranularCloudRateShift;
    g->envelope_phase = 0;
    g->phase_increment = base_increment;

    // pitch_mod = Random::GetSample() * COLOR >> 16, then a linear multiple of
    // the phase increment: >> 8 downward, >> 7 upward. Both shifts are
    // arithmetic, so both floor -- which is why the upward side has a dead
    // zone below pitch_mod 128 and the downward side does not, and why the
    // reach is asymmetric (0.75x against 1.496x). Reproduced with integer
    // shifts rather than a float scale so the quantisation carries over.
    const int32_t sample = static_cast<int32_t>(
        static_cast<int16_t>(NextWord() >> 16));
    const int32_t pitch_mod = (sample * color_code) >> 16;
    const int32_t steps = pitch_mod < 0 ? (pitch_mod >> 8) : (pitch_mod >> 7);
    g->phase_increment += base_increment *
        static_cast<float>(steps) * (1.0f / 256.0f);
    CONSTRAIN(g->phase_increment, 0.0f, 0.5f);
  }
}

void GranularCloudEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  // Braids' Strike() only sets a flag this shape never reads
  // (digital_oscillator.h:284), so a trigger does not restart the cloud here
  // either -- the grains keep running through it.

  const bool stereo = PLAITS_STEREO_GRANULAR_CLOUD && parameters.stereo;

  const float base_increment = NoteToFrequency(parameters.note);

  // Braids' two knobs are 15-bit integers (parameter_[0] and parameter_[1],
  // set from the pots), and both are used through integer shifts, so the port
  // quantizes them the same way the module does before scheduling.
  float timbre = parameters.timbre;
  float color = parameters.harmonics;
  CONSTRAIN(timbre, 0.0f, 1.0f);
  CONSTRAIN(color, 0.0f, 1.0f);
  const int timbre_code = static_cast<int>(timbre * 32767.0f);
  const int color_code = static_cast<int>(color * 32767.0f);

  const float spawn = ApplyMacro(
      kGranularCloudSpawnStock,
      kGranularCloudSpawnMin,
      kGranularCloudSpawnMax,
      parameters.macro);
  const uint32_t spawn_threshold = static_cast<uint32_t>(spawn);

  float morph = parameters.morph;
  CONSTRAIN(morph, 0.0f, 1.0f);
  const float target_peak = kGranularCloudPeakLate +
      (kGranularCloudPeakEarly - kGranularCloudPeakLate) * morph;

  ParameterInterpolator peak_modulation(&peak_position_, target_peak, size);

  float pan_left[kGranularCloudNumGrains];
  float pan_right[kGranularCloudNumGrains];
  if (stereo) {
    for (int i = 0; i < kGranularCloudNumGrains; ++i) {
      const float spread = static_cast<float>(i) /
          static_cast<float>(kGranularCloudNumGrains - 1) - 0.5f;
      StereoPanGains(0.5f + spread * kGranularCloudStereoSpread,
                     &pan_left[i], &pan_right[i]);
    }
  }

  const float inverse_end = 1.0f / static_cast<float>(kGranularCloudEnvelopeEnd);

  while (size--) {
    if (scheduler_countdown_ <= 0) {
      ScheduleGrains(timbre_code, color_code, spawn_threshold, base_increment);
      scheduler_countdown_ = kGranularCloudSchedulerPeriod;
    }
    --scheduler_countdown_;

    const float peak = peak_modulation.Next();

    float mono = 0.0f;
    float left = 0.0f;
    float right = 0.0f;
    float envelope_sum = 0.0f;

    for (int i = 0; i < kGranularCloudNumGrains; ++i) {
      Grain* g = &grain_[i];
      g->phase += g->phase_increment;
      if (g->phase >= 1.0f) {
        g->phase -= 1.0f;
      }
      g->envelope_phase += g->envelope_phase_increment;

      const float envelope = GrainEnvelope(
          static_cast<float>(g->envelope_phase) * inverse_end, peak);
      const float value = BraidsSine(g->phase) * envelope *
          kGranularCloudGrainGain;

      mono += value;
      envelope_sum += envelope * kGranularCloudGrainGain;
      if (stereo) {
        left += value * pan_left[i];
        right += value * pan_right[i];
      }
    }

    // Braids clips the sum into int16 (lines 2066-2071). Four grains at a
    // quarter of full scale each can only just reach it, so this is the
    // module's ceiling rather than a limiter doing work.
    CONSTRAIN(mono, -1.0f, 1.0f);

    aux_phase_ += base_increment;
    if (aux_phase_ >= 1.0f) {
      aux_phase_ -= 1.0f;
    }

    if (stereo) {
      CONSTRAIN(left, -1.0f, 1.0f);
      CONSTRAIN(right, -1.0f, 1.0f);
      *out++ = left;
      *aux++ = right;
    } else {
      *out++ = mono;
      // The cloud's amplitude on one steady tone at the played pitch: the
      // grain rhythm with the scatter removed.
      *aux++ = BraidsSine(aux_phase_) * envelope_sum;
    }
  }
}

}  // namespace plaits
