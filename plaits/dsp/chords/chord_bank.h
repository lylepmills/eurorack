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
// Chord bank shared by several engines.

#ifndef PLAITS_DSP_CHORDS_CHORD_BANK_H_
#define PLAITS_DSP_CHORDS_CHORD_BANK_H_

#include "stmlib/dsp/hysteresis_quantizer.h"

#include "stmlib/utils/buffer_allocator.h"

#include <algorithm>

namespace plaits {

const int kNumChordSetOpts = 3;

const int kChordNumNotes = 4;
const int kChordNumVoices = kChordNumNotes + 1;

const int kChordNumChords = 35;
const int kChordNumOriginalChords = 11;
const int kChordNumJonChords = 17;
const int kChordNumJoeChords = 18;

const int originalChordMapping[kChordNumOriginalChords] = {
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

class ChordBank {
 public:
  ChordBank() { }
  ~ChordBank() { }
  
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  
  int ComputeChordInversion(
      float inversion, float* ratios, float* amplitudes);
  
  inline void Sort() {
    for (int i = 0; i < kChordNumNotes; ++i) {
      float r = ratio(i);
      while (r > 2.0f) {
        r *= 0.5f;
      }
      sorted_ratios_[i] = r;
    }
    std::sort(&sorted_ratios_[0], &sorted_ratios_[kChordNumNotes]);
  }
  
  // Options
  // 1. Original chords
  // 2. Jon Butler chords
  // 3. Joe McMullen chords
  inline void set_chord(float parameter, uint8_t chord_set_option) {
    chord_set_option_ = chord_set_option;
    chord_index_quantizer_[chord_set_option_].Process(parameter * 1.02f);
  }
  
  inline int chord_index() const {
    int value = chord_index_quantizer_[chord_set_option_].quantized_value();
    if (chord_set_option_ == 0) {
      return originalChordMapping[value];
    } else if (chord_set_option_ == 1) {
      return value;
    }
    return kChordNumJonChords + value;
  }
  
  inline const float* ratios() const {
    return &ratios_[chord_index() * kChordNumNotes];
  }

  inline float ratio(int note) const {
    return ratios_[chord_index() * kChordNumNotes + note];
  }

  inline float sorted_ratio(int note) const {
    return sorted_ratios_[note];
  }
  
  inline int num_notes() const {
    return note_count_[chord_index()];
  }

 private:
  stmlib::HysteresisQuantizer2 chord_index_quantizer_[kNumChordSetOpts];

  uint8_t chord_set_option_;
  
  float* ratios_;
  float* sorted_ratios_;
  int* note_count_;
  
  // static const float chords_[kChordNumChords][kChordNumNotes];
  
  DISALLOW_COPY_AND_ASSIGN(ChordBank);
};

}  // namespace plaits

#endif  // PLAITS_DSP_CHORDS_CHORD_BANK_H_