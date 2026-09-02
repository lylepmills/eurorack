// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

// Shared wavetable oscillator path for WTCH and WTx6. This lives outside
// scale_voices.cc so the classical engines do not retain the table reader or
// Plaits' integrated wave bank.

#include "plaits/dsp/engine2/scale_voices.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/integrated_wavetable.h"
#include "plaits/resources.h"

#ifndef PLAITS_SHARED_WAVE_SCALE_AUTOSWEEP
#define PLAITS_SHARED_WAVE_SCALE_AUTOSWEEP 0
#endif

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

#if PLAITS_SHARED_WAVE_SCALE_AUTOSWEEP
const uint32_t kScaleWavetableAutosweepLeaderSamples = 6u * 48000u;
const uint32_t kScaleWavetableAutosweepGapSamples = 3u * 24000u;
const uint32_t kScaleWavetableAutosweepWindowSamples = 8u * 48000u;
const uint32_t kScaleWavetableAutosweepSlotSamples =
    kScaleWavetableAutosweepGapSamples +
    kScaleWavetableAutosweepWindowSamples;
const uint32_t kScaleWavetableAutosweepProfiles = 4u;
const uint32_t kScaleWavetableAutosweepTrailerSamples = 6u * 48000u;
const uint32_t kScaleWavetableAutosweepCycleSamples =
    kScaleWavetableAutosweepLeaderSamples +
    kScaleWavetableAutosweepProfiles *
        kScaleWavetableAutosweepSlotSamples +
    kScaleWavetableAutosweepTrailerSamples;

uint32_t scale_wavetable_autosweep_samples = 0;
#endif

// Renaissance's WTCH and WTx6 models scan Braids' 33-entry mini_wave_line.
// These are the measured counterparts already used by Wave Paraphonic: 16
// slots are the same source wave in Plaits' bank and the other 17 are the
// closest spectral matches. Both projects generated their source banks from
// the same byte-identical waves.bin, so this keeps the scan's character without
// retaining Braids' separate 33,024-byte wt_waves table.
const uint8_t kWaveIndex[33] = {
  182,  41,  57, 182, 145, 146, 122, 147,  57, 117, 151,
  167, 166, 165, 163, 162,  96, 129,  90, 130, 170, 132,
  134, 136, 138, 141, 140,  60,  62, 173, 121, 147, 178
};

// Plaits peak-normalises its generated tables. Restore each Braids wave's
// relative RMS before crossfading, in 1/128 steps, as Wave Paraphonic does.
const uint8_t kWaveGain[33] = {
  121, 121, 130, 166, 118, 120, 100, 125, 147, 137, 218,
  121, 111, 127, 124, 121, 104, 103,  98, 104, 122, 141,
  126, 108, 112, 119, 106, 122, 125, 120,  94, 151, 127
};

const int kWaveTableSize = 128;
const float kIntegratedScale = 1.0f / 1024.0f;

// The source-level WTCH/WTx6 endpoint averaged about 8 dB below the square in
// the listening prototype. This makeup was A/B approved; the ordinary Plaits
// output limiter remains the final guard for the line's highest-crest slots.
const float kWavetableLevelMakeup = 2.5f;

struct WaveTap {
  const int16_t* low;
  const int16_t* high;
  float low_weight;
  float high_weight;
};

inline const int16_t* WaveAt(int slot) {
#if PLAITS_HAS_CUSTOM_WAVE_LINES
  return kBraidsWaveLine[slot];
#else
  return FactoryIntegratedWavetable(kWaveIndex[slot]);
#endif
}

inline float WaveGainAt(int slot) {
#if PLAITS_HAS_CUSTOM_WAVE_LINES
  return static_cast<float>(kBraidsWaveLineGains[slot]);
#else
  return static_cast<float>(kWaveGain[slot]);
#endif
}

inline void ResolveWaveTap(float scan, WaveTap* tap) {
  CONSTRAIN(scan, 0.0f, 1.0f);
  const float position = scan * 31.999f;
  const int slot = static_cast<int>(position);
  tap->low = WaveAt(slot);
  tap->high = WaveAt(slot + 1);
  const float crossfade = position - static_cast<float>(slot);
  const float scale = kIntegratedScale * kWavetableLevelMakeup / 128.0f;
  tap->low_weight = WaveGainAt(slot) *
      (1.0f - crossfade) * scale;
  tap->high_weight = WaveGainAt(slot + 1) *
      crossfade * scale;
}

