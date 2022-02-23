// Copyright 2015 Emilie Gillet.
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
// Group of voices.

#include "rings/dsp/part.h"

#include "stmlib/dsp/units.h"

#include "rings/resources.h"

namespace rings {

using namespace std;
using namespace stmlib;

void Part::Init(uint16_t* reverb_buffer) {
  active_voice_ = 0;
  acquisition_delay_ = 0;
  
  fill(&note_[0], &note_[kMaxPolyphony], 0.0f);
  
  bypass_ = false;
  polyphony_ = 1;
  model_ = RESONATOR_MODEL_MODAL;
  dirty_ = true;
  
  for (int32_t i = 0; i < kMaxPolyphony; ++i) {
    excitation_filter_[i].Init();
    plucker_[i].Init();
    strike_[i].Init();
    dc_blocker_[i].Init(1.0f - 10.0f / kSampleRate);
  }
  
  oscillator_.Init();

  tube_.Init();
  diffuser_.Init(diffuser_buffer_);
  bow_.Init();
  bow_.set_model(EXCITER_MODEL_FLOW);
  bow_.set_parameter(0.7f);
  bow_.set_timbre(0.5f);  
  blow_.Init();
  blow_.set_model(EXCITER_MODEL_GRANULAR_SAMPLE_PLAYER);

  reverb_.Init(reverb_buffer);
  limiter_.Init();

  note_filter_.Init(
      kSampleRate / kMaxBlockSize,
      0.001f,  // Lag time with a sharp edge on the V/Oct input or trigger.
      0.010f,  // Lag time after the trigger has been received.
      0.050f,  // Time to transition from reactive to filtered.
      0.004f); // Prevent a sharp edge to partly leak on the previous voice.
}

void Part::ConfigureResonators() {
  if (!dirty_) {
    return;
  }

  for (int32_t i = 0; i < polyphony_; ++i) {
    strike_[i].Init();
  }
  
  switch (model_) {
    case RESONATOR_MODEL_MODAL:
      {
        int32_t resolution = 64 / polyphony_ - 4;
        for (int32_t i = 0; i < polyphony_; ++i) {
          resonator_[i].Init();
          resonator_[i].set_resolution(resolution);
        }
      }
      break;
    
    case RESONATOR_MODEL_SYMPATHETIC_STRING:
    case RESONATOR_MODEL_STRING:
    case RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED:
    case RESONATOR_MODEL_STRING_AND_REVERB:
      {
        float lfo_frequencies[kNumStrings] = {
          0.5f, 0.4f, 0.35f, 0.23f, 0.211f, 0.2f, 0.171f
        };
        for (int32_t i = 0; i < kNumStrings; ++i) {
          bool has_dispersion = model_ == RESONATOR_MODEL_STRING || \
              model_ == RESONATOR_MODEL_STRING_AND_REVERB;
          string_[i].Init(has_dispersion);

          float f_lfo = float(kMaxBlockSize) / float(kSampleRate);
          f_lfo *= lfo_frequencies[i];
          lfo_[i].Init<COSINE_OSCILLATOR_APPROXIMATE>(f_lfo);
        }
        for (int32_t i = 0; i < polyphony_; ++i) {
          plucker_[i].Init();
        }
      }
      break;
    
    case RESONATOR_MODEL_FM_VOICE:
      {
        for (int32_t i = 0; i < polyphony_; ++i) {
          fm_voice_[i].Init();
        }
      }
      break;
    
    default:
      break;
  }

  if (active_voice_ >= polyphony_) {
    active_voice_ = 0;
  }
  dirty_ = false;
}

// Chord table by Bryan Noll:
float bryanChords[kMaxPolyphony][kNumBryanChords][8] = {
  {
    { -12.0f, -0.01f, 0.0f,  0.01f, 0.02f, 11.98f, 11.99f, 12.0f },  // OCT
    { -12.0f, -5.0f,  0.0f,  6.99f, 7.0f,  11.99f, 12.0f,  19.0f },  // 5
    { -12.0f, -5.0f,  0.0f,  5.0f,  7.0f,  11.99f, 12.0f,  17.0f },  // sus4
    { -12.0f, -5.0f,  0.0f,  3.0f,  7.0f,   3.01f, 12.0f,  19.0f },  // m
    { -12.0f, -5.0f,  0.0f,  3.0f,  7.0f,   3.01f, 10.0f,  19.0f },  // m7
    { -12.0f, -5.0f,  0.0f,  3.0f, 14.0f,   3.01f, 10.0f,  19.0f },  // m9
    { -12.0f, -5.0f,  0.0f,  3.0f,  7.0f,   3.01f, 10.0f,  17.0f },  // m11
    { -12.0f, -5.0f,  0.0f,  2.0f,  7.0f,   9.0f,  16.0f,  19.0f },  // 69
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.0f,  14.0f,  19.0f },  // M9
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.0f,  10.99f, 19.0f },  // M7
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.99f, 12.0f,  19.0f } // M
  },
  { 
    { -12.0f, 0.0f,  0.01f, 12.0f },  // OCT
    { -12.0f, 6.99f, 7.0f,  12.0f },  // 5
    { -12.0f, 5.0f,  7.0f,  12.0f },  // sus4
    { -12.0f, 3.0f, 11.99f, 12.0f },  // m
    { -12.0f, 3.0f, 10.0f,  12.0f },  // m7
    { -12.0f, 3.0f, 10.0f,  14.0f },  // m9
    { -12.0f, 3.0f, 10.0f,  17.0f },  // m11
    { -12.0f, 2.0f,  9.0f,  16.0f },  // 69
    { -12.0f, 4.0f, 11.0f,  14.0f },  // M9
    { -12.0f, 4.0f,  7.0f,  11.0f },  // M7
    { -12.0f, 4.0f,  7.0f,  12.0f },  // M
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 9.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f }
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 9.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f }
  }
};

