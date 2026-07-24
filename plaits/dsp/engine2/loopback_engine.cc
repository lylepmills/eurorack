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

void LoopbackEngine::Reset() {
  carrier_phase_ = 0.0f;
  feedback_phase_ = 0.0f;
  feedback_state_ = 0.0f;
  fb_osc_ = 0.0f;
  ratio_ = 1.0f;
  depth_ = 0.0f;
  morph_ = 0.0f;
  polarity_ = 0.0f;
}

void LoopbackEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    feedback_state_ = 0.0f;
    fb_osc_ = 0.0f;
    carrier_phase_ = 0.0f;
    feedback_phase_ = 0.0f;
  }

  const float frequency = min(0.24f, NoteToFrequency(parameters.note));
  const float ratio = 0.5f + 7.5f * \
      parameters.harmonics * parameters.harmonics;
  const float depth = 0.98f * parameters.timbre * parameters.timbre;
  const float polarity = 2.0f * parameters.macro - 1.0f;

  // MORPH sets the phase-feedback index of the feedback oscillator.
  const float morph = kLoopbackFeedbackDepth * parameters.morph;

  ParameterInterpolator ratio_modulation(&ratio_, ratio, size);
  ParameterInterpolator depth_modulation(&depth_, depth, size);
  ParameterInterpolator morph_modulation(&morph_, morph, size);
  ParameterInterpolator polarity_modulation(&polarity_, polarity, size);

  // The AM carrier body and the sideband product are genuinely different
  // signals, so in stereo they are panned apart (main to 0.35, sidebands to
  // 0.65) while each keeps its own scale. Both components reach both channels
  // (equal-power), so a mono sum stays in phase.
  float main_left, main_right, sideband_left, sideband_right;
  StereoPanGains(0.35f, &main_left, &main_right);
  StereoPanGains(0.65f, &sideband_left, &sideband_right);

  for (size_t i = 0; i < size; ++i) {
    const float current_ratio = ratio_modulation.Next();
    const float current_depth = depth_modulation.Next();
    const float current_morph = morph_modulation.Next();
    const float current_polarity = polarity_modulation.Next();

    carrier_phase_ += frequency;
    carrier_phase_ -= static_cast<int>(carrier_phase_);
    feedback_phase_ += min(0.24f, frequency * current_ratio);
    feedback_phase_ -= static_cast<int>(feedback_phase_);

    const float carrier = SineNoWrap(carrier_phase_);
    // The feedback oscillator phase-modulates itself: as MORPH raises the
    // index, its sine progressively gains harmonics, so the signal that
    // regenerates through the loop carries real spectral variety instead of
    // the single partial the tilt filter had to work with.
    float modulated_phase = feedback_phase_ + current_morph * fb_osc_;
    modulated_phase -= floorf(modulated_phase);
    const float feedback_carrier = SineNoWrap(modulated_phase);
    fb_osc_ = feedback_carrier;

    const float magnitude = fabsf(feedback_state_);
    float shaped_feedback;
    if (current_polarity < 0.0f) {
      shaped_feedback = feedback_state_ + (-magnitude - feedback_state_) * \
          -current_polarity;
    } else {
      shaped_feedback = feedback_state_ + (magnitude - feedback_state_) * \
          current_polarity;
    }

    // Dividing by 1 + depth makes [-1, 1] an invariant range for the
    // recursive state. This remains stable even at maximum feedback.
    const float normalization = 1.0f / (1.0f + current_depth);
    const float envelope = (1.0f + \
        current_depth * shaped_feedback) * normalization;
    float recursive = feedback_carrier * envelope;
    CONSTRAIN(recursive, -1.0f, 1.0f);
    feedback_state_ = recursive;

    const float main = carrier * envelope;
    const float sidebands = carrier * current_depth * \
        shaped_feedback * normalization;
    if ((PLAITS_STEREO_LOOPBACK && parameters.stereo)) {
      const float body = 0.9f * main;
      const float sideband_signal = 1.55f * sidebands;
      out[i] = body * main_left + sideband_signal * sideband_left;
      aux[i] = body * main_right + sideband_signal * sideband_right;
    } else {
      out[i] = 0.9f * main;
      aux[i] = 1.55f * sidebands;
    }
  }
}

}  // namespace plaits
