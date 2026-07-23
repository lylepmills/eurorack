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
// Slope -> Waveshaper -> Wavefolder.

#ifndef PLAITS_DSP_ENGINE_WAVESHAPING_ENGINE_H_
#define PLAITS_DSP_ENGINE_WAVESHAPING_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/oscillator.h"

namespace plaits {

// OUT: wavefolder (lut_fold). AUX: sine/overtone blend of the second folder
// table (lut_fold_2). In stereo mode OUT/AUX become L/R and the AUX overtone
// blend is dropped: the two folder tables are panned apart instead (fold to
// 0.25, fold_2 to 0.75), so each channel carries a different fold of the same
// index while both remain in phase for a mono sum.
class WaveshapingEngine : public Engine {
 public:
  WaveshapingEngine() { }
  ~WaveshapingEngine() { }
  
  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return true; }

 private:
  Oscillator slope_;
  Oscillator triangle_;
  float previous_shape_;
  float previous_wavefolder_gain_;
  float previous_overtone_gain_;
  
  DISALLOW_COPY_AND_ASSIGN(WaveshapingEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_WAVESHAPING_ENGINE_H_