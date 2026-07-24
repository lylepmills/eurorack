// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Undertone and subharmonic mixture engine.

#include "plaits/dsp/engine2/undertow_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const int kNumUndertowRegistrations = 5;

// Columns are the pitch anchor followed by divisors 2 through 6. These are
// original registrations chosen to cover octave, odd, dense, and pedal
// families without cloning the stop layout of a particular module.
const float kUndertowRegistration[kNumUndertowRegistrations]
    [kNumUndertowVoices] = {
  { 0.80f, 0.70f, 0.00f, 0.22f, 0.00f, 0.00f },
  { 0.42f, 0.82f, 0.08f, 0.58f, 0.00f, 0.34f },
  { 0.34f, 0.12f, 0.82f, 0.08f, 0.66f, 0.00f },
  { 0.30f, 0.72f, 0.58f, 0.48f, 0.38f, 0.28f },
  { 0.28f, 0.24f, 0.34f, 0.62f, 0.54f, 0.86f }
};

// Fixed stereo positions, indexed like the voices: the anchor first, then
// divisors 2 through 6. The anchor — often the loudest component — and the
// deepest undertone hold the centre so the image cannot lean with the
// registration; the midrange undertones alternate sides and carry the width.
const float kUndertowVoicePan[kNumUndertowVoices] = {
  0.5f, 0.12f, 0.88f, 0.22f, 0.78f, 0.42f
};

inline float WrapPhase(float phase) {
  if (phase >= 1.0f) {
    phase -= 1.0f;
  }
  return phase;
}

// Two-point polynomial BLEP for the discontinuities of the saw and pulse
// components. Each undertone is lower than the pitch anchor, so this modest
// correction is sufficient and substantially cheaper than oversampling six
// oscillators.
inline float PolyBlep(float phase, float frequency) {
  if (phase < frequency) {
    const float t = phase / frequency;
    return t + t - t * t - 1.0f;
  }
  if (phase > 1.0f - frequency) {
    const float t = (phase - 1.0f) / frequency;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

inline float UndertoneWave(
    float phase,
    float frequency,
    float colour) {
  const float triangle = 1.0f - 4.0f * fabsf(phase - 0.5f);
  const float saw = 2.0f * phase - 1.0f - PolyBlep(phase, frequency);

  float pulse_width = 0.5f - 0.36f * colour * colour;
  CONSTRAIN(pulse_width, 2.0f * frequency, 1.0f - 2.0f * frequency);
  float pulse_phase = phase - pulse_width;
  if (pulse_phase < 0.0f) {
    pulse_phase += 1.0f;
  }
  const float pulse = (phase < pulse_width ? 1.0f : -1.0f) + \
      PolyBlep(phase, frequency) - PolyBlep(pulse_phase, frequency);

  if (colour < 0.5f) {
    return triangle + (saw - triangle) * colour * 2.0f;
  }
  return saw + (pulse - saw) * (colour * 2.0f - 1.0f);
}

}  // namespace

void UndertowEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void UndertowEngine::Reset() {
  for (int i = 0; i < kNumUndertowVoices; ++i) {
    phase_[i] = 0.0f;
    frequency_[i] = 0.001f;
    main_amplitude_[i] = 0.0f;
    aux_amplitude_[i] = 0.0f;
  }
  colour_ = 0.5f;
}

void UndertowEngine::ComputeRegistration(
    float registration,
    float* amplitude) const {
  registration *= static_cast<float>(kNumUndertowRegistrations - 1);
  int integral = static_cast<int>(registration);
  if (integral >= kNumUndertowRegistrations - 1) {
    integral = kNumUndertowRegistrations - 2;
    registration = static_cast<float>(kNumUndertowRegistrations - 1);
  }
  const float fractional = registration - static_cast<float>(integral);
  for (int i = 0; i < kNumUndertowVoices; ++i) {
    const float a = kUndertowRegistration[integral][i];
    const float b = kUndertowRegistration[integral + 1][i];
    amplitude[i] = a + (b - a) * fractional;
  }
}

void UndertowEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // All integer dividers begin at the same crossing, giving triggered notes
    // a repeatable pitch-anchored attack while free-running notes retain phase.
    fill(&phase_[0], &phase_[kNumUndertowVoices], 0.0f);
  }

  float registration[kNumUndertowVoices];
  ComputeRegistration(parameters.harmonics, registration);

  float main_target[kNumUndertowVoices];
  float aux_target[kNumUndertowVoices];
  float main_sum = 0.0f;
  float aux_sum = 0.0f;
  for (int i = 0; i < kNumUndertowVoices; ++i) {
    const float balance = i == 0
        ? 1.45f - 0.65f * parameters.morph
        : 0.32f + 1.08f * parameters.morph;
    main_target[i] = registration[i] * balance;
    main_sum += fabsf(main_target[i]);

    // AUX removes the anchor and alternates the divider polarities. It is a
    // raw, hollow lattice rather than a quieter copy of the main registration.
    const float polarity = (i & 1) ? 1.0f : -1.0f;
    aux_target[i] = i == 0 ? 0.0f : registration[i] * polarity;
    aux_sum += fabsf(aux_target[i]);
  }
  const float main_gain = main_sum > 0.0f ? 0.86f / main_sum : 0.0f;
  const float aux_gain = aux_sum > 0.0f ? 0.76f / aux_sum : 0.0f;

  const float anchor_frequency = min(
      0.24f, max(0.000001f, NoteToFrequency(parameters.note)));
  // The midpoint is an exact integer undertone lattice. The macro contracts
  // or expands its deep end, introducing slow beating without detuning the
  // audible pitch anchor.
  const float lattice_stretch = (parameters.macro - 0.5f) * 0.18f;
  const float colour_start = colour_;
  const float colour_increment = \
      (parameters.timbre - colour_) / static_cast<float>(size);

  for (int voice = 0; voice < kNumUndertowVoices; ++voice) {
    const float divisor = static_cast<float>(voice + 1);
    const float distance = divisor - 1.0f;
    const float warped_divisor = divisor + lattice_stretch * \
        distance * distance / static_cast<float>(kNumUndertowVoices - 1);
    const float target_frequency = anchor_frequency / warped_divisor;

    float out_channel_target = main_target[voice] * main_gain;
    float aux_channel_target = aux_target[voice] * aux_gain;
    if ((PLAITS_STEREO_UNDERTOW && parameters.stereo)) {
      // OUT/AUX become L/R: the voice keeps its registration level, split by
      // equal-power pan gains. The alternating-polarity lattice is skipped so
      // that both channels stay mono-compatible.
      float pan_left;
      float pan_right;
      StereoPanGains(kUndertowVoicePan[voice], &pan_left, &pan_right);
      aux_channel_target = out_channel_target * pan_right;
      out_channel_target *= pan_left;
    }
    ParameterInterpolator frequency_modulation(
        &frequency_[voice], target_frequency, size);
    ParameterInterpolator main_modulation(
        &main_amplitude_[voice], out_channel_target, size);
    ParameterInterpolator aux_modulation(
        &aux_amplitude_[voice], aux_channel_target, size);

    for (size_t i = 0; i < size; ++i) {
      const float frequency = frequency_modulation.Next();
      phase_[voice] = WrapPhase(phase_[voice] + frequency);
      const float colour = colour_start + \
          colour_increment * static_cast<float>(i + 1);
      const float sample = UndertoneWave(
          phase_[voice], frequency, colour);
      out[i] += sample * main_modulation.Next();
      aux[i] += sample * aux_modulation.Next();
    }
  }
  colour_ = parameters.timbre;
}

}  // namespace plaits
