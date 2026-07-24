// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Chords: wavetable and divide-down organ/string machine.
//
// OUT: all notes. AUX: the notes selected by the chord inversion, boosted.
// alt firmware, stereo mode: each note keeps the mix it contributes to OUT
// (including the macro's voice balance), panned to a fixed per-note
// position - root at the center, outer voices widest - with no AUX boost.

#include "plaits/dsp/engine/chord_engine.h"

#include <algorithm>

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void ChordEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kChordNumVoices; ++i) {
    divide_down_voice_[i].Init();
    wavetable_voice_[i].Init();
  }
  chords_.Init(allocator);
  
  morph_lp_ = 0.0f;
  timbre_lp_ = 0.0f;
}

void ChordEngine::Reset() {
  chords_.Reset();
}

const float fade_point[kChordNumVoices] = {
  0.55f, 0.47f, 0.49f, 0.51f, 0.53f
};

const float chord_pan[kChordNumVoices] = {
  0.5f, 0.2f, 0.8f, 0.05f, 0.95f
};

const int kRegistrationTableSize = 8;
const float registrations[kRegistrationTableSize][kChordNumHarmonics * 2] = {
  { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },  // Square
  { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },  // Saw
  { 0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f },  // Saw + saw
  { 0.33f, 0.0f, 0.33f, 0.0f, 0.33f, 0.0f },  // Full saw
  { 0.33f, 0.0f, 0.0f, 0.33f, 0.0f, 0.33f },  // Full saw + square hybrid
  { 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f },  // Saw + high square harmo
  { 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f },  // Square + high square harmo
  { 0.0f, 0.1f, 0.1f, 0.0f, 0.2f, 0.6f },  // // Saw+square + high harmo
};

void ChordEngine::ComputeRegistration(
    float registration,
    float* amplitudes) {
  registration *= (kRegistrationTableSize - 1.001f);
  MAKE_INTEGRAL_FRACTIONAL(registration);
  
  for (int i = 0; i < kChordNumHarmonics * 2; ++i) {
    float a = registrations[registration_integral][i];
    float b = registrations[registration_integral + 1][i];
    amplitudes[i] = a + (b - a) * registration_fractional;
  }
}

#define WAVE(bank, row, column) &wav_integrated_waves[(bank * 64 + row * 8 + column) * 132]

const int16_t* const wavetable[] = {
  WAVE(2, 6, 1),
  WAVE(2, 6, 6),
  WAVE(2, 6, 4),
  WAVE(0, 6, 0),
  WAVE(0, 6, 1),
  WAVE(0, 6, 2),
  WAVE(0, 6, 7),
  WAVE(2, 4, 7),
  WAVE(2, 4, 6),
  WAVE(2, 4, 5),
  WAVE(2, 4, 4),
  WAVE(2, 4, 3),
  WAVE(2, 4, 2),
  WAVE(2, 4, 1),
  WAVE(2, 4, 0),
};

void ChordEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  ONE_POLE(morph_lp_, parameters.morph, 0.1f);
  ONE_POLE(timbre_lp_, parameters.timbre, 0.1f);

  chords_.set_chord(parameters.harmonics, parameters.chord_set_option);

  float harmonics[kChordNumHarmonics * 2 + 2];
  float note_amplitudes[kChordNumVoices];
  float registration = max(1.0f - morph_lp_ * 2.15f, 0.0f);
  
  ComputeRegistration(registration, harmonics);
  harmonics[kChordNumHarmonics * 2] = 0.0f;

  float ratios[kChordNumVoices];
  int aux_note_mask = chords_.ComputeChordInversion(
      timbre_lp_,
      ratios,
      note_amplitudes);
  
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);
  
  const float f0 = NoteToFrequency(parameters.note) * 0.998f;
  const float waveform = max((morph_lp_ - 0.535f) * 2.15f, 0.0f);

  if ((PLAITS_STEREO_CHORDS && parameters.stereo)) {
    const float voice_balance = ApplyMacro(
        1.0f, 0.0f, 2.0f, parameters.macro);
    for (int note = 0; note < kChordNumVoices; ++note) {
      float wavetable_amount = 50.0f * (morph_lp_ - fade_point[note]);
      CONSTRAIN(wavetable_amount, 0.0f, 1.0f);

      float divide_down_amount = 1.0f - wavetable_amount;

      const float note_f0 = f0 * ratios[note];
      float divide_down_gain = 4.0f - note_f0 * 32.0f;
      CONSTRAIN(divide_down_gain, 0.0f, 1.0f);
      divide_down_amount *= divide_down_gain;

      float note_samples[kMaxBlockSize];
      fill(&note_samples[0], &note_samples[size], 0.0f);

      if (wavetable_amount) {
        wavetable_voice_[note].Render(
            note_f0 * 1.004f,
            note_amplitudes[note] * wavetable_amount,
            waveform,
            wavetable,
            note_samples,
            size);
      }

      if (divide_down_amount) {
        divide_down_voice_[note].Render(
            note_f0,
            harmonics,
            note_amplitudes[note] * divide_down_amount,
            note_samples,
            size);
      }

      float left_gain, right_gain;
      StereoPanGains(chord_pan[note], &left_gain, &right_gain);
      // A note the mask would have sent to AUX reaches OUT scaled by the
      // macro's voice balance: keep that level in the stereo mix.
      const float note_gain = (1 << note) & aux_note_mask
          ? voice_balance
          : 1.0f;
      left_gain *= note_gain;
      right_gain *= note_gain;
      for (size_t i = 0; i < size; ++i) {
        out[i] += note_samples[i] * left_gain;
        aux[i] += note_samples[i] * right_gain;
      }
    }
    return;
  }

  for (int note = 0; note < kChordNumVoices; ++note) {
    float wavetable_amount = 50.0f * (morph_lp_ - fade_point[note]);
    CONSTRAIN(wavetable_amount, 0.0f, 1.0f);

    float divide_down_amount = 1.0f - wavetable_amount;
    float* destination = (1 << note) & aux_note_mask ? aux : out;

    const float note_f0 = f0 * ratios[note];
    float divide_down_gain = 4.0f - note_f0 * 32.0f;
    CONSTRAIN(divide_down_gain, 0.0f, 1.0f);
    divide_down_amount *= divide_down_gain;

    if (wavetable_amount) {
      wavetable_voice_[note].Render(
          note_f0 * 1.004f,
          note_amplitudes[note] * wavetable_amount,
          waveform,
          wavetable,
          destination,
          size);
    }

    if (divide_down_amount) {
      divide_down_voice_[note].Render(
          note_f0,
          harmonics,
          note_amplitudes[note] * divide_down_amount,
          destination,
          size);
    }
  }

  const float voice_balance = ApplyMacro(
      1.0f, 0.0f, 2.0f, parameters.macro);
  for (size_t i = 0; i < size; ++i) {
    out[i] += aux[i] * voice_balance;
    aux[i] *= 3.0f;
  }
}

}  // namespace plaits
