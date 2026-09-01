// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Falling-pitch electronic percussion with crossed and ring-modulated topologies.

#include "plaits/dsp/engine2/circuit_zaps_engine.h"

#include <algorithm>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

inline float Noise() {
  return 2.0f * Random::GetFloat() - 1.0f;
}

inline float DecayCoefficient(float seconds) {
  return 1.0f - 1.0f / max(2.0f, seconds * kSampleRate);
}

inline float Triangle(float phase) {
  const float ramp = phase < 0.5f ? phase : 1.0f - phase;
  return 4.0f * ramp - 1.0f;
}

inline float SoftClip(float value, float drive) {
  const float x = value * drive;
  return x / (1.0f + fabsf(x));
}

}  // namespace

void CircuitZapsEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void CircuitZapsEngine::Reset() {
  phase_a_ = 0.0f;
  phase_b_ = 0.0f;
  body_envelope_ = 0.0f;
  sweep_ratio_ = 1.0f;
  spark_envelope_ = 0.0f;
  spark_lowpass_ = 0.0f;
  out_dc_ = 0.0f;
  aux_dc_ = 0.0f;
}

void CircuitZapsEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    const float accent = 0.30f + 0.70f * parameters.accent;
    body_envelope_ = accent;
    // Even the counter-clockwise end falls by a fifth. This keeps the voice
    // firmly in swept-percussion territory instead of duplicating a general
    // purpose analog tom at the bottom of the control.
    const float sweep_depth = 7.0f + 41.0f * parameters.timbre *
        parameters.timbre;
    sweep_ratio_ = SemitonesToRatio(sweep_depth);
    spark_envelope_ = accent;
    phase_a_ = 0.0f;
    phase_b_ = 0.0f;
  }

  const float base = min(0.12f, NoteToFrequency(parameters.note));
  const float topology = parameters.harmonics;
  const float ratio = 1.35f + 2.15f * topology * topology;
  const float decay_time = 0.025f + 1.20f * parameters.morph *
      parameters.morph;
  const float charge = parameters.macro * parameters.macro;
  const float body_decay = DecayCoefficient(decay_time);
  const float sweep_decay = DecayCoefficient(
      0.004f + 0.110f * parameters.timbre);
  const float spark_decay = DecayCoefficient(0.0008f + 0.026f * charge);
  const float drive = 1.2f + 7.0f * charge;

  for (size_t i = 0; i < size; ++i) {
    const float frequency_a = min(0.235f, base * sweep_ratio_);
    const float frequency_b = min(0.235f, frequency_a * ratio);
    phase_a_ += frequency_a;
    phase_a_ -= static_cast<int>(phase_a_);
    phase_b_ += frequency_b;
    phase_b_ -= static_cast<int>(phase_b_);

    const float sine = SineNoWrap(phase_a_);
    const float partner = 0.68f * SineNoWrap(phase_b_) +
        0.32f * Triangle(phase_b_);
    const float dual = 0.72f * sine + 0.42f * partner;
    const float ring = sine * partner;
    const float axis = topology * 2.0f;
    const float voice = axis < 1.0f
        ? sine + (dual - sine) * axis
        : dual + (ring - dual) * (axis - 1.0f);

    const float noise = Noise();
    spark_lowpass_ += 0.12f * (noise - spark_lowpass_);
    const float spark = (noise - spark_lowpass_) * spark_envelope_;
    const float clean = voice * body_envelope_;
    const float driven_input = clean + 0.68f * charge * spark;
    const float saturated = 1.45f * SoftClip(driven_input, drive);
    const float dirty = driven_input + charge * (saturated - driven_input);

    const float aux_sample = 0.72f * clean + 0.28f * spark;
    out_dc_ += 0.001f * (dirty - out_dc_);
    aux_dc_ += 0.001f * (aux_sample - aux_dc_);
    // The previous prototype hit full-scale for much of the upper Charge
    // range. This trim keeps the soft saturation audible without PCM clipping.
    out[i] = 0.62f * (dirty - out_dc_);
    aux[i] = aux_sample - aux_dc_;

    body_envelope_ *= body_decay;
    sweep_ratio_ += (1.0f - sweep_decay) * (1.0f - sweep_ratio_);
    spark_envelope_ *= spark_decay;
  }
}

}  // namespace plaits