// Based on the chord table for Plaits by Jon Butler:
// - organized into fixed, minor, major, colour chords
const float jonChords[kMaxPolyphony][kNumJonChords][8] = {
  {
    // Fixed Intervals
    { -12.0f, -0.01f, 0.0f, 0.01f, 0.02f, 11.98f, 11.99f, 12.0f },  // Octave
    { -12.0f, -5.0f,  0.0f, 6.99f,  7.0f, 11.99f,  12.0f, 19.0f },  // Fifth
    // Minor
    { -12.0f, -5.0f,  0.0f,  3.0f, 3.01f,   7.0f,  12.0f, 19.0f },  // Minor
    { -12.0f, -5.0f,  0.0f,  3.0f, 3.01f,   7.0f,  10.0f, 19.0f },  // Minor 7th
    { -12.0f, -5.0f,  0.0f,  3.0f, 3.01f,  10.0f,  14.0f, 19.0f },  // Minor 9th
    { -12.0f, -5.0f,  0.0f,  3.0f, 3.01f,   7.0f,  10.0f, 17.0f },  // Minor 11th
    // Major
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f, 11.99f,  12.0f, 19.0f },  // Major
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f, 10.99f,  11.0f, 19.0f },  // Major 7th
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.0f,  14.0f, 19.0f },  // Major 9th
    // Colour Chords
    { -12.0f, -5.0f,  0.0f,  5.0f,  7.0f, 11.99f,  12.0f, 17.0f },  // Sus4
    { -12.0f, -5.0f,  0.0f,  2.0f,  7.0f,   9.0f,  16.0f, 19.0f },  // 69
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,   9.0f,  12.0f, 19.0f },  // 6th
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  12.0f,  16.0f, 23.0f },  // 10th (Spread maj7)
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  10.0f,  12.0f, 19.0f },  // Dominant 7th
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  10.0f,  13.0f, 19.0f },  // Dominant 7th (b9)
    { -12.0f,  0.0f,  3.0f,  6.0f, 10.0f,  12.0f,  15.0f, 18.0f },  // Half Diminished
    { -12.0f,  0.0f,  3.0f,  6.0f,  9.0f,  12.0f,  15.0f, 18.0f },  // Fully Diminished
  },
  {
    // Fixed Intervals
    { -12.0f, 0.0f,   0.01f, 12.00f },  // Octave
    { -12.0f, 6.99f,  7.00f, 12.00f },  // Fifth
    // Minor
    { -12.0f, 3.00f, 11.99f, 12.00f },  // Minor
    { -12.0f, 3.00f,  9.99f, 10.00f },  // Minor 7th
    { -12.0f, 3.00f, 10.00f, 14.00f },  // Minor 9th
    { -12.0f, 3.00f, 10.00f, 17.00f },  // Minor 11th
    // Major
    { -12.0f, 4.00f,  7.00f, 12.00f },  // Major
    { -12.0f, 4.00f,  7.00f, 11.00f },  // Major 7th
    { -12.0f, 4.00f, 11.00f, 14.00f },  // Major 9th
    // Colour Chords
    { -12.0f, 5.00f,  7.00f, 12.00f },  // Sus4
    { -12.0f, 2.00f,  9.00f, 16.00f },  // 69
    { -12.0f, 4.00f,  7.00f,  9.00f },  // 6th
    { -12.0f, 7.00f, 16.00f, 23.00f },  // 10th (Spread maj7)
    { -12.0f, 4.00f,  7.00f, 10.00f },  // Dominant 7th
    { -12.0f, 7.00f, 10.00f, 13.00f },  // Dominant 7th (b9)
    { -12.0f, 3.00f,  6.00f, 10.00f },  // Half Diminished
    { -12.0f, 3.00f,  6.00f,  9.00f },  // Fully Diminished
  },
  {
    { 0.0f, 0.01f },  // Unison
    { 0.0f,  1.0f },  // Minor 2nd
    { 0.0f,  2.0f },  // Major 2nd
    { 0.0f,  3.0f },  // Minor 3rd
    { 0.0f,  4.0f },  // Major 3rd
    { 0.0f,  5.0f },  // Perfect 4th
    { 0.0f,  6.0f },  // Diminished 5th
    { 0.0f,  7.0f },  // Perfect 5th
    { 0.0f,  8.0f },  // Minor 6th
    { 0.0f,  9.0f },  // Major 6th
    { 0.0f, 10.0f },  // Minor 7th
    { 0.0f, 11.0f },  // Major 7th
    { 0.0f, 12.0f },  // Octave
    { 0.0f, 13.0f },  // Minor 9th
    { 0.0f, 14.0f },  // Major 9th
    { 0.0f, 15.0f },  // Minor 10th
    { 0.0f, 16.0f },  // Major 10th
  },
  {
    { 0.0f, 0.01f },  // Unison
    { 0.0f,  1.0f },  // Minor 2nd
    { 0.0f,  2.0f },  // Major 2nd
    { 0.0f,  3.0f },  // Minor 3rd
    { 0.0f,  4.0f },  // Major 3rd
    { 0.0f,  5.0f },  // Perfect 4th
    { 0.0f,  6.0f },  // Diminished 5th
    { 0.0f,  7.0f },  // Perfect 5th
    { 0.0f,  8.0f },  // Minor 6th
    { 0.0f,  9.0f },  // Major 6th
    { 0.0f, 10.0f },  // Minor 7th
    { 0.0f, 11.0f },  // Major 7th
    { 0.0f, 12.0f },  // Octave
    { 0.0f, 13.0f },  // Minor 9th
    { 0.0f, 14.0f },  // Major 9th
    { 0.0f, 15.0f },  // Minor 10th
    { 0.0f, 16.0f },  // Major 10th
  },
};

