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
#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

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
const int kWaveStride = kWaveTableSize + 4;
const float kIntegratedScale = 1.0f / 1024.0f;

// The source-level WTCH/WTx6 endpoint averaged about 8 dB below the square in
// the listening prototype. This makeup was A/B approved; the ordinary Plaits
// output limiter remains the final guard for the line's highest-crest slots.
const float kWavetableLevelMakeup = 2.5f;

struct WaveTap {
  const int16_t* low;
  const int16_t* high;
  float crossfade;
  float gain_low;
  float gain_high;
};

inline const int16_t* WaveAt(int slot) {
  return wav_integrated_waves + size_t(kWaveIndex[slot]) * kWaveStride;
}

inline void ResolveWaveTap(float scan, WaveTap* tap) {
  CONSTRAIN(scan, 0.0f, 1.0f);
  const float position = scan * 31.999f;
  const int slot = static_cast<int>(position);
  tap->low = WaveAt(slot);
  tap->high = WaveAt(slot + 1);
  tap->crossfade = position - static_cast<float>(slot);
  tap->gain_low = static_cast<float>(kWaveGain[slot]) / 128.0f;
  tap->gain_high = static_cast<float>(kWaveGain[slot + 1]) / 128.0f;
}

// wav_integrated_waves stores a scaled running sum. Difference first, then
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
  const float low =
      (low_0 + (low_1 - low_0) * p_fractional) * tap.gain_low;
  const float high =
      (high_0 + (high_1 - high_0) * p_fractional) * tap.gain_high;
  return (low + (high - low) * tap.crossfade) *
      kIntegratedScale * kWavetableLevelMakeup;
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
  CONSTRAIN(num_voices, 1, kScaleVoicesMaxVoices);

  float frequency[kScaleVoicesMaxVoices];
  bool audible[kScaleVoicesMaxVoices];
  for (int v = 0; v < num_voices; ++v) {
    const float sign = (v & 1) ? 1.0f : -1.0f;
    const float detune = v == 0 ? 0.0f : sign * detune_cents * 0.01f;
    frequency[v] = NoteToFrequency(notes[v] + detune);
    audible[v] = frequency[v] <= kScaleVoicesMaxVoiceFrequency;
  }

  WaveTap wave_tap;
  ResolveWaveTap(scan, &wave_tap);
  const float mix = 1.0f / static_cast<float>(max(num_voices, 1));
  for (size_t i = 0; i < size; ++i) {
    float mixed = 0.0f;
    float root = 0.0f;
    for (int v = 0; v < num_voices; ++v) {
      if (!audible[v]) {
        continue;
      }
      phase_[v] += frequency[v];
      if (phase_[v] >= 1.0f) {
        phase_[v] -= 1.0f;
      }
      const float sample = ReadWave(wave_tap, phase_[v]);
      mixed += sample * mix;
      if (v == 0) {
        root = sample;
      }
    }
    // Differencing the integrated table reconstructs a zero-mean cycle, so
    // the folded-wave DC blocker is unnecessary on this CPU-critical path.
    out[i] = mixed;
    aux[i] = root;
  }
}

}  // namespace plaits
