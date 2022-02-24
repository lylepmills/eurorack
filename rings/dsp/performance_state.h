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
// Note triggering state.

#ifndef RINGS_DSP_PERFORMANCE_STATE_H_
#define RINGS_DSP_PERFORMANCE_STATE_H_

namespace rings {

const int32_t kNumBryanChords = 11;
const int32_t kNumJonChords = 17;
const int32_t kNumJoeEasterEggChords = 18;
const int32_t kMaxNumJoeChords = 23;

enum PerformanceMode {
  MODE_RINGS_STEREO,
  MODE_RINGS_WAVEFORM,
  MODE_MINI_ELEMENTS_STEREO,
  MODE_MINI_ELEMENTS_EXCITER,
  MODE_EASTER_EGG,
};

enum ChordTable {
  CHORD_TABLE_BRYAN,
  CHORD_TABLE_JON,
  CHORD_TABLE_JOE,
};

struct PerformanceState {
  bool strum;
  bool strum_gate;
  bool internal_exciter;
  bool internal_strum;
  bool internal_note;

  PerformanceMode mode;
  ChordTable chord_table;

  float tonic;
  float note;
  float fm;
  float locked_frequency_pot_value;
  int32_t chord;
  uint8_t frequency_locked;
  uint8_t waveform_exciter;
  uint8_t strum_hold_option;

  bool MiniElements() const {
    return (mode == MODE_MINI_ELEMENTS_STEREO) || (mode == MODE_MINI_ELEMENTS_EXCITER);
  }
};

}  // namespace rings

#endif  // RINGS_DSP_PERFORMANCE_STATE_H_
