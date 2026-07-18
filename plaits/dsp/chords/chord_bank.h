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

const int kChordNumNotes = 4;
const int kChordNumVoices = kChordNumNotes + 1;

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
      float r = ratios_[i];
      while (r > 2.0f) {
        r *= 0.5f;
      }
      sorted_ratios_[i] = r;
    }
    std::sort(&sorted_ratios_[0], &sorted_ratios_[kChordNumNotes]);
  }
  
  void set_chord(float parameter, uint8_t chord_set_option);
  
  inline int chord_index() const {
    return chord_index_;
  }
  
  inline const float* ratios() const {
    return ratios_;
  }

  inline float ratio(int note) const {
    return ratios_[note];
  }

  inline float sorted_ratio(int note) const {
    return sorted_ratios_[note];
  }
  
  inline int num_notes() const {
    return num_notes_;
  }

 private:
  void UpdateRatios(int chord_index);

  stmlib::HysteresisQuantizer2 chord_index_quantizer_;
  uint8_t chord_set_option_;
  int chord_index_;
  int num_notes_;
  float ratios_[kChordNumNotes];
  float sorted_ratios_[kChordNumNotes];

  DISALLOW_COPY_AND_ASSIGN(ChordBank);
};

}  // namespace plaits

#endif  // PLAITS_DSP_CHORDS_CHORD_BANK_H_