// Chord tables by Joe McMullen
// - each has a different theme based on the polyphony setting
// polyphony 1 -> cycle through scales so that Rings can be used like Martenot’s Palme Diffuseur
// polyphony 2 -> cycle through triads, sevenths and some more exotic chords all within a single key
// polyphony 3 -> cycle through the first sixteen harmonic intervals from the overtone series
// polyphony 4 -> cycle through justly tempered intervals based on the shruti of Indian classical music theory
const float joeChords[kMaxPolyphony][kMaxNumJoeChords][8] = {
  { // Scales
    { -12.0f, -5.0f, 0.0f, 6.0f,  11.0f, 15.0f, 20.0f, 26.0f },  // Hungarian Minor
    { -12.0f, -5.0f, 0.0f, 6.0f,  11.0f, 15.0f, 21.0f, 26.0f },  // Lydian Minor
    { -12.0f, -5.0f, 0.0f, 5.0f,  11.0f, 15.0f, 21.0f, 26.0f },  // Melodic Minor
    { -12.0f, -5.0f, 0.0f, 5.0f,  11.0f, 15.0f, 20.0f, 26.0f },  // Harmonic Minor
    { -12.0f, -5.0f, 0.0f, 5.0f,  10.0f, 15.0f, 20.0f, 26.0f },  // Diatonic Minor
    { -12.0f, -5.0f, 0.0f, 3.0f,  10.0f, 15.0f, 17.0f, 19.0f },  // Pentatonic Minor
    { -12.0f, -5.0f, 0.0f, 3.0f,  7.0f,  12.0f, 15.0f, 24.0f },  // Triadic Minor
    { -12.0f, -5.0f, 0.0f, 6.99f, 7.0f,  12.0f, 19.0f, 24.0f },  // Perfect Fifths
    { -12.0f, -5.0f, 0.0f, 4.0f,  7.0f,  12.0f, 16.0f, 24.0f },  // Triadic Major
    { -12.0f, -5.0f, 0.0f, 4.0f,  7.0f,  9.0f,  14.0f, 16.0f },  // Pentatonic Major
    { -12.0f, -5.0f, 0.0f, 5.0f,  11.0f, 16.0f, 21.0f, 26.0f },  // Diatonic Major
    { -12.0f, -5.0f, 0.0f, 5.0f,  11.0f, 16.0f, 20.0f, 26.0f },  // Harmonic Major
    { -12.0f, -5.0f, 0.0f, 5.0f,  10.0f, 16.0f, 20.0f, 26.0f },  // Melodic Major
    { -12.0f, -5.0f, 0.0f, 5.0f,  10.0f, 16.0f, 20.0f, 25.0f },  // Phrygian Major
    { -12.0f, -5.0f, 0.0f, 5.0f,  11.0f, 16.0f, 20.0f, 25.0f },  // Byzantine Major
  },
  { // Functional Harmony
    { 0.0f,  7.0f,  12.0f, 16.0f },  // I
    { -3.0f, 9.0f,  12.0f, 16.0f },  // vi
    { 5.0f,  9.0f,  12.0f, 17.0f },  // IV
    { 2.0f,  9.0f,  14.0f, 17.0f },  // ii
    { -1.0f, 11.0f, 14.0f, 17.0f },  // viio
    { -5.0f, 7.0f,  11.0f, 14.0f },  // V
    { 4.0f,  7.0f,  11.0f, 16.0f },  // iii
    { 0.0f,  7.0f,  11.0f, 16.0f },  // Imaj7
    { -3.0f, 7.0f,  12.0f, 16.0f },  // vim7
    { 5.0f,  9.0f,  12.0f, 16.0f },  // IVmaj7
    { 2.0f,  9.0f,  12.0f, 17.0f },  // iim7
    { -1.0f, 9.0f,  14.0f, 17.0f },  // viim7b5
    { -5.0f, 5.0f,  11.0f, 14.0f },  // V7
    { 4.0f,  7.0f,  11.0f, 14.0f },  // iiim7
    { 0.0f,  7.0f,  11.0f, 17.0f },  // Imaj7sus4
    { -3.0f, 7.0f,  11.0f, 16.0f },  // vim9
    { 5.0f,  9.0f,  11.0f, 16.0f },  // IVmaj7#11
    { 2.0f,  7.0f,  12.0f, 17.0f },  // iim11
    { -1.0f, 8.0f,  14.0f, 17.0f },  // viio7
    { -5.0f, 5.0f,  11.0f, 13.0f },  // V7b5
    { 4.0f,  8.0f,  12.0f, 14.0f },  // III+7
  },
  { // Harmonic Intervals
    { -12.0f, -11.99f },  // C0
    { -12.0f, 0.0f    },  // C1
    { -12.0f, 7.02f   },  // G1
    { -12.0f, 12.0f   },  // C2
    { -12.0f, 15.86f  },  // E2
    { -12.0f, 19.02f  },  // G2
    { -12.0f, 21.69f  },  // Bb2
    { -12.0f, 24.0f   },  // C3
    { -12.0f, 26.04f  },  // D3
    { -12.0f, 27.86f  },  // E3
    { -12.0f, 29.51f  },  // F#3
    { -12.0f, 31.02f  },  // G3
    { -12.0f, 32.41f  },  // Ab3
    { -12.0f, 33.69f  },  // Bb3
    { -12.0f, 34.88f  },  // B3
    { -12.0f, 36.0f   },  // C4
  },
  { // Just Intervals
    { 0.0f, 0.01f  },  // Unison
    { 0.0f, 0.90f  },  // Pythagorean Limma
    { 0.0f, 1.12f  },  // Minor Diatonic Semitone
    { 0.0f, 1.82f  },  // Minor Whole Tone
    { 0.0f, 2.04f  },  // Major Whole Tone
    { 0.0f, 2.94f  },  // Pythagorean Minor Third
    { 0.0f, 3.16f  },  // Minor Third
    { 0.0f, 3.86f  },  // Major Third
    { 0.0f, 4.08f  },  // Pythagorean Major Third
    { 0.0f, 4.98f  },  // Perfect Fourth
    { 0.0f, 5.20f  },  // Acute Fourth
    { 0.0f, 5.90f  },  // Tritone
    { 0.0f, 6.12f  },  // Pythagorean Tritone
    { 0.0f, 7.02f  },  // Perfect Fifth
    { 0.0f, 7.92f  },  // Pythagorean Minor Sixth
    { 0.0f, 8.14f  },  // Minor Sixth
    { 0.0f, 8.84f  },  // Major Sixth
    { 0.0f, 9.06f  },  // Pythagorean Major Sixth
    { 0.0f, 9.96f  },  // Pythagorean Minor Seventh
    { 0.0f, 10.18f },  // Minor Seventh
    { 0.0f, 10.88f },  // Major Seventh
    { 0.0f, 11.10f },  // Pythagorean Major Seventh
    { 0.0f, 12.0f  },  // Octave
  },
};

