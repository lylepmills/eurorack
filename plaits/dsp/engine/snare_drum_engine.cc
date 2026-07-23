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
// 808 and synthetic snare drum generators.
//
// OUT: analog 808 snare. AUX: synthetic snare.
// alt firmware, stereo mode: the analog snare is panned to 0.3 and the
// synthetic snare to 0.7 - snares tolerate more width than kicks.

#include "plaits/dsp/engine/snare_drum_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

void SnareDrumEngine::Init(BufferAllocator* allocator) {
  analog_snare_drum_.Init();
  synthetic_snare_drum_.Init();
}

void SnareDrumEngine::Reset() {
  
}

void SnareDrumEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);
  const float mode_spread = ApplyMacro(
      1.0f, 0.5f, 1.75f, parameters.macro);
  
  analog_snare_drum_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      parameters.harmonics,
      mode_spread,
      out,
      size);
  
  synthetic_snare_drum_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      parameters.harmonics,
      mode_spread,
      aux,
      size);

  if (parameters.stereo) {
    // Spread the two snare models across the stereo field; snares tolerate
    // more width than kicks.
    float analog_left, analog_right;
    float synthetic_left, synthetic_right;
    StereoPanGains(0.3f, &analog_left, &analog_right);
    StereoPanGains(0.7f, &synthetic_left, &synthetic_right);
    for (size_t i = 0; i < size; ++i) {
      const float analog = out[i];
      const float synthetic = aux[i];
      out[i] = analog * analog_left + synthetic * synthetic_left;
      aux[i] = analog * analog_right + synthetic * synthetic_right;
    }
  }
}

}  // namespace plaits
