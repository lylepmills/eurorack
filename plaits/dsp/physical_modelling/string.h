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
// Comb filter / KS string. "Lite" version of the implementation used in Rings.

#ifndef PLAITS_DSP_PHYSICAL_MODELLING_STRING_H_
#define PLAITS_DSP_PHYSICAL_MODELLING_STRING_H_

#include <algorithm>

#include "stmlib/stmlib.h"

#include "stmlib/dsp/filter.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/copyable_svf.h"
#include "plaits/dsp/local_random.h"
#include "plaits/dsp/physical_modelling/delay_line.h"

namespace plaits {

const size_t kDelayLineSize = 1024;

enum StringNonLinearity {
  STRING_NON_LINEARITY_CURVED_BRIDGE,
  STRING_NON_LINEARITY_DISPERSION
};

class String {
 public:
  String() { }
  ~String() { }
  
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void Process(
      float f0,
      float non_linearity_amount,
      float brightness,
      float damping,
      const float* in,
      float* out,
      size_t size);

 private:
  template<StringNonLinearity non_linearity>
  void ProcessInternal(
      float f0,
      float non_linearity_amount,
      float brightness,
      float damping,
      const float* in,
      float* out,
      size_t size);
  
  DelayLine<float, kDelayLineSize> string_;
  DelayLine<float, kDelayLineSize / 4> stretch_;
  
  // stmlib::DCBlocker's arithmetic as a plain struct, and a CopyableSvf for
  // stmlib::Svf: ProcessInternal runs the per-sample model on local copies of
  // these and of the floats below. As members they were reloaded and stored
  // every sample, since the output buffer being written could alias them.
  struct DcBlocker {
    inline float Process(float in) {
      const float old_x = x;
      x = in;
      return y = y * pole + x - old_x;
    }
    float x;
    float y;
    float pole;
  };
  CopyableSvf iir_damping_filter_;
  DcBlocker dc_blocker_;
  
  float delay_;
  float dispersion_noise_;
  float curved_bridge_;
  
  // Very crappy linear interpolation upsampler used for low pitches that
  // do not fit the delay line. Rarely used.
  float src_phase_;
  float out_sample_[2];

  DISALLOW_COPY_AND_ASSIGN(String);
};

}  // namespace plaits

#endif  // PLAITS_DSP_PHYSICAL_MODELLING_STRING_H_
