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

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

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

// Only the two waveforms bracketing the knob are computed; a four-way
// crossfade would cost twice this for the same result.
inline float Waveform(float phase, float dt, float waveform) {
  const float scaled = waveform * 3.0f;
  if (scaled < 1.0f) {
    const float sine = Sine(phase);
    return sine + (Triangle(phase) - sine) * scaled;
  } else if (scaled < 2.0f) {
    const float triangle = Triangle(phase);
    return triangle + (Saw(phase, dt) - triangle) * (scaled - 1.0f);
  } else {
    const float saw = Saw(phase, dt);
    return saw + (Square(phase, dt) - saw) * min(scaled - 2.0f, 1.0f);
  }
}

// The same waveforms with every per-block decision taken out of the sample
// loop: the knob's region (and so which pair is blended), whether the fold is
// on, and PolyBLEP's dt <= 0 guard (Render()'s frequencies come from
// NoteToFrequency and are always positive). Same arithmetic, same order, so
// the output is bit-identical; the loop used to spend ~30 float compares per
// voice per sample on these choices (on-module overrun sweep, 2026-09-24).
inline float PolyBlepPositive(float t, float dt) {
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

enum WaveRegion {
  WAVE_REGION_SINE_TRIANGLE,
  WAVE_REGION_TRIANGLE_SAW,
  WAVE_REGION_SAW_SQUARE
};

template <WaveRegion region>
inline float RegionWaveform(float phase, float dt, float blend) {
  if (region == WAVE_REGION_SINE_TRIANGLE) {
    // phase is already in [0, 1): the same table read without the wrap.
    const float sine = SineNoWrap(phase);
    return sine + (Triangle(phase) - sine) * blend;
  } else if (region == WAVE_REGION_TRIANGLE_SAW) {
    const float triangle = Triangle(phase);
    const float saw = 2.0f * phase - 1.0f - PolyBlepPositive(phase, dt);
    return triangle + (saw - triangle) * blend;
  } else {
    const float saw = 2.0f * phase - 1.0f - PolyBlepPositive(phase, dt);
    float other = phase + 0.5f;
    if (other >= 1.0f) {
      other -= 1.0f;
    }
    const float naive = phase < 0.5f ? 1.0f : -1.0f;
    const float square =
        naive + PolyBlepPositive(phase, dt) - PolyBlepPositive(other, dt);
    return saw + (square - saw) * blend;
  }
}

struct ScaleVoiceState {
  float* phase;
  float* dc_in;
  float* dc_out;
  float* dc_aux_in;
  float* dc_aux_out;
};

template <WaveRegion region, bool folding>
void RenderScaleVoices(
    const ScaleVoiceState& state,
    const int* voices,
    int num_audible,
    bool root_audible,
    const float* frequency,
    float blend,
    float fold_drive,
    float fold_amount,
    float mix,
    float* out,
    float* aux,
    size_t size) {
  float* phase = state.phase;
  float dc_in = *state.dc_in;
  float dc_out = *state.dc_out;
  float dc_aux_in = *state.dc_aux_in;
  float dc_aux_out = *state.dc_aux_out;
  for (size_t i = 0; i < size; ++i) {
    float mixed = 0.0f;
    float root = 0.0f;
    for (int n = 0; n < num_audible; ++n) {
      const int v = voices[n];
      phase[v] += frequency[v];
      if (phase[v] >= 1.0f) {
        phase[v] -= 1.0f;
      }
      float sample = RegionWaveform<region>(phase[v], frequency[v], blend);
      if (folding) {
        // See Render(): the +1.0f keeps Sine()'s argument non-negative.
        const float folded = Sine(1.0f + sample * fold_drive * 0.25f);
        sample += (folded - sample) * fold_amount;
      }
      mixed += sample * mix;
      if (root_audible && n == 0) {
        root = sample;
      }
    }
    dc_out = mixed - dc_in + 0.999f * dc_out;
    dc_in = mixed;
    out[i] = dc_out;
    dc_aux_out = root - dc_aux_in + 0.999f * dc_aux_out;
    dc_aux_in = root;
    aux[i] = dc_aux_out;
  }
  *state.dc_in = dc_in;
  *state.dc_out = dc_out;
  *state.dc_aux_in = dc_aux_in;
  *state.dc_aux_out = dc_aux_out;
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
  quantized_ = false;
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
  const float fold_amount = max(1.0f - waveform * 3.0f, 0.0f);

  int voices[kScaleVoicesMaxVoices];
  int num_audible = 0;
  for (int v = 0; v < num_voices; ++v) {
    if (audible[v]) {
      voices[num_audible++] = v;
    }
  }
  const bool root_audible = num_audible > 0 && voices[0] == 0;
  const ScaleVoiceState state = {
    phase_, &dc_in_, &dc_out_, &dc_aux_in_, &dc_aux_out_ };
  // Waveform()'s region choice and blend, taken once per block.
  const float scaled = waveform * 3.0f;
  const bool folding = fold_amount > 0.0f;
  if (scaled < 1.0f) {
    if (folding) {
      RenderScaleVoices<WAVE_REGION_SINE_TRIANGLE, true>(
          state, voices, num_audible, root_audible, frequency, scaled,
          fold_drive, fold_amount, mix, out, aux, size);
    } else {
      RenderScaleVoices<WAVE_REGION_SINE_TRIANGLE, false>(
          state, voices, num_audible, root_audible, frequency, scaled,
          fold_drive, fold_amount, mix, out, aux, size);
    }
  } else if (scaled < 2.0f) {
    RenderScaleVoices<WAVE_REGION_TRIANGLE_SAW, false>(
        state, voices, num_audible, root_audible, frequency, scaled - 1.0f,
        fold_drive, fold_amount, mix, out, aux, size);
  } else {
    RenderScaleVoices<WAVE_REGION_SAW_SQUARE, false>(
        state, voices, num_audible, root_audible, frequency,
        min(scaled - 2.0f, 1.0f), fold_drive, fold_amount, mix, out, aux,
        size);
  }
}

void ScaleVoiceBank::RenderFrequencyOffset(
    const float* notes,
    int num_voices,
    float waveform,
    float detune_cents,
    float fold,
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

  const float mix = 1.0f / static_cast<float>(max(num_voices, 1));
  const float fold_drive = 1.0f + fold * (kScaleVoicesMaxFoldDrive - 1.0f);
  const float fold_amount = max(1.0f - waveform * 3.0f, 0.0f);

  for (size_t i = 0; i < size; ++i) {
    float mixed = 0.0f;
    float root = 0.0f;
    for (int v = 0; v < num_voices; ++v) {
      float f = frequency[v] +
          root_frequency_offset[i] * frequency_ratio[v];
      if ((PLAITS_BUILD_EXTENDED_TZFM ? fabsf(f) : f) > kScaleVoicesMaxVoiceFrequency) {
        continue;
      }
      if (!PLAITS_BUILD_EXTENDED_TZFM && f < 1.0e-7f) {
        f = 1.0e-7f;
      }
      phase_[v] += f;
      if (PLAITS_BUILD_EXTENDED_TZFM && phase_[v] < 0.0f) phase_[v] += 1.0f;
      if (phase_[v] >= 1.0f) {
        phase_[v] -= 1.0f;
      }
      float sample = Waveform(phase_[v], (PLAITS_BUILD_EXTENDED_TZFM ? fabsf(f) : f), waveform);
      if (fold_amount > 0.0f) {
        const float folded = Sine(1.0f + sample * fold_drive * 0.25f);
        sample += (folded - sample) * fold_amount;
      }
      mixed += sample * mix;
      if (v == 0) {
        root = sample;
      }
    }

    dc_out_ = mixed - dc_in_ + 0.999f * dc_out_;
    dc_in_ = mixed;
    out[i] = dc_out_;
    dc_aux_out_ = root - dc_aux_in_ + 0.999f * dc_aux_out_;
    dc_aux_in_ = root;
    aux[i] = dc_aux_out_;
  }
}

}  // namespace plaits
