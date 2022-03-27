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

#include "plaits/dsp/engine/chord_engine.h"

#include <algorithm>

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

const float chords[kChordNumChords][kChordNumNotes] = {
  ////// Jon Butler chords
  // Fixed Intervals
  { 0.00f, 0.01f, 11.99f, 12.00f },  // Octave
  { 0.00f, 7.01f,  7.00f, 12.00f },  // Fifth
  // Minor
  { 0.00f, 3.00f,  7.00f, 12.00f },  // Minor
  { 0.00f, 3.00f,  7.00f, 10.00f },  // Minor 7th
  { 0.00f, 3.00f, 10.00f, 14.00f },  // Minor 9th
  { 0.00f, 3.00f, 10.00f, 17.00f },  // Minor 11th
  // Major
  { 0.00f, 4.00f,  7.00f, 12.00f },  // Major
  { 0.00f, 4.00f,  7.00f, 11.00f },  // Major 7th
  { 0.00f, 4.00f, 11.00f, 14.00f },  // Major 9th
  // Colour Chords
  { 0.00f, 5.00f,  7.00f, 12.00f },  // Sus4
  { 0.00f, 2.00f,  9.00f, 16.00f },  // 69
  { 0.00f, 4.00f,  7.00f,  9.00f },  // 6th
  { 0.00f, 7.00f, 16.00f, 23.00f },  // 10th (Spread maj7)
  { 0.00f, 4.00f,  7.00f, 10.00f },  // Dominant 7th
  { 0.00f, 7.00f, 10.00f, 13.00f },  // Dominant 7th (b9)
  { 0.00f, 3.00f,  6.00f, 10.00f },  // Half Diminished
  { 0.00f, 3.00f,  6.00f,  9.00f },  // Fully Diminished

  ////// Joe McMullen chords
  {  5.00f, 12.00f, 19.00f, 26.00f }, // iv 6/9
  { 14.00f,  8.00f, 19.00f, 24.00f }, // iio 7sus4
  { 10.00f, 17.00f, 19.00f, 26.00f }, // VII 6
  {  7.00f, 14.00f, 22.00f, 24.00f }, // v m11
  { 15.00f, 10.00f, 19.00f, 20.00f }, // III add4
  { 12.00f, 19.00f, 20.00f, 27.00f }, // i addb13
  {  8.00f, 15.00f, 24.00f, 26.00f }, // VI add#11
  { 17.00f, 12.00f, 20.00f, 26.00f }, // iv m6
  { 14.00f, 17.00f, 20.00f, 23.00f }, // iio
  { 11.00f, 14.00f, 17.00f, 20.00f }, // viio
  {  7.00f, 14.00f, 17.00f, 23.00f }, // V 7
  {  4.00f,  7.00f, 17.00f, 23.00f }, // iii add b9
  { 12.00f,  7.00f, 16.00f, 23.00f }, // I maj7
  {  9.00f, 12.00f, 16.00f, 23.00f }, // vi m9
  {  5.00f, 12.00f, 19.00f, 21.00f }, // IV maj9
  { 14.00f,  9.00f, 17.00f, 24.00f }, // ii m7
  { 11.00f,  5.00f, 19.00f, 24.00f }, // I maj7sus4/vii
  {  7.00f, 14.00f, 17.00f, 24.00f }, // V 7sus4
};

const uint8_t originalChordMapping[kChordNumOriginalChords] = {
  0,  // OCT
  1,  // 5
  9,  // sus4
  2,  // m
  3,  // m7
  4,  // m9
  5,  // m11
  10, // 69
  8,  // M9
  7,  // M7
  6,  // M
};

void ChordEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kChordNumVoices; ++i) {
    divide_down_voice_[i].Init();
    wavetable_voice_[i].Init();
  }
  chord_index_quantizer_.Init();
  morph_lp_ = 0.0f;
  timbre_lp_ = 0.0f;
  
  ratios_ = allocator->Allocate<float>(kChordNumChords * kChordNumNotes);
}