void Part::ComputeSympatheticStringsNotes(
    const PerformanceState& performance_state,
    float tonic,
    float note,
    float parameter,
    float* destination,
    size_t num_strings) {
  float notes[9] = {
      tonic,
      note - 12.0f,
      note - 7.01955f,
      note,
      note + 7.01955f,
      note + 12.0f,
      note + 19.01955f,
      note + 24.0f,
      note + 24.0f };
  const float detunings[4] = {
      0.013f,
      0.011f,
      0.007f,
      0.017f
  };
  
  if (parameter >= 2.0f) {
    // Quantized chords
    int32_t chord_index = parameter - 2.0f;
    const float* chord;
    if (performance_state.chord_table == CHORD_TABLE_BRYAN) {
      chord = bryanChords[polyphony_ - 1][chord_index];
    } else if (performance_state.chord_table == CHORD_TABLE_JON) {
      chord = jonChords[polyphony_ - 1][chord_index];
    } else {
      chord = joeChords[polyphony_ - 1][chord_index];
    }
    for (size_t i = 0; i < num_strings; ++i) {
      destination[i] = chord[i] + note;
    }
    return;
  }

  size_t num_detuned_strings = (num_strings - 1) >> 1;
  size_t first_detuned_string = num_strings - num_detuned_strings;
  
  for (size_t i = 0; i < first_detuned_string; ++i) {
    float note = 3.0f;
    if (i != 0) {
      note = parameter * 7.0f;
      parameter += (1.0f - parameter) * 0.2f;
    }
    
    MAKE_INTEGRAL_FRACTIONAL(note);
    note_fractional = Squash(note_fractional);

    float a = notes[note_integral];
    float b = notes[note_integral + 1];
    
    note = a + (b - a) * note_fractional;
    destination[i] = note;
    if (i + first_detuned_string < num_strings) {
      destination[i + first_detuned_string] = destination[i] + detunings[i & 3];
    }
  }
}

