// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Complex frequency-shift feedback synthesis engine.

#include "plaits/dsp/engine2/spectral_spiral_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

inline float Wrap(float phase) {
  phase -= static_cast<int>(phase);
  if (phase < 0.0f) {
    phase += 1.0f;
  }
  return phase;
}

inline float PartialAttenuation(float frequency) {
  // Smoothly retire partials over the final useful audio octave.
  float gain = (0.24f - frequency) * 12.5f;
  CONSTRAIN(gain, 0.0f, 1.0f);
  return gain;
}

inline float LimitState(float sample) {
  // Unity is not normally reached because the loop is gain-normalized. These
  // rails make corrupt/restored state and extreme modulation safe as well.
  CONSTRAIN(sample, -1.2f, 1.2f);
  return sample;
}

inline float LimitAudio(float sample) {
  CONSTRAIN(sample, -1.0f, 1.0f);
  return sample;
}

}  // namespace

void SpectralSpiralEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void SpectralSpiralEngine::ClearLoop() {
  fill(&delay_i_[0], &delay_i_[kSpectralSpiralDelay], 0.0f);
  fill(&delay_q_[0], &delay_q_[kSpectralSpiralDelay], 0.0f);
  write_index_ = 0;
}

void SpectralSpiralEngine::Reset() {
  ClearLoop();
  source_phase_ = 0.0f;
  shift_phase_ = 0.0f;
  reset_pending_ = true;
}

void SpectralSpiralEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (reset_pending_ || (parameters.trigger & TRIGGER_RISING_EDGE)) {
    ClearLoop();
    source_phase_ = 0.0f;
    shift_phase_ = 0.0f;
    reset_pending_ = false;
  }

  const float source_frequency = min(
      0.20f, NoteToFrequency(parameters.note));
  const float richness = parameters.harmonics;
  float weight[4];
  weight[0] = PartialAttenuation(source_frequency);
  weight[1] = 0.58f * richness * \
      PartialAttenuation(source_frequency * 2.0f);
  weight[2] = 0.37f * richness * richness * \
      PartialAttenuation(source_frequency * 3.0f);
  weight[3] = 0.24f * richness * richness * richness * \
      PartialAttenuation(source_frequency * 5.0f);
  const float weight_sum = weight[0] + weight[1] + weight[2] + weight[3];
  const float source_normalization = weight_sum > 0.0f
      ? 1.0f / weight_sum
      : 0.0f;

  // TIMBRE sets the spacing between recirculated spectral copies. MACRO is a
  // bipolar direction control with an exactly stationary midpoint.
  const float shift_magnitude = 0.000015f + source_frequency * \
      (0.035f + 0.43f * parameters.timbre * parameters.timbre);
  const float shift_frequency = shift_magnitude * \
      (parameters.macro * 2.0f - 1.0f);
  // Keeping feedback strictly below unity makes the complex delay loop stable.
  const float feedback = 0.94f * parameters.morph * parameters.morph;
  const float injection = 0.76f * (1.0f - feedback);

  const int harmonic[4] = { 1, 2, 3, 5 };
  for (size_t i = 0; i < size; ++i) {
    source_phase_ = Wrap(source_phase_ + source_frequency);
    float source_i = 0.0f;
    float source_q = 0.0f;
    for (int partial = 0; partial < 4; ++partial) {
      const float phase = Wrap(
          source_phase_ * static_cast<float>(harmonic[partial]));
      source_i += weight[partial] * SineNoWrap(phase);
      source_q += weight[partial] * SineNoWrap(phase + 0.25f);
    }
    source_i *= source_normalization;
    source_q *= source_normalization;

    shift_phase_ = Wrap(shift_phase_ + shift_frequency);
    const float shift_sine = SineNoWrap(shift_phase_);
    const float shift_cosine = SineNoWrap(shift_phase_ + 0.25f);
    const float delayed_i = delay_i_[write_index_];
    const float delayed_q = delay_q_[write_index_];

    // The delayed analytic signal is multiplied by a complex oscillator before
    // returning to the loop. Each circulation therefore adds another signed
    // frequency shift, building a geometric ladder of moving partials.
    const float shifted_i = delayed_i * shift_cosine - \
        delayed_q * shift_sine;
    const float shifted_q = delayed_i * shift_sine + \
        delayed_q * shift_cosine;
    const float write_i = LimitState(
        injection * source_i + feedback * shifted_i);
    const float write_q = LimitState(
        injection * source_q + feedback * shifted_q);
    delay_i_[write_index_] = write_i;
    delay_q_[write_index_] = write_q;
    if (++write_index_ >= kSpectralSpiralDelay) {
      write_index_ = 0;
    }

    out[i] = LimitAudio(write_i * 1.05f);
    // AUX listens to the quadrature component of a counter-rotated return.
    // Quadrature source injection keeps it distinct even at the stationary
    // macro midpoint, while the opposite rotation emphasizes complementary
    // sideband motion once the shifter starts moving.
    const float counter_shift_q = -delayed_i * shift_sine + \
        delayed_q * shift_cosine;
    aux[i] = LimitAudio(
        (injection * source_q + feedback * counter_shift_q) * 1.05f);
  }
}

}  // namespace plaits
