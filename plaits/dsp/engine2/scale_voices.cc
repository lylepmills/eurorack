// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Shared scale-degree machinery for the two Braids Renaissance ports.

#include "plaits/dsp/engine2/scale_voices.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"

#include "plaits/build_config.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
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

// One-sided PolyBLEP. `t` is the phase, `dt` the per-sample increment.
inline float PolyBlep(float t, float dt) {
  if (dt <= 0.0f) {
    return 0.0f;
  }
  if (t < dt) {
    const float x = t / dt;
    return x + x - x * x - 1.0f;
  }
  if (t > 1.0f - dt) {
    const float x = (t - 1.0f) / dt;
    return x * x + x + x + 1.0f;
  }
  return 0.0f;
}

inline float Triangle(float phase) {
  return 2.0f * fabsf(2.0f * phase - 1.0f) - 1.0f;
}

inline float Saw(float phase, float dt) {
  return 2.0f * phase - 1.0f - PolyBlep(phase, dt);
}

inline float Square(float phase, float dt) {
  float other = phase + 0.5f;
  if (other >= 1.0f) {
    other -= 1.0f;
  }
  const float naive = phase < 0.5f ? 1.0f : -1.0f;
  return naive + PolyBlep(phase, dt) - PolyBlep(other, dt);
}

inline float NaiveSquare(float phase) {
  return phase < 0.5f ? 1.0f : -1.0f;
}

// Only the two waveforms bracketing the knob are computed; a five-way
// crossfade would cost more for the same result.
inline float ClassicalWaveform(float phase, float dt, float waveform) {
  const float scaled = waveform * 4.0f;
  // Test from the expensive end first, and return exact anchors without
  // computing their zero-gain neighbour. The five-anchor remap puts saw at
  // MORPH noon, so that short-circuit is a material part of the M4 budget.
  if (scaled >= 3.0f) {
    return Square(phase, dt);
  } else if (scaled >= 2.0f) {
    const float saw = Saw(phase, dt);
    if (scaled == 2.0f) {
      return saw;
    }
    return saw + (Square(phase, dt) - saw) * (scaled - 2.0f);
  } else if (scaled >= 1.0f) {
    const float triangle = Triangle(phase);
    return triangle + (Saw(phase, dt) - triangle) * (scaled - 1.0f);
  } else {
    const float sine = Sine(phase);
    return sine + (Triangle(phase) - sine) * scaled;
  }
}

}  // namespace

#if PLAITS_SCALE_BANK_COUNT < 1 || PLAITS_SCALE_BANK_COUNT > 16
#error "Scale bank must contain between 1 and 16 entries"
#endif

const int kScaleVoicesNumScales = PLAITS_SCALE_BANK_COUNT;
const Scale kScaleVoicesScales[PLAITS_SCALE_BANK_COUNT] = PLAITS_SCALE_BANK;

float ScaleDegreeToNote(int degree, int scale) {
  const Scale& s = kScaleVoicesScales[scale];
  // Floor division, so negative degrees fall an octave down rather than
  // folding back on themselves.
  int octave = degree / s.num_degrees;
  int index = degree - octave * s.num_degrees;
  if (index < 0) {
    index += s.num_degrees;
    --octave;
  }
  const int pitch = octave * kScaleVoicesUnitsPerOctave + s.pitches[index];
  return static_cast<float>(pitch) /
      static_cast<float>(kScaleVoicesUnitsPerSemitone);
}

int QuantizeToScale(float note, int scale, float* residual) {
  const Scale& s = kScaleVoicesScales[scale];
  // The played note is within an octave of one of these; three octaves of
  // candidates covers the boundary either way.
  const int base_octave = static_cast<int>(floorf(note / 12.0f)) - 1;
  int best_degree = base_octave * s.num_degrees;
  float best_distance = 1e30f;
  for (int octave = 0; octave < 3; ++octave) {
    for (int index = 0; index < s.num_degrees; ++index) {
      const int degree = (base_octave + octave) * s.num_degrees + index;
      const float distance = fabsf(note - ScaleDegreeToNote(degree, scale));
      if (distance < best_distance) {
        best_distance = distance;
        best_degree = degree;
      }
    }
  }
  *residual = note - ScaleDegreeToNote(best_degree, scale);
  return best_degree;
}

void ScaleVoiceBank::Init() {
  Reset();
}