void Part::RenderModalVoice(
    int32_t voice,
    const PerformanceState& performance_state,
    const Patch& patch,
    bool update_patch,
    float frequency,
    float filter_cutoff,
    size_t size) {
  // Internal exciter is a pulse, pre-filter.
  if (performance_state.internal_exciter &&
      voice == active_voice_ &&
      performance_state.strum) {
    resonator_input_[0] += 0.25f * SemitonesToRatio(
        filter_cutoff * filter_cutoff * 24.0f) / filter_cutoff;
  }
  
  // Process through filter.
  excitation_filter_[voice].Process<FILTER_MODE_LOW_PASS>(
      resonator_input_, resonator_input_, size);

  Resonator& r = resonator_[voice];
  r.set_frequency(frequency);
  if (update_patch) {
    r.set_structure(patch.structure);
    r.set_brightness(patch.brightness * patch.brightness);
    if (PositionRepurposed(performance_state)) {
      r.set_position(0.0f);
    } else {
      r.set_position(patch.position);
    }
    r.set_damping(patch.damping);
  }
  r.Process(resonator_input_, out_buffer_, aux_buffer_, size);
}

void Part::RenderFMVoice(
    int32_t voice,
    const PerformanceState& performance_state,
    const Patch& patch,
    bool update_patch,
    float frequency,
    float filter_cutoff,
    size_t size) {
  FMVoice& v = fm_voice_[voice];
  if (performance_state.internal_exciter &&
      voice == active_voice_ &&
      performance_state.strum) {
    v.TriggerInternalEnvelope();
  }

  v.set_frequency(frequency);
  if (update_patch) {
    v.set_ratio(patch.structure);
    v.set_brightness(patch.brightness);
    if (PositionRepurposed(performance_state)) {
      v.set_feedback_amount(0.5f);
    } else {
      v.set_feedback_amount(patch.position);
    }
    v.set_position(/*patch.position*/ 0.0f);
    v.set_damping(patch.damping);
  }
  v.Process(resonator_input_, out_buffer_, aux_buffer_, size);
}

