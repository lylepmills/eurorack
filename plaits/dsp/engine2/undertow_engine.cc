// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Undertone and subharmonic mixture engine.

#include "plaits/dsp/engine2/undertow_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/dsp.h"
#include "stmlib/dsp/polyblep.h"

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

inline float TriangleSawWave(
    float phase,
    float frequency,
    float mix) {
  const float triangle = 1.0f - 4.0f * fabsf(phase - 0.5f);
  const float saw = 2.0f * phase - 1.0f - PolyBlep(phase, frequency);
  return triangle + (saw - triangle) * mix;
}

inline float BandlimitedSawPulseWave(
    float phase,
    float frequency,
    float mix,
    float pulse_width,
    float previous_pulse_width,
    bool wrapped,
    bool* pulse_high,
    float* next_blep) {
  const float saw = 2.0f * phase - 1.0f;
  float this_blep = *next_blep;
  *next_blep = 0.0f;

  if (wrapped) {
    const float t = phase / frequency;
    // Saw falls by 2 at wrap while pulse rises by 2.
    const float discontinuity = -2.0f + 4.0f * mix;
    this_blep += discontinuity * ThisBlepSample(t);
    *next_blep += discontinuity * NextBlepSample(t);
    *pulse_high = true;
  }

  if (*pulse_high && phase >= pulse_width) {
    const float denominator = \
        previous_pulse_width - pulse_width + frequency;
    const float t = (phase - pulse_width) / denominator;
    const float discontinuity = -2.0f * mix;
    this_blep += discontinuity * ThisBlepSample(t);
    *next_blep += discontinuity * NextBlepSample(t);
    *pulse_high = false;
  }
  const float pulse = *pulse_high ? 1.0f : -1.0f;
  return saw + (pulse - saw) * mix + this_blep;
}

__attribute__((noinline)) void RenderTriangleSawBlock(
    size_t begin,
    size_t end,
    float colour_start,
    float colour_increment,
    float* phase,
    float* frequency,
    const float* frequency_increment,
    float* main_amplitude,
    const float* main_increment,
    float* aux_amplitude,
    const float* aux_increment,
    float* out,
    float* aux) {
  float colour = colour_start + colour_increment * static_cast<float>(begin);
  for (size_t i = begin; i < end; ++i) {
    colour += colour_increment;
    const float wave_mix = colour * 2.0f;
    float out_sum = 0.0f;
    float aux_sum = 0.0f;
    for (int voice = 0; voice < kNumUndertowVoices; ++voice) {
      frequency[voice] += frequency_increment[voice];
      main_amplitude[voice] += main_increment[voice];
      aux_amplitude[voice] += aux_increment[voice];
      phase[voice] = WrapPhase(phase[voice] + frequency[voice]);
      const float sample = TriangleSawWave(
          phase[voice], frequency[voice], wave_mix);
      out_sum += sample * main_amplitude[voice];
      aux_sum += sample * aux_amplitude[voice];
    }
    out[i] = out_sum;
    aux[i] = aux_sum;
  }
}