// The factory/generated integrated waves store a scaled running sum. Difference first, then
// interpolate the reconstructed samples: this matches Braids' linear table
// read and avoids the zero-order-hold images produced by interpolating the
// integral before differentiating it.
inline float ReadWave(const WaveTap& tap, float phase) {
  const float p = phase * static_cast<float>(kWaveTableSize);
  MAKE_INTEGRAL_FRACTIONAL(p);
  const float low_0 = static_cast<float>(
      tap.low[p_integral + 1] - tap.low[p_integral]);
  const float low_1 = static_cast<float>(
      tap.low[p_integral + 2] - tap.low[p_integral + 1]);
  const float high_0 = static_cast<float>(
      tap.high[p_integral + 1] - tap.high[p_integral]);
  const float high_1 = static_cast<float>(
      tap.high[p_integral + 2] - tap.high[p_integral + 1]);
  // Crossfading and applying the two per-wave gains are linear operations.
  // Fold their block-constant coefficients together first, then perform the
  // phase interpolation once on the already-mixed endpoints. This is
  // algebraically the same readout with one fewer multiply and a shorter
  // dependent arithmetic chain in the six-voice inner loop.
  const float sample_0 =
      low_0 * tap.low_weight + high_0 * tap.high_weight;
  const float sample_1 =
      low_1 * tap.low_weight + high_1 * tap.high_weight;
  return sample_0 + (sample_1 - sample_0) * p_fractional;
}

}  // namespace

void ScaleVoiceBank::RenderWavetable(
    const float* notes,
    int num_voices,
    float scan,
    float detune_cents,
    float* out,
    float* aux,
    size_t size) {
#if PLAITS_SHARED_WAVE_SCALE_AUTOSWEEP
  const size_t block_size = size;
  const uint32_t cycle_position =
      scale_wavetable_autosweep_samples %
      kScaleWavetableAutosweepCycleSamples;
  const uint32_t active_start = kScaleWavetableAutosweepLeaderSamples;
  const uint32_t active_end = active_start +
      kScaleWavetableAutosweepProfiles *
          kScaleWavetableAutosweepSlotSamples;
  bool autosweep_silent = true;
  float autosweep_notes[kScaleVoicesMaxVoices];
  if (cycle_position >= active_start && cycle_position < active_end) {
    const uint32_t relative = cycle_position - active_start;
    const uint32_t profile = relative / kScaleWavetableAutosweepSlotSamples;
    const uint32_t within_profile =
        relative % kScaleWavetableAutosweepSlotSamples;
    const uint32_t rendered =
        within_profile > kScaleWavetableAutosweepGapSamples
            ? within_profile - kScaleWavetableAutosweepGapSamples
            : 0u;
    float progress = static_cast<float>(rendered) /
        static_cast<float>(kScaleWavetableAutosweepWindowSamples);
    CONSTRAIN(progress, 0.0f, 1.0f);
    const float roots[kScaleWavetableAutosweepProfiles] = {
      36.0f, 42.0f, 48.0f, 54.0f
    };
    const float intervals[kScaleVoicesMaxVoices] = {
      0.0f, 4.0f, 7.0f, 10.0f, 14.0f, 17.0f
    };
    for (int voice = 0; voice < kScaleVoicesMaxVoices; ++voice) {
      autosweep_notes[voice] = roots[profile] + intervals[voice];
    }
    notes = autosweep_notes;
    num_voices = 3 + profile;
    scan = progress;
    detune_cents = static_cast<float>(profile) * 2.0f;
    autosweep_silent =
        within_profile < kScaleWavetableAutosweepGapSamples;
  }
#endif

  // This path must be costed at the chord engine's VARIABLE voice count. The
  // original generic sweep never selected its six-voice rows: explicit
  // Cortex-M4 counts were 256.4/320.0/382.7/446.8 instructions per sample for
  // 3/4/5/6 voices. Block-folding the wave coefficients and compacting audible
  // upper voices below reduce those to 221.7/276.6/332.2/387.5 without dropping
  // a voice or changing the interpolation.
  CONSTRAIN(num_voices, 1, kScaleVoicesMaxVoices);

  float frequency[kScaleVoicesMaxVoices];
  int audible_upper[kScaleVoicesMaxVoices - 1];
  int num_audible_upper = 0;
  for (int v = 0; v < num_voices; ++v) {
    const float sign = (v & 1) ? 1.0f : -1.0f;
    const float detune = v == 0 ? 0.0f : sign * detune_cents * 0.01f;
    frequency[v] = NoteToFrequency(notes[v] + detune);
    if (v > 0 && frequency[v] <= kScaleVoicesMaxVoiceFrequency) {
      audible_upper[num_audible_upper++] = v;
    }
  }
  const bool root_audible = frequency[0] <= kScaleVoicesMaxVoiceFrequency;

  WaveTap wave_tap;
  ResolveWaveTap(scan, &wave_tap);
  const float mix = 1.0f / static_cast<float>(max(num_voices, 1));
  for (size_t i = 0; i < size; ++i) {
    float mixed = 0.0f;
    float root = 0.0f;
    if (root_audible) {
      phase_[0] += frequency[0];
      if (phase_[0] >= 1.0f) {
        phase_[0] -= 1.0f;
      }
      root = ReadWave(wave_tap, phase_[0]);
      mixed = root * mix;
    }
    for (int a = 0; a < num_audible_upper; ++a) {
      const int v = audible_upper[a];
      phase_[v] += frequency[v];
      if (phase_[v] >= 1.0f) {
        phase_[v] -= 1.0f;
      }
      mixed += ReadWave(wave_tap, phase_[v]) * mix;
    }
    // Differencing the integrated table reconstructs a zero-mean cycle, so
    // the folded-wave DC blocker is unnecessary on this CPU-critical path.
    out[i] = mixed;
    aux[i] = root;
  }
#if PLAITS_SHARED_WAVE_SCALE_AUTOSWEEP
  if (autosweep_silent) {
    fill(&out[0], &out[block_size], 0.0f);
    fill(&aux[0], &aux[block_size], 0.0f);
  }
  scale_wavetable_autosweep_samples =
      (scale_wavetable_autosweep_samples + block_size) %
      kScaleWavetableAutosweepCycleSamples;
#endif
}

