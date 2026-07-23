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
// 808 and synthetic bass drum generators.
//
// OUT: analog 808 kick (after overdrive). AUX: synthetic kick.
// alt firmware, stereo mode: the analog kick is panned to 0.42 and the
// synthetic kick to 0.58 - both kept near centre so the low end stays largely
// mono-compatible.

#include "plaits/dsp/engine/bass_drum_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

void BassDrumEngine::Init(BufferAllocator* allocator) {
  analog_bass_drum_.Init();
  synthetic_bass_drum_.Init();
  overdrive_.Init();
}

void BassDrumEngine::Reset() {
  
}

void BassDrumEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);
  
  const float punch = ApplyMacro(1.0f, 0.25f, 1.75f, parameters.macro);
  const float attack_fm_amount = min(parameters.harmonics * 4.0f, 1.0f) * punch;
  const float self_fm_amount = max(min(parameters.harmonics * 4.0f - 1.0f, 1.0f), 0.0f);
  const float drive = max(parameters.harmonics * 2.0f - 1.0f, 0.0f) * \
      max(1.0f - 16.0f * f0, 0.0f);
  
  const bool sustain = parameters.trigger & TRIGGER_UNPATCHED;
  
  analog_bass_drum_.Render(
      sustain,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      attack_fm_amount,
      self_fm_amount,
      out,
      size);

  overdrive_.Process(
      0.5f + 0.5f * drive,
      out,
      size);

  synthetic_bass_drum_.Render(
      sustain,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      sustain
          ? parameters.harmonics
          : 0.4f - 0.25f * parameters.morph * parameters.morph,
      min(parameters.harmonics * 2.0f, 1.0f) * punch,
      max(parameters.harmonics * 2.0f - 1.0f, 0.0f),
      aux,
      size);

  if (parameters.stereo) {
    // Spread the two kick models across the stereo field. Both are panned
    // close to the centre so the low end stays largely mono-compatible.
    float analog_left, analog_right;
    float synthetic_left, synthetic_right;
    StereoPanGains(0.42f, &analog_left, &analog_right);
    StereoPanGains(0.58f, &synthetic_left, &synthetic_right);
    for (size_t i = 0; i < size; ++i) {
      const float analog = out[i];
      const float synthetic = aux[i];
      out[i] = analog * analog_left + synthetic * synthetic_left;
      aux[i] = analog * analog_right + synthetic * synthetic_right;
    }
  }
}

}  // namespace plaits