__attribute__((noinline)) void RenderSawPulseBlock(
    size_t begin,
    size_t end,
    float colour_start,
    float colour_increment,
    bool entering_pulse,
    float* phase,
    float* frequency,
    const float* frequency_increment,
    float* main_amplitude,
    const float* main_increment,
    float* aux_amplitude,
    const float* aux_increment,
    float* pulse_width_state,
    bool* pulse_high,
    float* next_blep,
    float* out,
    float* aux) {
  float colour = colour_start + colour_increment * static_cast<float>(begin);
  for (size_t i = begin; i < end; ++i) {
    colour += colour_increment;
    const float wave_mix = colour * 2.0f - 1.0f;
    const float pulse_width = 0.5f - 0.36f * colour * colour;
    float out_sum = 0.0f;
    float aux_sum = 0.0f;
    // Correct the two brightest voices at their discontinuities. The remaining
    // integer dividers are at one third of the anchor frequency or below.
    for (int voice = 0; voice < 2; ++voice) {
      frequency[voice] += frequency_increment[voice];
      main_amplitude[voice] += main_increment[voice];
      aux_amplitude[voice] += aux_increment[voice];
      float next_phase = phase[voice] + frequency[voice];
      const bool wrapped = next_phase >= 1.0f;
      phase[voice] = WrapPhase(next_phase);
      // The raw width never exceeds 0.5 and frequency is capped below 0.25,
      // so only the lower anti-aliasing bound can become active.
      const float voice_pulse_width = max(
          pulse_width, 2.0f * frequency[voice]);
      if (entering_pulse && i == begin) {
        pulse_high[voice] = phase[voice] < voice_pulse_width;
        pulse_width_state[voice] = voice_pulse_width;
        next_blep[voice] = 0.0f;
      }
      const float sample = BandlimitedSawPulseWave(
          phase[voice],
          frequency[voice],
          wave_mix,
          voice_pulse_width,
          pulse_width_state[voice],
          wrapped,
          &pulse_high[voice],
          &next_blep[voice]);
      pulse_width_state[voice] = voice_pulse_width;
      out_sum += sample * main_amplitude[voice];
      aux_sum += sample * aux_amplitude[voice];
    }

    for (int voice = 2; voice < kNumUndertowVoices; ++voice) {
      frequency[voice] += frequency_increment[voice];
      main_amplitude[voice] += main_increment[voice];
      aux_amplitude[voice] += aux_increment[voice];
      phase[voice] = WrapPhase(phase[voice] + frequency[voice]);
      const float voice_pulse_width = max(
          pulse_width, 2.0f * frequency[voice]);
      const float saw = 2.0f * phase[voice] - 1.0f;
      const float pulse = phase[voice] < voice_pulse_width ? 1.0f : -1.0f;
      const float sample = saw + (pulse - saw) * wave_mix;
      out_sum += sample * main_amplitude[voice];
      aux_sum += sample * aux_amplitude[voice];
    }
    out[i] = out_sum;
    aux[i] = aux_sum;
  }
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
    next_blep_[i] = 0.0f;
    pulse_high_[i] = true;
    pulse_width_[i] = 0.41f;
  }
  colour_ = 0.5f;
  pulse_mode_ = false;
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
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // All integer dividers begin at the same crossing, giving triggered notes
    // a repeatable pitch-anchored attack while free-running notes retain phase.
    fill(&phase_[0], &phase_[kNumUndertowVoices], 0.0f);
    fill(&next_blep_[0], &next_blep_[kNumUndertowVoices], 0.0f);
    fill(&pulse_high_[0], &pulse_high_[kNumUndertowVoices], true);
    pulse_mode_ = false;
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

  float frequency[kNumUndertowVoices];
  float frequency_increment[kNumUndertowVoices];
  float main_amplitude[kNumUndertowVoices];
  float main_increment[kNumUndertowVoices];
  float aux_amplitude[kNumUndertowVoices];
  float aux_increment[kNumUndertowVoices];
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
    frequency[voice] = frequency_[voice];
    frequency_increment[voice] = \
        (target_frequency - frequency_[voice]) / static_cast<float>(size);
    main_amplitude[voice] = main_amplitude_[voice];
    main_increment[voice] = \
        (out_channel_target - main_amplitude_[voice]) / \
        static_cast<float>(size);
    aux_amplitude[voice] = aux_amplitude_[voice];
    aux_increment[voice] = \
        (aux_channel_target - aux_amplitude_[voice]) / \
        static_cast<float>(size);
  }

  size_t boundary = 0;
  const bool starts_in_pulse = colour_start > 0.5f;
  const bool ends_in_pulse = parameters.timbre > 0.5f;
  if (starts_in_pulse == ends_in_pulse) {
    boundary = size;
  } else {
    while (boundary < size) {
      const float colour = colour_start + \
          colour_increment * static_cast<float>(boundary + 1);
      if ((colour > 0.5f) != starts_in_pulse) {
        break;
      }
      ++boundary;
    }
  }
  if (starts_in_pulse) {
    if (boundary > 0) {
      RenderSawPulseBlock(
          0, boundary, colour_start, colour_increment, !pulse_mode_,
          phase_, frequency, frequency_increment,
          main_amplitude, main_increment, aux_amplitude, aux_increment,
          pulse_width_, pulse_high_, next_blep_, out, aux);
    }
    if (boundary < size) {
      RenderTriangleSawBlock(
          boundary, size, colour_start, colour_increment,
          phase_, frequency, frequency_increment,
          main_amplitude, main_increment, aux_amplitude, aux_increment,
          out, aux);
      fill(&next_blep_[0], &next_blep_[kNumUndertowVoices], 0.0f);
    }
  } else {
    RenderTriangleSawBlock(
        0, boundary, colour_start, colour_increment,
        phase_, frequency, frequency_increment,
        main_amplitude, main_increment, aux_amplitude, aux_increment,
        out, aux);
    fill(&next_blep_[0], &next_blep_[kNumUndertowVoices], 0.0f);
    if (boundary < size) {
      RenderSawPulseBlock(
          boundary, size, colour_start, colour_increment, true,
          phase_, frequency, frequency_increment,
          main_amplitude, main_increment, aux_amplitude, aux_increment,
          pulse_width_, pulse_high_, next_blep_, out, aux);
    }
  }
  pulse_mode_ = parameters.timbre > 0.5f;

  for (int voice = 0; voice < kNumUndertowVoices; ++voice) {
    frequency_[voice] = frequency[voice];
    main_amplitude_[voice] = main_amplitude[voice];
    aux_amplitude_[voice] = aux_amplitude[voice];
  }
  colour_ = parameters.timbre;
}

}  // namespace plaits
