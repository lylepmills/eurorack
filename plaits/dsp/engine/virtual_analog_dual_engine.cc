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

#include "plaits/dsp/engine/virtual_analog_dual_engine.h"

#include "plaits/build_config.h"

#if PLAITS_BUILD_ENABLE_SYNC_INPUT
#define PLAITS_HARD_SYNC_EVENTS(parameters) ((parameters).hard_sync)
#else
#define PLAITS_HARD_SYNC_EVENTS(parameters) 0u
#endif

namespace plaits {

using namespace stmlib;

namespace {

const float kIntervals[5] = {
  0.0f, 7.01f, 12.01f, 19.01f, 24.01f
};

inline float Squash(float x) {
  return x * x * (3.0f - 2.0f * x);
}

// A small opposing mix offset makes the constituent oscillators locatable
// without turning the stereo render into two unrelated mono voices.
const float kStereoWidth = 0.18f;

}  // namespace

void VirtualAnalogDualEngine::Init(BufferAllocator* allocator) {
  primary_.Init();
  auxiliary_.Init();
  auxiliary_.set_master_phase(0.25f);
  sync_.Init();
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
}

void VirtualAnalogDualEngine::Reset() {
}

float VirtualAnalogDualEngine::ComputeDetuning(float detune) const {
  detune = 2.05f * detune - 1.025f;
  CONSTRAIN(detune, -1.0f, 1.0f);

  float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 3.9999f;
  MAKE_INTEGRAL_FRACTIONAL(detune);

  const float a = kIntervals[detune_integral];
  const float b = kIntervals[detune_integral + 1];
  return (a + (b - a) * Squash(Squash(detune_fractional))) * sign;
}

void VirtualAnalogDualEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  (void) already_enveloped;

  // MACRO 0.5 is exactly Emilie's original interval spread. Scaling the signed
  // result preserves the centre/unison point and every contour in the lookup.
  const float auxiliary_detune =
      ComputeDetuning(parameters.harmonics) * parameters.macro * 2.0f;
  const float primary_f = NoteToFrequency(parameters.note);
  const float auxiliary_f = NoteToFrequency(
      parameters.note + auxiliary_detune);
  const float sync_f = NoteToFrequency(
      parameters.note + parameters.harmonics * 48.0f);

  float primary_shape = parameters.timbre * 1.5f;
  CONSTRAIN(primary_shape, 0.0f, 1.0f);

  float primary_pw = 0.5f + (parameters.timbre - 0.66f) * 1.4f;
  CONSTRAIN(primary_pw, 0.5f, 0.99f);

  float auxiliary_shape = parameters.morph * 1.5f;
  CONSTRAIN(auxiliary_shape, 0.0f, 1.0f);

  float auxiliary_pw = 0.5f + (parameters.morph - 0.66f) * 1.4f;
  CONSTRAIN(auxiliary_pw, 0.5f, 0.99f);

#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  float auxiliary_frequency_offset[kMaxBlockSize];
  float sync_frequency_offset[kMaxBlockSize];
  const float auxiliary_ratio = auxiliary_f /
      (primary_f > 1.0e-9f ? primary_f : 1.0e-9f);
  const float sync_ratio = sync_f /
      (primary_f > 1.0e-9f ? primary_f : 1.0e-9f);
  if (parameters.frequency_offset) {
    for (size_t i = 0; i < size; ++i) {
      const float offset = parameters.frequency_offset[i];
      auxiliary_frequency_offset[i] = offset * auxiliary_ratio;
      sync_frequency_offset[i] = offset * sync_ratio;
    }
  }
#endif

#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  if (parameters.frequency_offset) {
    primary_.RenderLinearFm(
        primary_f,
        primary_pw,
        primary_shape,
        parameters.frequency_offset,
        temp_buffer_,
        size,
        PLAITS_HARD_SYNC_EVENTS(parameters));
  } else {
#endif
  primary_.Render(
      primary_f,
      primary_pw,
      primary_shape,
      temp_buffer_,
      size,
      PLAITS_HARD_SYNC_EVENTS(parameters));
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  }
#endif
  // OUT is scratch until the final mix. Keeping the normal secondary here
  // leaves AUX available for the synchronized secondary in mono.
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  if (parameters.frequency_offset) {
    auxiliary_.RenderLinearFm(
        auxiliary_f,
        auxiliary_pw,
        auxiliary_shape,
        auxiliary_frequency_offset,
        out,
        size,
        PLAITS_HARD_SYNC_EVENTS(parameters));
  } else {
#endif
  auxiliary_.Render(
      auxiliary_f,
      auxiliary_pw,
      auxiliary_shape,
      out,
      size,
      PLAITS_HARD_SYNC_EVENTS(parameters));
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  }
#endif

  if (PLAITS_STEREO_VIRTUAL_ANALOG_DUAL && parameters.stereo) {
    // Both channels are complementary views of the same original 50/50 mix.
    // Their mono sum is unchanged; all four controls remain active.
    const float left_secondary = 0.5f - kStereoWidth;
    const float right_secondary = 0.5f + kStereoWidth;
    for (size_t i = 0; i < size; ++i) {
      const float primary = temp_buffer_[i];
      const float secondary = out[i];
      out[i] = primary + (secondary - primary) * left_secondary;
      aux[i] = primary + (secondary - primary) * right_secondary;
    }
  } else {
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      sync_.RenderLinearFm<true, false>(
          primary_f,
          sync_f,
          auxiliary_pw,
          auxiliary_shape,
          0.0f,
          parameters.frequency_offset,
          sync_frequency_offset,
          aux,
          size,
          PLAITS_HARD_SYNC_EVENTS(parameters));
    } else {
#endif
    sync_.Render(
        primary_f,
        sync_f,
        auxiliary_pw,
        auxiliary_shape,
        aux,
        size,
        PLAITS_HARD_SYNC_EVENTS(parameters));
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    }
#endif
    for (size_t i = 0; i < size; ++i) {
      const float primary = temp_buffer_[i];
      out[i] = (out[i] + primary) * 0.5f;
      aux[i] = (aux[i] + primary) * 0.5f;
    }
  }
}

}  // namespace plaits

#undef PLAITS_HARD_SYNC_EVENTS
