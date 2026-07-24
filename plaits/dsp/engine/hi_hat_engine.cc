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
// 808-style HH with two noise sources - one faithful to the original, the other
// more metallic.
//
// OUT: faithful 808 hi-hat (hi_hat_1). AUX: metallic hi-hat (hi_hat_2).
// alt firmware, stereo mode: the faithful hi-hat is panned to 0.28 and the
// metallic one to 0.72.

#include "plaits/dsp/engine/hi_hat_engine.h"

namespace plaits {

using namespace stmlib;

void HiHatEngine::Init(BufferAllocator* allocator) {
  hi_hat_1_.Init();
  hi_hat_2_.Init();
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize * 2);
}

void HiHatEngine::Reset() {
  
}

void HiHatEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);
  const float metallic_spread = ApplyMacro(
      1.0f, 0.55f, 1.6f, parameters.macro);
  
  hi_hat_1_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      parameters.harmonics,
      metallic_spread,
      temp_buffer_,
      temp_buffer_ + size,
      out,
      size);
  
  hi_hat_2_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      parameters.harmonics,
      metallic_spread,
      temp_buffer_,
      temp_buffer_ + size,
      aux,
      size);

  if ((PLAITS_STEREO_ANALOG_HI_HAT && parameters.stereo)) {
    // Spread the faithful and metallic hi-hats across the stereo field.
    float faithful_left, faithful_right;
    float metallic_left, metallic_right;
    StereoPanGains(0.28f, &faithful_left, &faithful_right);
    StereoPanGains(0.72f, &metallic_left, &metallic_right);
    for (size_t i = 0; i < size; ++i) {
      const float faithful = out[i];
      const float metallic = aux[i];
      out[i] = faithful * faithful_left + metallic * metallic_left;
      aux[i] = faithful * faithful_right + metallic * metallic_right;
    }
  }
}

}  // namespace plaits
