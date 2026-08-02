// Copyright 2016 Emilie Gillet.
// Copyright 2026 Rubato Audio.
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

#include "plaits/dsp/engine/virtual_analog_crossfade_engine.h"

#include <algorithm>

#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const float kIntervals[5] = {
  0.0f, 7.01f, 12.01f, 19.01f, 24.01f
};

inline float Squash(float x) {
  return x * x * (3.0f - 2.0f * x);
}

// The two stereo channels use nearby points on the model's own primary-to-
// auxiliary crossfade. Coefficients remain bounded, so stereo cannot create
// the overshoot that an out-of-phase mid/side widening would.
const float kStereoWidth = 0.16f;

}  // namespace

void VirtualAnalogCrossfadeEngine::Init(BufferAllocator* allocator) {
  primary_.Init();
  auxiliary_.Init();
  auxiliary_.set_master_phase(0.25f);
  sync_.Init();

  auxiliary_amount_ = 0.0f;
  xmod_amount_ = 0.0f;
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
}

void VirtualAnalogCrossfadeEngine::Reset() {
}

float VirtualAnalogCrossfadeEngine::ComputeDetuning(float detune) const {
  detune = 2.05f * detune - 1.025f;
  CONSTRAIN(detune, -1.0f, 1.0f);

  float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 3.9999f;
  MAKE_INTEGRAL_FRACTIONAL(detune);

  const float a = kIntervals[detune_integral];
  const float b = kIntervals[detune_integral + 1];
  return (a + (b - a) * Squash(Squash(detune_fractional))) * sign;
}

void VirtualAnalogCrossfadeEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  (void) already_enveloped;

  float auxiliary_amount = max(0.5f - parameters.timbre, 0.0f) * 2.0f;
  auxiliary_amount *= auxiliary_amount * 0.5f;

  const float xmod_amount = max(parameters.timbre - 0.5f, 0.0f) * 2.0f;
  const float squashed_xmod_amount = xmod_amount * (2.0f - xmod_amount);

  // MACRO 0.5 is exactly Emilie's original interval spread.
  const float auxiliary_detune =
      ComputeDetuning(parameters.harmonics) * parameters.macro * 2.0f;
  const float primary_f = NoteToFrequency(parameters.note);
  const float auxiliary_f = NoteToFrequency(
      parameters.note + auxiliary_detune);
  const float sync_f = primary_f * SemitonesToRatio(
      xmod_amount * (auxiliary_detune + 36.0f));

  float shape = parameters.morph * 1.5f;
  CONSTRAIN(shape, 0.0f, 1.0f);

  float pw = 0.5f + (parameters.morph - 0.66f) * 1.4f;
  CONSTRAIN(pw, 0.5f, 0.99f);

  primary_.Render(primary_f, pw, shape, out, size);
  sync_.Render(primary_f, sync_f, pw, shape, temp_buffer_, size);

  ParameterInterpolator xmod_amount_modulation(
      &xmod_amount_,
      squashed_xmod_amount * (2.0f - squashed_xmod_amount),
      size);
  for (size_t i = 0; i < size; ++i) {
    out[i] += (temp_buffer_[i] - out[i]) * xmod_amount_modulation.Next();
  }

  auxiliary_.Render(auxiliary_f, pw, shape, aux, size);

  ParameterInterpolator auxiliary_amount_modulation(
      &auxiliary_amount_,
      auxiliary_amount,
      size);
  if (PLAITS_STEREO_VIRTUAL_ANALOG_CROSSFADE && parameters.stereo) {
    // Unlike the old experimental stereo branch, this keeps TIMBRE's entire
    // detuned -> dry -> sync trajectory audible. L/R are nearby points on that
    // trajectory, and the interpolator advances exactly as it does in mono.
    for (size_t i = 0; i < size; ++i) {
      const float primary = out[i];
      const float secondary = aux[i];
      const float amount = auxiliary_amount_modulation.Next();
      const float left_amount = max(amount - kStereoWidth, 0.0f);
      const float right_amount = min(amount + kStereoWidth, 1.0f);
      out[i] = primary + (secondary - primary) * left_amount;
      aux[i] = primary + (secondary - primary) * right_amount;
    }
  } else {
    for (size_t i = 0; i < size; ++i) {
      out[i] += (aux[i] - out[i]) * auxiliary_amount_modulation.Next();
    }
  }
}

}  // namespace plaits
