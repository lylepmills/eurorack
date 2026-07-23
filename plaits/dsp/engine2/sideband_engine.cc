// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Finite discrete-summation-formula sideband engine.

#include "plaits/dsp/engine2/sideband_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

inline float WrapPhase(float phase) {
  return phase - static_cast<int>(phase);
}

struct DsfParameters {
  float rolloff;
  float rolloff_n;
  float weight_sum;
  int count;
};

DsfParameters PrepareDsf(float rolloff, int count) {
  DsfParameters parameters;
  parameters.rolloff = rolloff;
  parameters.rolloff_n = 1.0f;
  parameters.count = count;
  for (int i = 0; i < count; ++i) {
    parameters.rolloff_n *= rolloff;
  }
  parameters.weight_sum = (1.0f - parameters.rolloff_n) / \
      (1.0f - rolloff);
  return parameters;
}

// Normalized finite geometric sinusoid bank:
//   sum(k=0..count-1, rolloff^k * sin(carrier + direction * k * band))
// The closed form costs a fixed amount regardless of partial count. Rolloff is
// capped away from one by the caller, and the explicit denominator guard keeps
// the cancellation case finite.
float FiniteDsf(
    float carrier_sin,
    float carrier_cos,
    float band_sin,
    float band_cos,
    float band_phase,
    const DsfParameters& parameters,
    float direction) {
  const float signed_band_sin = direction * band_sin;
  const float n_phase = WrapPhase(
      band_phase * static_cast<float>(parameters.count));
  const float n_sin = direction * SineNoWrap(n_phase);
  const float n_cos = SineNoWrap(n_phase + 0.25f);

  const float denominator = max(
      1.0e-5f,
      1.0f - 2.0f * parameters.rolloff * band_cos + \
      parameters.rolloff * parameters.rolloff);
  const float a = 1.0f - parameters.rolloff_n * n_cos;
  const float c = 1.0f - parameters.rolloff * band_cos;
  const float d = parameters.rolloff * signed_band_sin;
  const float real = (a * c + \
      parameters.rolloff_n * n_sin * d) / denominator;
  const float imaginary = (a * d - \
      parameters.rolloff_n * n_sin * c) / denominator;
  return (carrier_sin * real + carrier_cos * imaginary) / \
      parameters.weight_sum;
}

int GuardPartialCount(
    float carrier_frequency,
    float sideband_frequency,
    int requested,
    float direction) {
  while (requested > 1) {
    const float last_frequency = fabsf(
        carrier_frequency + direction * \
        static_cast<float>(requested - 1) * sideband_frequency);
    if (last_frequency <= 0.24f) {
      break;
    }
    --requested;
  }
  return requested;
}

}  // namespace

void SidebandEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void SidebandEngine::Reset() {
  carrier_phase_ = 0.0f;
  sideband_phase_ = 0.0f;
}

void SidebandEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    carrier_phase_ = 0.0f;
    sideband_phase_ = 0.0f;
  }

  const float carrier_frequency = min(
      0.24f, NoteToFrequency(parameters.note));
  const float spacing = 0.125f + 2.875f * \
      parameters.harmonics * parameters.harmonics;
  const float sideband_frequency = carrier_frequency * spacing;
  const int requested_count = 1 + static_cast<int>(
      23.999f * parameters.morph);
  const int upper_count = GuardPartialCount(
      carrier_frequency, sideband_frequency, requested_count, 1.0f);
  const int lower_count = GuardPartialCount(
      carrier_frequency, sideband_frequency, requested_count, -1.0f);

  const float base_rolloff = 0.08f + 0.88f * \
      parameters.timbre * parameters.timbre;
  const float asymmetry = 0.3f * (2.0f * parameters.macro - 1.0f);
  const float upper_rolloff = min(
      0.96f, base_rolloff * (1.0f + asymmetry));
  const float lower_rolloff = min(
      0.96f, base_rolloff * (1.0f - asymmetry));
  const DsfParameters upper_parameters = PrepareDsf(
      upper_rolloff, upper_count);
  const DsfParameters lower_parameters = PrepareDsf(
      lower_rolloff, lower_count);

  for (size_t i = 0; i < size; ++i) {
    carrier_phase_ += carrier_frequency;
    carrier_phase_ -= static_cast<int>(carrier_phase_);
    sideband_phase_ += sideband_frequency;
    sideband_phase_ -= static_cast<int>(sideband_phase_);

    const float carrier_sin = SineNoWrap(carrier_phase_);
    const float carrier_cos = SineNoWrap(carrier_phase_ + 0.25f);
    const float band_sin = SineNoWrap(sideband_phase_);
    const float band_cos = SineNoWrap(sideband_phase_ + 0.25f);
    out[i] = 0.86f * FiniteDsf(
        carrier_sin,
        carrier_cos,
        band_sin,
        band_cos,
        sideband_phase_,
        upper_parameters,
        1.0f);
    aux[i] = 0.86f * FiniteDsf(
        carrier_sin,
        carrier_cos,
        band_sin,
        band_cos,
        sideband_phase_,
        lower_parameters,
        -1.0f);
  }

  if (parameters.stereo) {
    // OUT/AUX become L/R: both channels carry the full spectrum, with a
    // complementary 85/15 power split between the upper and lower banks.
    const float a = Sqrt(0.85f);
    const float b = Sqrt(0.15f);
    for (size_t i = 0; i < size; ++i) {
      const float upper = out[i];
      const float lower = aux[i];
      out[i] = a * upper + b * lower;
      aux[i] = b * upper + a * lower;
    }
  }
}

}  // namespace plaits
