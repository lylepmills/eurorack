// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Mean-field coupled phase-oscillator synthesis engine.

#include "plaits/dsp/engine2/phase_flock_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/units.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const float kFlockDetuning[kNumPhaseFlockOscillators] = {
  -1.0f, -0.63f, -0.31f, -0.07f, 0.19f, 0.51f, 0.86f
};

const float kFlockInitialPhase[kNumPhaseFlockOscillators] = {
  0.031f, 0.647f, 0.287f, 0.903f, 0.491f, 0.154f, 0.778f
};

inline float Wrap(float phase) {
  phase -= static_cast<int>(phase);
  if (phase < 0.0f) {
    phase += 1.0f;
  }
  return phase;
}

inline float LimitAudio(float sample) {
  // Gentle rational saturation keeps newly synchronized flocks from jumping
  // in level while preserving the quiet cancellations of dispersed phases.
  sample /= 1.0f + 0.30f * fabsf(sample);
  CONSTRAIN(sample, -1.0f, 1.0f);
  return sample;
}

}  // namespace

void PhaseFlockEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void PhaseFlockEngine::Reset() {
  scatter_count_ = 0;
  reset_pending_ = true;
  for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
    phase_[i] = 0.0f;
  }
}

void PhaseFlockEngine::Scatter() {
  ++scatter_count_;
  const float turn = 0.083f * static_cast<float>(scatter_count_ & 7);
  for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
    phase_[i] = Wrap(
        kFlockInitialPhase[i] + turn * static_cast<float>(i + 1));
  }
}

void PhaseFlockEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (reset_pending_ || (parameters.trigger & TRIGGER_RISING_EDGE)) {
    Scatter();
    reset_pending_ = false;
  }

  const float base_frequency = min(0.20f, NoteToFrequency(parameters.note));
  const float spread = 14.0f * parameters.harmonics * \
      parameters.harmonics;
  float natural_frequency[kNumPhaseFlockOscillators];
  for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
    natural_frequency[i] = min(
        0.23f,
        base_frequency * SemitonesToRatio(spread * kFlockDetuning[i]));
  }

  const float coupling = min(
      0.045f,
      base_frequency * 1.8f * parameters.timbre * parameters.timbre);
  const float cluster_mix = parameters.morph;
  // A phase lag turns perfect attraction into a travelling/frustrated flock.
  // Its exactly neutral midpoint matches the firmware's fourth-macro idiom.
  const float lag = (parameters.macro - 0.5f) * 0.90f;
  const float lag_normalization = 1.0f / (1.0f + fabsf(lag));
  const float normalization = 1.0f / \
      static_cast<float>(kNumPhaseFlockOscillators);

  for (size_t sample = 0; sample < size; ++sample) {
    float sine[kNumPhaseFlockOscillators];
    float cosine[kNumPhaseFlockOscillators];
    float sine_2[kNumPhaseFlockOscillators];
    float cosine_2[kNumPhaseFlockOscillators];
    float mean_sine = 0.0f;
    float mean_cosine = 0.0f;
    float mean_sine_2 = 0.0f;
    float mean_cosine_2 = 0.0f;

    for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
      const float s = SineNoWrap(phase_[i]);
      const float c = SineNoWrap(phase_[i] + 0.25f);
      sine[i] = s;
      cosine[i] = c;
      sine_2[i] = 2.0f * s * c;
      cosine_2[i] = c * c - s * s;
      mean_sine += s;
      mean_cosine += c;
      mean_sine_2 += sine_2[i];
      mean_cosine_2 += cosine_2[i];
    }
    mean_sine *= normalization;
    mean_cosine *= normalization;
    mean_sine_2 *= normalization;
    mean_cosine_2 *= normalization;

    for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
      // The first circular moment attracts all phases into one flock. The
      // second attracts two antipodal clusters, yet costs no extra sine lookup.
      const float attraction_1 = mean_sine * cosine[i] - \
          mean_cosine * sine[i];
      const float quadrature_1 = mean_cosine * cosine[i] + \
          mean_sine * sine[i];
      const float attraction_2 = mean_sine_2 * cosine_2[i] - \
          mean_cosine_2 * sine_2[i];
      const float quadrature_2 = mean_cosine_2 * cosine_2[i] + \
          mean_sine_2 * sine_2[i];
      const float attraction = attraction_1 + \
          (0.5f * attraction_2 - attraction_1) * cluster_mix;
      const float quadrature = quadrature_1 + \
          (0.5f * quadrature_2 - quadrature_1) * cluster_mix;
      const float correction = (attraction - lag * quadrature) * \
          lag_normalization;
      float increment = natural_frequency[i] + coupling * correction;
      CONSTRAIN(increment, 0.0f, 0.24f);
      phase_[i] = Wrap(phase_[i] + increment);
    }

    out[sample] = LimitAudio(mean_sine * 1.55f);
    // Two-cluster states can cancel at OUT while remaining loud in the second
    // circular moment, making AUX a genuinely complementary octave projection.
    aux[sample] = LimitAudio(mean_sine_2 * 1.35f);
  }
}

}  // namespace plaits
