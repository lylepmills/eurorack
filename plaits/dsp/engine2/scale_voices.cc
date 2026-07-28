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

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const int8_t kMajor[] = { 0, 2, 4, 5, 7, 9, 11 };
const int8_t kNaturalMinor[] = { 0, 2, 3, 5, 7, 8, 10 };
const int8_t kDorian[] = { 0, 2, 3, 5, 7, 9, 10 };
const int8_t kMixolydian[] = { 0, 2, 4, 5, 7, 9, 10 };
const int8_t kHarmonicMinor[] = { 0, 2, 3, 5, 7, 8, 11 };
const int8_t kMelodicMinor[] = { 0, 2, 3, 5, 7, 9, 11 };
const int8_t kMajorPentatonic[] = { 0, 2, 4, 7, 9 };
const int8_t kWholeTone[] = { 0, 2, 4, 6, 8, 10 };

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

}  // namespace

const Scale kScaleVoicesScales[kScaleVoicesNumScales] = {
  { kMajor, 7 },
  { kNaturalMinor, 7 },
  { kDorian, 7 },
  { kMixolydian, 7 },
  { kHarmonicMinor, 7 },
  { kMelodicMinor, 7 },
  { kMajorPentatonic, 5 },
  { kWholeTone, 6 },
};

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
  return static_cast<float>(octave * 12 + s.semitones[index]);
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
      float sample = Waveform(phase_[v], frequency[v], waveform);
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
