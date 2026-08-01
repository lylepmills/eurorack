// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Two swept oscillators covering round electronic toms and zapping percussion.

#include "plaits/dsp/engine2/circuit_toms_engine.h"

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

void CircuitTomsEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void CircuitTomsEngine::Reset() {
  phase_a_ = 0.0f;
  phase_b_ = 0.0f;
  body_envelope_ = 0.0f;
  sweep_ratio_ = 1.0f;
  noise_envelope_ = 0.0f;
  noise_lowpass_ = 0.0f;
  out_dc_ = 0.0f;
  aux_dc_ = 0.0f;
}

void CircuitTomsEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    const float accent = 0.30f + 0.70f * parameters.accent;
    body_envelope_ = accent;
    const float sweep_depth = 1.0f + 35.0f * parameters.timbre *
        parameters.timbre;
    sweep_ratio_ = SemitonesToRatio(sweep_depth);
    noise_envelope_ = accent;
    phase_a_ = 0.0f;
    phase_b_ = 0.0f;
  }

  const float base = min(0.12f, NoteToFrequency(parameters.note));
  const float circuit = parameters.harmonics;
  const float ratio = 1.0f + 1.5f * circuit * circuit;
  const float decay_time = 0.045f + 1.55f * parameters.morph *
      parameters.morph;
  const float crack = parameters.macro * parameters.macro;
  const float body_decay = DecayCoefficient(decay_time);
  const float sweep_decay = DecayCoefficient(
      0.006f + 0.075f * parameters.timbre);
  const float noise_decay = DecayCoefficient(0.001f + 0.035f * crack);
  const float drive = 1.2f + 7.0f * crack;

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
    const float axis = circuit * 2.0f;
    const float voice = axis < 1.0f
        ? sine + (dual - sine) * axis
        : dual + (ring - dual) * (axis - 1.0f);

    const float noise = Noise();
    noise_lowpass_ += 0.12f * (noise - noise_lowpass_);
    const float attack = (noise - noise_lowpass_) * noise_envelope_;
    const float clean = voice * body_envelope_;
    const float driven_input = clean + 0.75f * crack * attack;
    const float saturated = 1.7f * SoftClip(driven_input, drive);
    const float dirty = driven_input + crack * (saturated - driven_input);

    const float aux_sample = 0.72f * clean + 0.28f * attack;
    out_dc_ += 0.001f * (dirty - out_dc_);
    aux_dc_ += 0.001f * (aux_sample - aux_dc_);
    out[i] = 0.78f * (dirty - out_dc_);
    aux[i] = aux_sample - aux_dc_;

    body_envelope_ *= body_decay;
    sweep_ratio_ += (1.0f - sweep_decay) * (1.0f - sweep_ratio_);
    noise_envelope_ *= noise_decay;
  }
}

}  // namespace plaits
