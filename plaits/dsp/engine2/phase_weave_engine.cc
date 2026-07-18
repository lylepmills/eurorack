// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Phase-cancellation oscillator bank.

#include "plaits/dsp/engine2/phase_weave_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const int kPhaseWeavePartials = 8;

inline float WrapPhase(float phase) {
  while (phase < 0.0f) {
    phase += 1.0f;
  }
  while (phase >= 1.0f) {
    phase -= 1.0f;
  }
  return phase;
}

// Evaluate a compact, explicitly band-limited harmonic wave. Chebyshev
// recurrence makes each four-voice sample require one sine lookup per voice,
// rather than one lookup per partial.
inline float HarmonicWave(
    float phase,
    const float* partial_amplitude) {
  const float x = SineNoWrap(WrapPhase(phase));
  const float two_x = 2.0f * x;
  float previous = 1.0f;
  float current = x;
  float sum = 0.0f;
  for (int i = 0; i < kPhaseWeavePartials; ++i) {
    sum += partial_amplitude[i] * current;
    const float next = two_x * current - previous;
    previous = current;
    current = next;
  }
  return sum;
}

}  // namespace

void PhaseWeaveEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void PhaseWeaveEngine::Reset() {
  phase_ = 0.0f;
  constellation_ = 0.5f;
  spread_ = 0.0f;
  cancellation_ = 0.0f;
  rotation_ = 0.0f;
}

void PhaseWeaveEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
  }

  const float frequency = min(0.24f, NoteToFrequency(parameters.note));

  // HARMONICS changes both the geometry and the even/odd balance of the
  // source wave. The latter keeps this control audible when phase spread is
  // at its minimum.
  float partial_amplitude[kPhaseWeavePartials];
  float partial_sum = 0.0f;
  for (int i = 0; i < kPhaseWeavePartials; ++i) {
    const int harmonic = i + 1;
    const float anti_alias = max(
        0.0f, 1.0f - 2.0f * frequency * static_cast<float>(harmonic));
    const float parity = harmonic & 1
        ? 1.0f - 0.22f * parameters.harmonics
        : 0.35f + 0.65f * parameters.harmonics;
    partial_amplitude[i] = anti_alias * parity / \
        static_cast<float>(harmonic);
    partial_sum += fabsf(partial_amplitude[i]);
  }
  const float partial_gain = partial_sum > 0.0f
      ? 1.0f / partial_sum
      : 0.0f;
  for (int i = 0; i < kPhaseWeavePartials; ++i) {
    partial_amplitude[i] *= partial_gain;
  }

  const float target_spread = 0.48f * parameters.timbre * parameters.timbre;
  const float target_rotation = (parameters.macro - 0.5f) * 0.36f;
  ParameterInterpolator constellation_modulation(
      &constellation_, parameters.harmonics, size);
  ParameterInterpolator spread_modulation(&spread_, target_spread, size);
  ParameterInterpolator cancellation_modulation(
      &cancellation_, parameters.morph, size);
  ParameterInterpolator rotation_modulation(
      &rotation_, target_rotation, size);

  for (size_t i = 0; i < size; ++i) {
    const float constellation = constellation_modulation.Next();
    const float spread = spread_modulation.Next();
    const float cancellation = cancellation_modulation.Next();
    const float rotation = rotation_modulation.Next();

    phase_ += frequency;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }

    // This intentionally is not an evenly spaced saw bank. The asymmetric
    // constellation avoids a single dominant PWM null and gives HARMONICS a
    // continuous path between clustered, skewed, and near-quadrature layouts.
    float offset[4];
    offset[0] = 0.0f;
    offset[1] = spread * (0.28f + 0.28f * constellation) + rotation;
    offset[2] = spread * (-0.64f + 0.20f * constellation) - \
        0.63f * rotation;
    offset[3] = spread * (0.86f - 0.32f * constellation) + \
        0.37f * rotation;

    float voice[4];
    for (int j = 0; j < 4; ++j) {
      voice[j] = HarmonicWave(phase_ + offset[j], partial_amplitude);
    }

    // MORPH turns two lanes through zero into opposite-polarity cancellation.
    // The other two retain unequal weights, leaving useful sound at the
    // deepest nulls instead of collapsing to numerical silence.
    const float bipolar = 1.0f - 2.0f * cancellation;
    const float weight[4] = {
      1.0f,
      bipolar,
      0.72f,
      0.55f * bipolar
    };
    const float weight_sum = fabsf(weight[0]) + fabsf(weight[1]) + \
        fabsf(weight[2]) + fabsf(weight[3]);
    float main = 0.0f;
    for (int j = 0; j < 4; ++j) {
      main += voice[j] * weight[j];
    }
    main *= 0.92f / weight_sum;

    // AUX is the orthogonal pair-difference field. It emphasizes the partials
    // rejected by OUT and remains distinct throughout the MORPH sweep.
    const float difference = \
        0.58f * (voice[0] - voice[2]) + \
        0.42f * (voice[1] - voice[3]);

    // Moving away from the macro midpoint both rotates the constellation and
    // gently saturates the cancellation peaks. SoftClip provides a final hard
    // bound even when controls jump between blocks.
    const float drive = 1.0f + 11.111111f * fabsf(rotation);
    out[i] = 0.9f * SoftClip(main * drive);
    aux[i] = 0.72f * SoftClip(difference * (0.8f + 0.2f * drive));
  }
}

}  // namespace plaits
