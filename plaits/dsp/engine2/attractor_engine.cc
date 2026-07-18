// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Three-state cyclic attractor synthesis engine.

#include "plaits/dsp/engine2/attractor_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const float kInverseTwoPi = 0.159154943f;

inline float StateSine(float state) {
  // Sine() expects a non-negative table phase. The large integral offset is
  // inaudible and keeps even the clamped attractor states in range.
  return Sine(8.0f + state * kInverseTwoPi);
}

inline float ShapeCoordinate(float value) {
  return value / (1.0f + fabsf(value));
}

inline float LimitAudio(float sample) {
  CONSTRAIN(sample, -1.0f, 1.0f);
  return sample;
}

}  // namespace

void AttractorEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void AttractorEngine::Reset() {
  state_[0] = 0.0f;
  state_[1] = 0.0f;
  state_[2] = 0.0f;
  dc_[0] = 0.0f;
  dc_[1] = 0.0f;
  dc_[2] = 0.0f;
  reset_pending_ = true;
}

void AttractorEngine::Seed(float accent) {
  // Avoid the symmetric fixed point while remaining fully deterministic.
  state_[0] = 0.17f + 0.21f * accent;
  state_[1] = -0.11f;
  state_[2] = 0.07f - 0.13f * accent;
}

void AttractorEngine::Step(
    float rate_x,
    float rate_y,
    float rate_z,
    float damping,
    float argument_scale) {
  // A controlled variation of the cyclically symmetric Thomas flow:
  //   x' = sin(y) - b*x, y' = sin(z) - b*y, z' = sin(x) - b*z.
  // Using the old state for all three derivatives preserves the cyclic
  // symmetry. Damping and hard state bounds make every control position safe.
  const float x = state_[0];
  const float y = state_[1];
  const float z = state_[2];
  state_[0] += rate_x * (StateSine(y * argument_scale) - damping * x);
  state_[1] += rate_y * (StateSine(z * argument_scale) - damping * y);
  state_[2] += rate_z * (StateSine(x * argument_scale) - damping * z);

  // The continuous system is bounded by its damping term. These wider safety
  // rails also cover coarse Euler steps at the top of the keyboard.
  CONSTRAIN(state_[0], -7.0f, 7.0f);
  CONSTRAIN(state_[1], -7.0f, 7.0f);
  CONSTRAIN(state_[2], -7.0f, 7.0f);
}

void AttractorEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (reset_pending_ || (parameters.trigger & TRIGGER_RISING_EDGE)) {
    Seed(parameters.accent);
    reset_pending_ = false;
  }

  // The flow's nominal orbit takes roughly one 2-pi traversal. Pitch therefore
  // controls integration distance rather than an unrelated readout oscillator.
  // Very high notes compress gracefully where a coarse explicit solver would
  // otherwise turn the intended attractor into numerical instability.
  const float base_rate = min(
      0.62f, NoteToFrequency(parameters.note) * 6.283185307f);
  const float skew = (parameters.macro - 0.5f) * 0.78f;
  const float rate_x = base_rate * (1.0f + skew);
  const float rate_y = base_rate;
  const float rate_z = base_rate * (1.0f - skew);

  // TIMBRE moves from a damped limit cycle into broad chaotic excursions.
  const float damping = 0.355f - 0.185f * parameters.timbre;
  // HARMONICS changes how many lobes of the sine coupling are visited.
  const float argument_scale = 0.68f + \
      0.92f * parameters.harmonics * parameters.harmonics;

  for (size_t i = 0; i < size; ++i) {
    Step(rate_x, rate_y, rate_z, damping, argument_scale);

    float coordinate[3];
    for (int j = 0; j < 3; ++j) {
      const float shaped = ShapeCoordinate(state_[j]);
      dc_[j] += 0.00022f * (shaped - dc_[j]);
      coordinate[j] = shaped - dc_[j];
    }

    // MORPH traverses x -> y -> z. AUX follows the same orbit one coordinate
    // behind, so the outputs remain distinct without inventing a conventional
    // oscillator that would mask the attractor's dynamics.
    const float selector = parameters.morph * 2.0f;
    float main_coordinate;
    float aux_coordinate;
    if (selector < 1.0f) {
      main_coordinate = coordinate[0] + \
          (coordinate[1] - coordinate[0]) * selector;
      aux_coordinate = coordinate[2] + \
          (coordinate[0] - coordinate[2]) * selector;
    } else {
      const float fraction = selector - 1.0f;
      main_coordinate = coordinate[1] + \
          (coordinate[2] - coordinate[1]) * fraction;
      aux_coordinate = coordinate[0] + \
          (coordinate[1] - coordinate[0]) * fraction;
    }
    // The bounded attractor spends much of its time near the origin at tame
    // settings. Bring that motion forward while retaining hard safety rails
    // for the broader chaotic orbits.
    out[i] = LimitAudio(0.95f * SoftClip(main_coordinate * 3.0f));
    aux[i] = LimitAudio(0.95f * SoftClip(aux_coordinate * 3.0f));
  }
}

}  // namespace plaits