void ScaleVoiceBank::Reset() {
  for (int i = 0; i < kScaleVoicesMaxVoices; ++i) {
    // Braids seeded these from its PRNG on a strike. A fixed spread is used
    // instead so a triggered note is repeatable, which is what a Plaits
    // trigger is for; the offsets still keep the voices from summing into one
    // large transient at t = 0.
    phase_[i] = static_cast<float>(i) / static_cast<float>(
        kScaleVoicesMaxVoices);
  }
  dc_in_ = 0.0f;
  dc_out_ = 0.0f;
  dc_aux_in_ = 0.0f;
  dc_aux_out_ = 0.0f;
}

void ScaleVoiceBank::Render(
    const float* notes,
    int num_voices,
    float waveform,
    float scan,
    float detune_cents,
    float fold,
    float* out,
    float* aux,
    size_t size) {
  CONSTRAIN(num_voices, 1, kScaleVoicesMaxVoices);

  float frequency[kScaleVoicesMaxVoices];
  bool audible[kScaleVoicesMaxVoices];
  for (int v = 0; v < num_voices; ++v) {
    // Symmetric detune: the chord's centre of mass does not move, the voices
    // just beat against each other.
    const float sign = (v & 1) ? 1.0f : -1.0f;
    const float detune = v == 0 ? 0.0f : sign * detune_cents * 0.01f;
    frequency[v] = NoteToFrequency(notes[v] + detune);
    audible[v] = frequency[v] <= kScaleVoicesMaxVoiceFrequency;
  }

  // Voices dropped for being out of range do not get their share of the mix
  // handed to the survivors -- a stack whose top voices leave the audible
  // range should thin out, not swell.
  const float mix = 1.0f / static_cast<float>(max(num_voices, 1));
  const float fold_drive = 1.0f + fold * (kScaleVoicesMaxFoldDrive - 1.0f);
  // The fold is a sine-region effect, as it was upstream.
  const float fold_amount = max(1.0f - waveform * 4.0f, 0.0f);
  if (waveform > 0.75f) {
    WaveTap wave_tap;
    ResolveWaveTap(scan, &wave_tap);
    const float wavetable_amount = min((waveform - 0.75f) * 4.0f, 1.0f);
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
        const float wavetable = ReadWave(wave_tap, phase_[v]);
        // The square anchor itself remains PolyBLEP. Inside the final
        // crossfade its contribution is already falling behind a raw
        // 128-sample Renaissance table; using the matching naive edge avoids
        // paying for two BLEP corrections in addition to the table read.
        float sample = wavetable;
        if (wavetable_amount < 1.0f) {
          const float square = NaiveSquare(phase_[v]);
          sample = square + (wavetable - square) * wavetable_amount;
        }
        mixed += sample * mix;
        if (v == 0) {
          root = sample;
        }
      }
      // Every reconstructed Plaits wave and the square transition are exactly
      // zero-mean over a cycle, so the folded-wave DC blocker is unnecessary
      // on this hot path.
      out[i] = mixed;
      aux[i] = root;
    }
    return;
  }

  if (waveform >= 0.25f) {
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
        const float sample = ClassicalWaveform(
            phase_[v], frequency[v], waveform);
        mixed += sample * mix;
        if (v == 0) {
          root = sample;
        }
      }
      // Triangle and both PolyBLEP waveforms are analytically zero-mean and
      // carry no sine fold, so keep the DC blocker off this CPU-critical path.
      out[i] = mixed;
      aux[i] = root;
    }
    return;
  }

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
      float sample = ClassicalWaveform(
          phase_[v], frequency[v], waveform);
      if (fold_amount > 0.0f) {
        // Driving Sine()'s argument past a quarter turn folds. The +1.0f is
        // load-bearing, not cosmetic: Sine() is documented safe "for phase >=
        // 0.0f", and `sample` is bipolar, so without the whole-period offset
        // the negative half of every waveform indexes lut_sine out of bounds.
        // That reads as a ~5.0 spike on OUT, which is exactly what the
        // audition gate caught here -- and the same defect the port's earlier
        // review found in z-filter.
        const float folded = Sine(1.0f + sample * fold_drive * 0.25f);
        sample += (folded - sample) * fold_amount;
      }
      mixed += sample * mix;
      if (v == 0) {
        root = sample;
      }
    }

    // A narrow-pulse or heavily folded stack carries DC, and the audio-health
    // gate rejects it.
    dc_out_ = mixed - dc_in_ + 0.999f * dc_out_;
    dc_in_ = mixed;
    out[i] = dc_out_;

    dc_aux_out_ = root - dc_aux_in_ + 0.999f * dc_aux_out_;
    dc_aux_in_ = root;
    aux[i] = dc_aux_out_;
  }
}

}  // namespace plaits
