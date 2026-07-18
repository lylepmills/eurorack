// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Normalized feedback-amplitude-modulation engine.

#include "plaits/dsp/engine2/loopback_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void LoopbackEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void LoopbackEngine::ClearDelay() {
  fill(&delay_line_[0], &delay_line_[kDelaySize], 0.0f);
  write_index_ = 0;
}

void LoopbackEngine::Reset() {
  ClearDelay();
  carrier_phase_ = 0.0f;
  feedback_phase_ = 0.0f;
  ratio_ = 1.0f;
  depth_ = 0.0f;
  delay_ = 0.0f;
  polarity_ = 0.0f;
}

float LoopbackEngine::ReadDelay(float delay) const {
  const int integral = static_cast<int>(delay);
  const float fractional = delay - static_cast<float>(integral);
  int index_a = write_index_ - integral - 1;
  if (index_a < 0) {
    index_a += kDelaySize;
  }
  int index_b = index_a - 1;
  if (index_b < 0) {
    index_b += kDelaySize;
  }
  const float a = delay_line_[index_a];
  return a + (delay_line_[index_b] - a) * fractional;
}

void LoopbackEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    ClearDelay();
    carrier_phase_ = 0.0f;
    feedback_phase_ = 0.0f;
  }

  const float frequency = min(0.24f, NoteToFrequency(parameters.note));
  const float ratio = 0.5f + 7.5f * \
      parameters.harmonics * parameters.harmonics;
  const float depth = 0.98f * parameters.timbre * parameters.timbre;
  const float delay = static_cast<float>(kDelaySize - 2) * \
      parameters.morph * parameters.morph;
  const float polarity = 2.0f * parameters.macro - 1.0f;

  ParameterInterpolator ratio_modulation(&ratio_, ratio, size);
  ParameterInterpolator depth_modulation(&depth_, depth, size);
  ParameterInterpolator delay_modulation(&delay_, delay, size);
  ParameterInterpolator polarity_modulation(&polarity_, polarity, size);

  for (size_t i = 0; i < size; ++i) {
    const float current_ratio = ratio_modulation.Next();
    const float current_depth = depth_modulation.Next();
    const float current_delay = delay_modulation.Next();
    const float current_polarity = polarity_modulation.Next();

    carrier_phase_ += frequency;
    carrier_phase_ -= static_cast<int>(carrier_phase_);
    feedback_phase_ += min(0.24f, frequency * current_ratio);
    feedback_phase_ -= static_cast<int>(feedback_phase_);

    const float carrier = SineNoWrap(carrier_phase_);
    const float feedback_carrier = SineNoWrap(feedback_phase_);
    const float delayed = ReadDelay(current_delay);
    const float magnitude = fabsf(delayed);
    float shaped_feedback;
    if (current_polarity < 0.0f) {
      shaped_feedback = delayed + (-magnitude - delayed) * \
          -current_polarity;
    } else {
      shaped_feedback = delayed + (magnitude - delayed) * \
          current_polarity;
    }

    // Dividing by 1 + depth makes [-1, 1] an invariant range for the
    // recursive state. This remains stable even at maximum feedback.
    const float normalization = 1.0f / (1.0f + current_depth);
    const float envelope = (1.0f + \
        current_depth * shaped_feedback) * normalization;
    float recursive = feedback_carrier * envelope;
    CONSTRAIN(recursive, -1.0f, 1.0f);
    delay_line_[write_index_] = recursive;
    write_index_ = (write_index_ + 1) & (kDelaySize - 1);

    const float main = carrier * envelope;
    const float sidebands = carrier * current_depth * \
        shaped_feedback * normalization;
    out[i] = 0.9f * main;
    aux[i] = 1.55f * sidebands;
  }
}

}  // namespace plaits