void Part::RenderStringVoice(
    int32_t voice,
    const PerformanceState& performance_state,
    const Patch& patch,
    bool update_patch,
    float frequency,
    float filter_cutoff,
    size_t size) {
  // Compute number of strings and frequency.
  int32_t num_strings = 1;
  float frequencies[kNumStrings];

  if (model_ == RESONATOR_MODEL_SYMPATHETIC_STRING ||
      model_ == RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED) {
    num_strings = kNumStrings / polyphony_;
    if (update_patch) {
      parameter_[voice] = model_ == RESONATOR_MODEL_SYMPATHETIC_STRING
        ? patch.structure
        : 2.0f + performance_state.chord;
    }
    ComputeSympatheticStringsNotes(
        performance_state,
        performance_state.tonic + performance_state.fm,
        performance_state.tonic + note_[voice] + performance_state.fm,
        parameter_[voice],
        frequencies,
        num_strings);
    for (int32_t i = 0; i < num_strings; ++i) {
      frequencies[i] = SemitonesToRatio(frequencies[i] - 69.0f) * a3;
    }
  } else { // RESONATOR_MODEL_STRING or RESONATOR_MODEL_STRING_AND_REVERB
    frequencies[0] = frequency;
  }

  if (voice == active_voice_) {
    const float gain = 1.0f / Sqrt(static_cast<float>(num_strings) * 2.0f);
    for (size_t i = 0; i < size; ++i) {
      resonator_input_[i] *= gain;
    }
  }

  // Process external input.
  excitation_filter_[voice].Process<FILTER_MODE_LOW_PASS>(
      resonator_input_, resonator_input_, size);
  
  float patch_position = patch.position;
  if (PositionRepurposed(performance_state)) {
    patch_position = 0.5f;
  }

  // Add noise burst.
  if (performance_state.internal_exciter) {
    if (voice == active_voice_ && performance_state.strum) {
      plucker_[voice].Trigger(frequency, filter_cutoff * 8.0f, patch_position);
    }
    plucker_[voice].Process(noise_burst_buffer_, size);
    for (size_t i = 0; i < size; ++i) {
      resonator_input_[i] += noise_burst_buffer_[i];
    }
  }
  dc_blocker_[voice].Process(resonator_input_, size);
  
  fill(&out_buffer_[0], &out_buffer_[size], 0.0f);
  fill(&aux_buffer_[0], &aux_buffer_[size], 0.0f);
  
  float structure = patch.structure;
  float dispersion = structure < 0.24f
      ? (structure - 0.24f) * 4.166f
      : (structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f);
  
  for (int32_t string = 0; string < num_strings; ++string) {
    int32_t i = voice + string * polyphony_;
    String& s = string_[i];
    float lfo_value = lfo_[i].Next();
    
    float brightness = patch.brightness;
    float damping = patch.damping;
    float position = patch_position;
    float glide = 1.0f;
    float string_index = static_cast<float>(string) / static_cast<float>(num_strings);
    const float* input = resonator_input_;
    
    if (model_ == RESONATOR_MODEL_STRING_AND_REVERB) {
      damping *= (2.0f - damping);
    }
    
    // When the internal exciter is used, string 0 is the main
    // source, the other strings are vibrating by sympathetic resonance.
    // When the internal exciter is not used, all strings are vibrating
    // by sympathetic resonance.
    if (string > 0 && performance_state.internal_exciter) {
      brightness *= (2.0f - brightness);
      brightness *= (2.0f - brightness);
      damping = 0.7f + patch.damping * 0.27f;
      float amount = (0.5f - fabs(0.5f - patch_position)) * 0.9f;
      position = patch_position + lfo_value * amount;
      glide = SemitonesToRatio((brightness - 1.0f) * 36.0f);
      input = sympathetic_resonator_input_;
    }
    
    s.set_frequency(frequencies[string], glide);
    if (update_patch) {
      s.set_dispersion(dispersion);
      s.set_brightness(brightness);
      s.set_position(position);
      s.set_damping(damping + string_index * (0.95f - damping));
    }
    s.Process(input, out_buffer_, aux_buffer_, size);
    
    if (string == 0) {
      // Was 0.1f, Ben Wilson -> 0.2f
      float gain = 0.2f / static_cast<float>(num_strings);
      for (size_t i = 0; i < size; ++i) {
        float sum = out_buffer_[i] - aux_buffer_[i];
        sympathetic_resonator_input_[i] = gain * sum;
      }
    }
  }
}

const int32_t kPingPattern[] = {
  1, 0, 2, 1, 0, 2, 1, 0
};