void ScaleVoiceBank::RenderWavetableFrequencyOffset(
    const float* notes,
    int num_voices,
    float scan,
    float detune_cents,
    const float* root_frequency_offset,
    float* out,
    float* aux,
    size_t size) {
  CONSTRAIN(num_voices, 1, kScaleVoicesMaxVoices);

  const float root_frequency = NoteToFrequency(notes[0]);
  float frequency[kScaleVoicesMaxVoices];
  float frequency_ratio[kScaleVoicesMaxVoices];
  for (int v = 0; v < num_voices; ++v) {
    const float sign = (v & 1) ? 1.0f : -1.0f;
    const float detune = v == 0 ? 0.0f : sign * detune_cents * 0.01f;
    frequency[v] = NoteToFrequency(notes[v] + detune);
    frequency_ratio[v] = frequency[v] / root_frequency;
  }

  WaveTap wave_tap;
  ResolveWaveTap(scan, &wave_tap);
  const float mix = 1.0f / static_cast<float>(max(num_voices, 1));
  for (size_t i = 0; i < size; ++i) {
    float mixed = 0.0f;
    float root = 0.0f;
    for (int v = 0; v < num_voices; ++v) {
      float f = frequency[v] +
          root_frequency_offset[i] * frequency_ratio[v];
      if (f > kScaleVoicesMaxVoiceFrequency) {
        continue;
      }
      if (f < 1.0e-7f) {
        f = 1.0e-7f;
      }
      phase_[v] += f;
      if (phase_[v] >= 1.0f) {
        phase_[v] -= 1.0f;
      }
      const float sample = ReadWave(wave_tap, phase_[v]);
      mixed += sample * mix;
      if (v == 0) {
        root = sample;
      }
    }
    out[i] = mixed;
    aux[i] = root;
  }
}

}  // namespace plaits