void ChordEngine::Reset() {
  for (int i = 0; i < kChordNumChords; ++i) {
    for (int j = 0; j < kChordNumNotes; ++j) {
      ratios_[i * kChordNumNotes + j] = SemitonesToRatio(chords[i][j]);
    }
  }
}

const float fade_point[kChordNumVoices] = {
  0.52f, 0.48f, 0.49f, 0.50f, 0.51f
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

int ChordEngine::ComputeChordInversion(
    int chord_index,
    float inversion,
    float* ratios,
    float* amplitudes) {
  const float* base_ratio = &ratios_[chord_index * kChordNumNotes];
  inversion = inversion * float(kChordNumNotes * 5);

  MAKE_INTEGRAL_FRACTIONAL(inversion);
  
  int num_rotations = inversion_integral / kChordNumNotes;
  int rotated_note = inversion_integral % kChordNumNotes;
  
  const float kBaseGain = 0.25f;
  
  int mask = 0;
  
  for (int i = 0; i < kChordNumNotes; ++i) {
    float transposition = 0.25f * static_cast<float>(
        1 << ((kChordNumNotes - 1 + inversion_integral - i) / kChordNumNotes));
    int target_voice = (i - num_rotations + kChordNumVoices) % kChordNumVoices;
    int previous_voice = (target_voice - 1 + kChordNumVoices) % kChordNumVoices;
    
    if (i == rotated_note) {
      ratios[target_voice] = base_ratio[i] * transposition;
      ratios[previous_voice] = ratios[target_voice] * 2.0f;
      amplitudes[previous_voice] = kBaseGain * inversion_fractional;
      amplitudes[target_voice] = kBaseGain * (1.0f - inversion_fractional);
    } else if (i < rotated_note) {
      ratios[previous_voice] = base_ratio[i] * transposition;
      amplitudes[previous_voice] = kBaseGain;
    } else {
      ratios[target_voice] = base_ratio[i] * transposition;
      amplitudes[target_voice] = kBaseGain;
    }
    
    if (i == 0) {
      if (i >= rotated_note) {
        mask |= 1 << target_voice;
      }
      if (i <= rotated_note) {
        mask |= 1 << previous_voice;
      }
    }
  }
  return mask;
}

#define WAVE(bank, row, column) &wav_integrated_waves[(bank * 64 + row * 8 + column) * 260]

const int16_t* wavetable[] = {
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

  bool use_original_chords = parameters.custom_options == 0;
  int num_chords = kChordNumJonChords;
  if (use_original_chords) {
    num_chords = kChordNumOriginalChords;
  } else if (parameters.custom_options == 2) {
    num_chords = kChordNumJoeChords;
  }
  int chord_index = chord_index_quantizer_.Process(
      parameters.harmonics * 1.02f, num_chords);
  if (use_original_chords) {
    chord_index = originalChordMapping[chord_index];
  } else if (parameters.custom_options == 2) {
    chord_index = chord_index + kChordNumJonChords;
  }

  float harmonics[kChordNumHarmonics * 2 + 2];
  float note_amplitudes[kChordNumVoices];
  float registration = max(1.0f - morph_lp_ * 2.15f, 0.0f);
  
  ComputeRegistration(registration, harmonics);
  harmonics[kChordNumHarmonics * 2] = 0.0f;

  float ratios[kChordNumVoices];
  int aux_note_mask = ComputeChordInversion(
      chord_index,
      timbre_lp_,
      ratios,
      note_amplitudes);
  
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);
  
  const float f0 = NoteToFrequency(parameters.note) * 0.998f;
  const float waveform = max((morph_lp_ - 0.535f) * 2.15f, 0.0f);
  
  for (int note = 0; note < kChordNumVoices; ++note) {
    float wavetable_amount = (morph_lp_ > fade_point[note]) ? 1.0f : 0.0f;

    float divide_down_amount = 1.0f - wavetable_amount;
    float* destination = (1 << note) & aux_note_mask ? aux : out;
    
    const float note_f0 = f0 * ratios[note];
    float divide_down_gain = 0.5f;
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
  
  for (size_t i = 0; i < size; ++i) {
    out[i] += aux[i];
    aux[i] *= 3.0f;
  }
}

}  // namespace plaits