void Part::Process(
    const PerformanceState& performance_state,
    const Patch& patch,
    const float* in,
    float* out,
    float* aux,
    size_t size) {

  // Copy inputs to outputs when bypass mode is enabled.
  if (bypass_) {
    copy(&in[0], &in[size], &out[0]);
    copy(&in[0], &in[size], &aux[0]);
    return;
  }
  
  ConfigureResonators();
  
  note_filter_.Process(
      performance_state.note,
      performance_state.strum);

  if (performance_state.strum) {
    note_[active_voice_] = note_filter_.stable_note();
    if (polyphony_ > 1 && polyphony_ & 1) {
      active_voice_ = kPingPattern[step_counter_ % 8];
      step_counter_ = (step_counter_ + 1) % 8;
    } else {
      active_voice_ = (active_voice_ + 1) % polyphony_;
    }
    acquisition_delay_ = 3;
  }
  
  note_[active_voice_] = note_filter_.note();
  
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);
  for (int32_t voice = 0; voice < polyphony_; ++voice) {
    // Compute MIDI note value, frequency, and cutoff frequency for excitation
    // filter.
    float cutoff = patch.brightness * (2.0f - patch.brightness);
    float note = note_[voice] + performance_state.tonic + performance_state.fm;
    float frequency = SemitonesToRatio(note - 69.0f) * a3;
    float filter_cutoff_range = performance_state.internal_exciter
      ? frequency * SemitonesToRatio((cutoff - 0.5f) * 96.0f)
      : 0.4f * SemitonesToRatio((cutoff - 1.0f) * 108.0f);
    float filter_cutoff = min(voice == active_voice_
      ? filter_cutoff_range
      : (10.0f / kSampleRate), 0.499f);
    float filter_q = performance_state.internal_exciter ? 1.5f : 0.8f;

    // Process input with excitation filter. Inactive voices receive silence.
    // TODO - possible this should also be affected by update_patch
    excitation_filter_[voice].set_f_q<FREQUENCY_DIRTY>(filter_cutoff, filter_q);
    if (voice == active_voice_) {
      copy(&in[0], &in[size], &resonator_input_[0]);
    } else {
      fill(&resonator_input_[0], &resonator_input_[size], 0.0f);
    }

    if (performance_state.mode == MODE_MINI_ELEMENTS_STEREO) {
      FillExciterBuffer(performance_state, patch, frequency, voice, voice == active_voice_, size);
      for (size_t i = 0; i < size; ++i) {
        resonator_input_[i] += exciter_buffer_[i];
      }
    }
    
    bool update_patch = true;
    if (performance_state.strum_hold_option == 1) {
      update_patch = false;
      // Introduce a slight delay so that sequenced values where the sequence is driven
      // by the same gates as the strum gate have enough time to update before being sampled.
      if (acquisition_delay_ && (voice == active_voice_)) {
        if (--acquisition_delay_ == 0) {
          update_patch = true;
        }
      }
    }

    if (model_ == RESONATOR_MODEL_MODAL) {
      RenderModalVoice(
          voice, performance_state, patch, update_patch, frequency, filter_cutoff, size);
    } else if (model_ == RESONATOR_MODEL_FM_VOICE) {
      RenderFMVoice(
          voice, performance_state, patch, update_patch, frequency, filter_cutoff, size);
    } else {
      RenderStringVoice(
          voice, performance_state, patch, update_patch, frequency, filter_cutoff, size);
    }
    
    // TODO: should this be an option? to dispatch odd/even voices individually vs
    //   odd/even harmonics regardless of polyphony setting?
    if (polyphony_ == 1) {
      // Send the two sets of harmonics / pickups to individual outputs.
      for (size_t i = 0; i < size; ++i) {
        out[i] += out_buffer_[i];
        aux[i] += aux_buffer_[i];
      }
    } else {
      // Dispatch odd/even voices to individual outputs.
      float* destination = voice & 1 ? aux : out;
      for (size_t i = 0; i < size; ++i) {
        destination[i] += out_buffer_[i] - aux_buffer_[i];
      }
    }
  }
  
  if (model_ == RESONATOR_MODEL_STRING_AND_REVERB) {
    if (!PositionRepurposed(performance_state)) {
      for (size_t i = 0; i < size; ++i) {
        float l = out[i];
        float r = aux[i];
        out[i] = l * patch.position + (1.0f - patch.position) * r;
        aux[i] = r * patch.position + (1.0f - patch.position) * l;
      }
    }

    reverb_.set_amount(0.1f + patch.damping * 0.5f);
    reverb_.set_diffusion(0.625f);
    reverb_.set_time(0.35f + 0.63f * patch.damping);
    reverb_.set_input_gain(0.2f);
    reverb_.set_lp(0.3f + patch.brightness * 0.6f);
    reverb_.Process(out, aux, size);
    for (size_t i = 0; i < size; ++i) {
      aux[i] = -aux[i];
    }
  }
  
  // Apply limiter to string output.
  limiter_.Process(out, aux, size, model_gains_[model_]);

  float note = note_[active_voice_] + performance_state.tonic + performance_state.fm;
  float frequency = SemitonesToRatio(note - 69.0f) * a3;

  if (performance_state.mode == MODE_RINGS_WAVEFORM ||
      performance_state.mode == MODE_MINI_ELEMENTS_EXCITER) {
    for (size_t i = 0; i < size; ++i) {
      out[i] = Crossfade(out[i], aux[i], 0.5f);
    }
  }

  if (performance_state.mode == MODE_RINGS_WAVEFORM) {
    if (performance_state.waveform_exciter == 0) {
      oscillator_.Render<OSCILLATOR_SHAPE_SQUARE>(frequency, 0.5f, aux, size);
      for (size_t i = 0; i < size; ++i) {
        aux[i] /= 2.0f;
      }
    } else if (performance_state.waveform_exciter == 1) {
      oscillator_.Render<OSCILLATOR_SHAPE_IMPULSE_TRAIN>(frequency, 0.5f, aux, size);
      for (size_t i = 0; i < size; ++i) {
        aux[i] /= 1.5f;
      }
    } else if (performance_state.waveform_exciter == 2) {
      oscillator_.Render<OSCILLATOR_SHAPE_SAW>(frequency, 0.5f, aux, size);
      for (size_t i = 0; i < size; ++i) {
        aux[i] /= 2.0f;
      }
    }
  } else if (performance_state.mode == MODE_MINI_ELEMENTS_EXCITER) {
    FillExciterBuffer(performance_state, patch, frequency, 0, true, size);
    copy(&exciter_buffer_[0], &exciter_buffer_[size], &aux[0]);
  }
}

