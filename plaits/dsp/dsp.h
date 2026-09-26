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
// Utility DSP routines.

#ifndef PLAITS_DSP_DSP_H_
#define PLAITS_DSP_DSP_H_

#include "stmlib/stmlib.h"

#ifdef PLAITS_CORRECTED_SAMPLE_RATE
#include <cmath>
#endif

namespace plaits {
  
static const float kSampleRate = 48000.0f;

// There is no proper PLL for I2S, only a divider on the system clock to derive
// the bit clock.
// The division ratio is set to 47 (23 EVEN, 1 ODD) by the ST libraries.
//
// Bit clock = 72000000 / 47 = 1531.91 kHz
// Frame clock = Bit clock / 32 = 47872.34 Hz
//
// That's only 4.6 cts of error, but we care!
//
// A build whose samples really play at some other rate defines
// PLAITS_CORRECTED_SAMPLE_RATE as that rate. A desktop host that plays the
// engines at exactly kSampleRate (Palette) defines it as 48000.0f, which
// removes the correction; left undefined, the module's divider rate applies.

#ifdef PLAITS_CORRECTED_SAMPLE_RATE
static const float kCorrectedSampleRate = PLAITS_CORRECTED_SAMPLE_RATE;
#else
static const float kCorrectedSampleRate = 47872.34f;
#endif
const float a0 = (440.0f / 8.0f) / kCorrectedSampleRate;

// 12 * log2(kSampleRate / kCorrectedSampleRate): the semitones a pitch that
// does NOT go through a0 (a Braids table index, say) adds to agree with the
// ones that do (SPEC R6). The module keeps the literal, so its firmware gains
// no libm call; an override computes it, and 48 kHz gives exactly zero.
#ifdef PLAITS_CORRECTED_SAMPLE_RATE
static const float kCorrectedPitchOffset =
    12.0f * std::log2(kSampleRate / kCorrectedSampleRate);
#else
static const float kCorrectedPitchOffset = 0.046105f;
#endif

const size_t kMaxBlockSize = 24;
const size_t kBlockSize = 12;

}  // namespace plaits

#endif  // PLAITS_DSP_DSP_H_
