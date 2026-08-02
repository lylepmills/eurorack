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
// Three voices of string synthesis.
//
// OUT: sum of the three strings. AUX: raw exciter signals.
// alt firmware, stereo mode: each string is panned to a fixed position -
// left, center, right in round-robin order - and the exciter bus is not
// rendered.

#include "plaits/dsp/engine/string_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

void StringEngine::Init(BufferAllocator* allocator) {
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
  for (int i = 0; i < kNumStrings; ++i) {
    voice_[i].Init(allocator);
    f0_[i] = 0.01f;
  }
  active_string_ = kNumStrings - 1;
  f0_delay_.Init(allocator->Allocate<float>(16));
}

void StringEngine::Reset() {
  f0_delay_.Reset();
  for (int i = 0; i < kNumStrings; ++i) {
    voice_[i].Reset();
  }
}

// Equal-power gains for the three fixed pan positions (0.15, 0.5, 0.85).
// Keep the one-ULP asymmetry between sqrt(0.15) and sqrt(1.0f - 0.85f).
const float string_pan_left[kNumStrings] = {
  0.921954453f, 0.707106769f, 0.387298316f
};
const float string_pan_right[kNumStrings] = {
  0.387298346f, 0.707106769f, 0.921954453f
};

void StringEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const bool contour_excitation = parameters.articulation_envelope_active;
  const bool sustain =
      (parameters.trigger & TRIGGER_UNPATCHED) || contour_excitation;
  const float accent = contour_excitation
      ? parameters.accent * parameters.articulation_envelope
      : parameters.accent;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // 8 in original firmware version.
    // 05.01.18: mic.w: problem with microbrute.
    f0_[active_string_] = f0_delay_.Read(14);
    active_string_ = (active_string_ + 1) % kNumStrings;
  }
  
  const float f0 = NoteToFrequency(parameters.note);
  f0_[active_string_] = f0;
  f0_delay_.Write(f0);
  
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);
  const float exciter_size = ApplyMacro(
      1.0f, 0.25f, 4.0f, parameters.macro);

  if ((PLAITS_STEREO_INHARMONIC_STRING && parameters.stereo)) {
    for (int i = 0; i < kNumStrings; ++i) {
      float string_out[kMaxBlockSize];
      fill(&string_out[0], &string_out[size], 0.0f);
      voice_[i].Render(
          sustain && i == active_string_,
          parameters.trigger & TRIGGER_RISING_EDGE && i == active_string_,
          accent,
          f0_[i],
          parameters.harmonics,
          parameters.timbre * parameters.timbre,
          parameters.morph,
          exciter_size,
          temp_buffer_,
          string_out,
          size);
      const float left_gain = string_pan_left[i];
      const float right_gain = string_pan_right[i];
      for (size_t j = 0; j < size; ++j) {
        out[j] += string_out[j] * left_gain;
        aux[j] += string_out[j] * right_gain;
      }
    }
    return;
  }

  for (int i = 0; i < kNumStrings; ++i) {
    voice_[i].Render(
        sustain && i == active_string_,
        parameters.trigger & TRIGGER_RISING_EDGE && i == active_string_,
        accent,
        f0_[i],
        parameters.harmonics,
        parameters.timbre * parameters.timbre,
        parameters.morph,
        exciter_size,
        temp_buffer_,
        out,
        size);
    for (size_t j = 0; j < size; ++j) {
      aux[j] += temp_buffer_[j];
    }
  }
}

}  // namespace plaits