void Part::FillExciterBuffer(
    const PerformanceState& performance_state,
    const Patch& patch,
    float frequency,
    int32_t voice,
    bool is_active_voice,
    size_t size) {
  fill(&exciter_buffer_[0], &exciter_buffer_[size], 0.0f);

  uint8_t exciter_flags = 0;
  if (is_active_voice && performance_state.strum) {
    exciter_flags |= EXCITER_FLAG_RISING_EDGE;
  }
  if (is_active_voice && performance_state.strum_gate) {
    exciter_flags |= EXCITER_FLAG_GATE;
  }

  if (is_active_voice && performance_state.waveform_exciter == 0) {
    if (performance_state.frequency_locked) {
      bow_.set_timbre(performance_state.locked_frequency_pot_value);
    }
    bow_.Process(exciter_flags, exciter_buffer_, size);
  } else if (is_active_voice && performance_state.waveform_exciter == 1) {
    if (performance_state.frequency_locked) {
      blow_.set_timbre(performance_state.locked_frequency_pot_value);
    }
    blow_.set_parameter(patch.position);
    blow_.Process(exciter_flags, exciter_buffer_, size);
    tube_.Process(
      frequency,
      1.0f,  // envelope_value
      patch.damping,
      1.0f,  // tube_level
      exciter_buffer_,
      0.5f,  // tube_level * 0.5f
      size);
    diffuser_.Process(exciter_buffer_, size);
  } else if (performance_state.waveform_exciter == 2) {
    if (is_active_voice) {
      if (performance_state.frequency_locked) {
        strike_[voice].set_timbre(performance_state.locked_frequency_pot_value);
      }
      float strike_meta = patch.position;
      strike_[voice].set_meta(
        strike_meta <= 0.4f ? strike_meta * 0.625f : strike_meta * 1.25f - 0.25f,
        EXCITER_MODEL_SAMPLE_PLAYER,
        EXCITER_MODEL_PARTICLES);
    }

    strike_[voice].Process(exciter_flags, exciter_buffer_, size);
  }
}

/* static */
float Part::model_gains_[] = {
  1.4f,  // RESONATOR_MODEL_MODAL
  1.0f,  // RESONATOR_MODEL_SYMPATHETIC_STRING
  1.4f,  // RESONATOR_MODEL_STRING
  0.7f,  // RESONATOR_MODEL_FM_VOICE,
  1.0f,  // RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED
  1.4f,  // RESONATOR_MODEL_STRING_AND_REVERB
};

}  // namespace rings