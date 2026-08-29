// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// A diagnostic, not a musical model. The 4 KB case follows Mutable's existing
// 64-byte map + 15 integrated 128-sample waves path. Native cases evaluate the
// editor equation directly at the current phase, column and row. Each native
// case is evaluated twice per sample because a multi-bank production engine
// has to read both sides of the HARMONICS blend.

#include "wavetable_equation_bench_engine.h"

#include <algorithm>
#include <cmath>
#include <stdint.h>

#include "stmlib/dsp/atan.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

const int kWavetableEquationBenchCases = 17;
const int kWaveSize = 128;
const int kIntegratedWaveSize = kWaveSize + 4;
const float kPi = 3.14159265358979323846f;
const float kTwoPi = 6.28318530717958647692f;
const float kInvTwoPi = 0.15915494309189535f;

#ifndef PLAITS_WAVETABLE_BENCH_FIXED_CASE
#define PLAITS_WAVETABLE_BENCH_FIXED_CASE -1
#endif

#ifndef PLAITS_WAVETABLE_BENCH_AUTOSWEEP
#define PLAITS_WAVETABLE_BENCH_AUTOSWEEP 0
#endif

#ifndef PLAITS_WAVETABLE_BENCH_STRESS_AUTOSWEEP
#define PLAITS_WAVETABLE_BENCH_STRESS_AUTOSWEEP 0
#endif

#if PLAITS_WAVETABLE_BENCH_STRESS_AUTOSWEEP
const int kWavetableEquationAutosweepTests =
    kWavetableEquationBenchCases * 2;
#else
const int kWavetableEquationAutosweepTests = kWavetableEquationBenchCases;
#endif

// 105.5 seconds: 6 s leader, 17 x (1.5 s gap + 4 s case), 6 s trailer.
// Adjacent trailer/leader gaps form a unique 12-second boundary marker.
const uint32_t kAutosweepLeaderSamples = 6u * 48000u;
const uint32_t kAutosweepGapSamples = 3u * 24000u;
const uint32_t kAutosweepCaseSamples = 4u * 48000u;
const uint32_t kAutosweepSlotSamples =
    kAutosweepGapSamples + kAutosweepCaseSamples;
const uint32_t kAutosweepTrailerSamples = 6u * 48000u;
const uint32_t kAutosweepCycleSamples = kAutosweepLeaderSamples +
    kWavetableEquationAutosweepTests * kAutosweepSlotSamples +
    kAutosweepTrailerSamples;

// These two objects deliberately occupy the exact meaningful portion of an MI
// upload: 64 map bytes plus 15 x 132 x int16 integrated samples = 4,024 bytes.
// Sparse waveform contents are sufficient for timing; the fidelity script uses
// fully generated banks from the real equations.
static const uint8_t kBenchMap[64] = {
  0, 0, 1, 1, 2, 2, 3, 3,
  0, 0, 1, 1, 2, 2, 3, 3,
  4, 4, 5, 5, 6, 6, 7, 7,
  4, 4, 5, 5, 6, 6, 7, 7,
  8, 8, 9, 9, 10, 10, 11, 11,
  8, 8, 9, 9, 10, 10, 11, 11,
  12, 12, 13, 13, 14, 14, 14, 14,
  12, 12, 13, 13, 14, 14, 14, 14,
};

static const int16_t kBenchIntegratedWaves[15 * kIntegratedWaveSize] = {
  0, 1608, 3212, 4808, 6393, 7962, 9512, 11039,
  12539, 14009, 15445, 16844, 18203, 19517, 20782, 21993,
};

inline float FastSin(float radians) {
  return Sine(8.0f + radians * kInvTwoPi);
}

inline float FastAtan(float value) {
  const int16_t angle = static_cast<int16_t>(fast_atan2(value, 1.0f));
  return static_cast<float>(angle) * (kPi / 32768.0f);
}

inline float Sign(float value) {
  return value > 0.0f ? 1.0f : value < 0.0f ? -1.0f : 0.0f;
}

inline float Round(float value) {
  return floorf(value + 0.5f);
}

inline float Clip(float value, float low, float high) {
  return min(max(value, low), high);
}

inline float Glass(float phi, float x, float y) {
  return FastSin(phi + (1.0f + 7.0f * x) * (0.2f + 0.65f * y) *
      FastSin(2.0f * phi));
}

inline float EvaluateWavetableCase(
    int index, float phi, float x, float y) {
  switch (index) {
    case 1:  // Mutable FM.
      return FastSin(phi + 3.0f * (x + 0.125f) *
          FastSin((1.0f + floorf(4.0f * y)) * phi));

    case 2:  // Glass FM.
      return Glass(phi, x, y);

    case 3:  // Harmonic grid.
      return FastSin(phi) +
          0.45f * FastSin((2.0f + floorf(5.0f * x)) * phi) +
          0.28f * FastSin((3.0f + floorf(8.0f * y)) * phi);

    case 4:  // Phase warp.
      return FastSin(phi + (0.3f + 2.8f * x) *
          FastSin(phi + kPi * y));

    case 5:  // Pulse matrix.
      return Sign(FastSin(phi) - (0.75f * x - 0.35f)) +
          0.18f * FastSin((2.0f + floorf(5.0f * y)) * phi);

    case 6:  // Odd / even weave.
      return FastSin(phi) + 0.65f * x * FastSin(2.0f * phi) +
          0.55f * y * FastSin(3.0f * phi) +
          0.3f * x * y * FastSin(5.0f * phi);

    case 7:  // Glass + upper partial.
      return Glass(phi, x, y) +
          0.24f * FastSin((2.0f + floorf(6.0f * x)) * phi);

    case 8:  // Glass + row motion.
      return Glass(phi, x, y) +
          0.22f * FastSin((3.0f + 7.0f * y) * phi + kPi * x);

    case 9:  // Fold the Glass output.
      return FastSin(kPi * Glass(phi, x, y));

    case 10:  // Use Glass as an FM shape.
      return FastSin(phi + (1.0f + 4.0f * y) * Glass(phi, x, y));

    case 11:  // Ring Glass with rows.
      return Glass(phi, x, y) *
          FastSin((2.0f + floorf(4.0f * y)) * phi);

    case 12:  // Terrace Glass.
      return Round(5.0f * Glass(phi, x, y)) * 0.2f;

    case 13:  // Soft-clip Glass.
      return FastAtan(2.5f * Glass(phi, x, y));

    case 14:  // Hard-clip Glass.
      return Clip(Glass(phi, x, y), -0.6f, 0.6f);

    case 15: {  // Three-transform stack: row motion -> fold -> soft clip.
      const float row = Glass(phi, x, y) +
          0.22f * FastSin((3.0f + 7.0f * y) * phi + kPi * x);
      return FastAtan(2.5f * FastSin(kPi * row));
    }

    case 16:  // Eight-sine stress.
      return 0.125f * (
          FastSin(phi) + FastSin(2.0f * phi + x) +
          FastSin(3.0f * phi + y) + FastSin(4.0f * phi + x + y) +
          FastSin(5.0f * phi + 2.0f * x) +
          FastSin(6.0f * phi + 2.0f * y) +
          FastSin(7.0f * phi + x - y) +
          FastSin(8.0f * phi + 2.0f * x - y));

    default:
      return 0.0f;
  }
}

void WavetableEquationBenchEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  phase_ = 0.0f;
  x_lp_ = y_lp_ = 0.0f;
  previous_x_ = previous_y_ = 0.0f;
  previous_f0_ = a0;
  sequence_samples_ = 0;
  differentiator_.Init();
}

void WavetableEquationBenchEngine::Reset() {
  sequence_samples_ = 0;
}

float WavetableEquationBenchEngine::ReadSampledBank(
    float x, float y, float phase) {
  MAKE_INTEGRAL_FRACTIONAL(x);
  MAKE_INTEGRAL_FRACTIONAL(y);
  const int x1 = min(static_cast<int>(x_integral) + 1, 7);
  const int y1 = min(static_cast<int>(y_integral) + 1, 7);
  const float p = phase * float(kWaveSize);
  MAKE_INTEGRAL_FRACTIONAL(p);
  const int map_indices[4] = {
    x_integral + y_integral * 8,
    x1 + y_integral * 8,
    x_integral + y1 * 8,
    x1 + y1 * 8,
  };
  float phase_samples[4];
  for (int tap = 0; tap < 4; ++tap) {
    const int sample = p_integral + tap;
    const int16_t* a = kBenchIntegratedWaves +
        kBenchMap[map_indices[0]] * kIntegratedWaveSize;
    const int16_t* b = kBenchIntegratedWaves +
        kBenchMap[map_indices[1]] * kIntegratedWaveSize;
    const int16_t* c = kBenchIntegratedWaves +
        kBenchMap[map_indices[2]] * kIntegratedWaveSize;
    const int16_t* d = kBenchIntegratedWaves +
        kBenchMap[map_indices[3]] * kIntegratedWaveSize;
    const float top = a[sample] + (b[sample] - a[sample]) * x_fractional;
    const float bottom = c[sample] + (d[sample] - c[sample]) * x_fractional;
    phase_samples[tap] = top + (bottom - top) * y_fractional;
  }
  return InterpolateWaveHermite(phase_samples, 0, p_fractional);
}

void WavetableEquationBenchEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
#if PLAITS_WAVETABLE_BENCH_AUTOSWEEP
  int autosweep_case = -1;
#if PLAITS_WAVETABLE_BENCH_STRESS_AUTOSWEEP
  int autosweep_profile = 0;
#endif
  uint32_t position = sequence_samples_;
  if (position >= kAutosweepLeaderSamples) {
    position -= kAutosweepLeaderSamples;
    const uint32_t case_region =
        kWavetableEquationAutosweepTests * kAutosweepSlotSamples;
    if (position < case_region) {
      const int slot = static_cast<int>(position / kAutosweepSlotSamples);
      const uint32_t within_slot = position % kAutosweepSlotSamples;
      if (within_slot >= kAutosweepGapSamples) {
        autosweep_case = slot % kWavetableEquationBenchCases;
#if PLAITS_WAVETABLE_BENCH_STRESS_AUTOSWEEP
        autosweep_profile = slot / kWavetableEquationBenchCases;
#endif
      }
    }
  }
  sequence_samples_ += static_cast<uint32_t>(size);
  if (sequence_samples_ >= kAutosweepCycleSamples) {
    sequence_samples_ -= kAutosweepCycleSamples;
  }
#if PLAITS_WAVETABLE_BENCH_STRESS_AUTOSWEEP
  const float note = autosweep_profile ? 84.0f : 48.0f;
  const float timbre = autosweep_profile
      ? ((autosweep_case & 1) ? 1.0f : 0.0f) : 0.5f;
  const float morph = autosweep_profile
      ? ((autosweep_case & 1) ? 0.0f : 1.0f) : 0.5f;
  const float macro = autosweep_profile
      ? static_cast<float>((autosweep_case + 1) % 3) * 0.5f : 0.5f;
#else
  const float note = 48.0f;
  const float timbre = 0.5f;
  const float morph = 0.5f;
  const float macro = 0.5f;
#endif
#else
  const float note = parameters.note;
  const float timbre = parameters.timbre;
  const float morph = parameters.morph;
  const float macro = parameters.macro;
#endif
  const float f0 = NoteToFrequency(note);
  const float target_x = timbre * 6.9999f;
  const float target_y = morph * 6.9999f;
  const float phase_warp = 0.14f * (2.0f * macro - 1.0f);
  const float lp_coefficient = min(max(8.0f * f0, 0.01f), 0.1f);
  ParameterInterpolator x_modulation(&previous_x_, target_x, size);
  ParameterInterpolator y_modulation(&previous_y_, target_y, size);
  ParameterInterpolator f0_modulation(&previous_f0_, f0, size);

#if PLAITS_WAVETABLE_BENCH_FIXED_CASE >= 0
  int wavetable_case = PLAITS_WAVETABLE_BENCH_FIXED_CASE;
#elif PLAITS_WAVETABLE_BENCH_AUTOSWEEP
  int wavetable_case = autosweep_case;
#else
  int wavetable_case = min(
      int(parameters.harmonics * float(kWavetableEquationBenchCases)),
      kWavetableEquationBenchCases - 1);
#endif

  while (size--) {
    const float frequency = f0_modulation.Next();
    ONE_POLE(x_lp_, x_modulation.Next(), lp_coefficient);
    ONE_POLE(y_lp_, y_modulation.Next(), lp_coefficient);
    phase_ += frequency;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    float warped_phase = phase_ + phase_warp * Sine(phase_);
    if (warped_phase < 0.0f) warped_phase += 1.0f;
    if (warped_phase >= 1.0f) warped_phase -= 1.0f;

    float sample;
    if (wavetable_case == 0) {
      // Two reads stand in for the two adjacent banks selected by HARMONICS.
      const float a = ReadSampledBank(x_lp_, y_lp_, warped_phase);
      const float b = ReadSampledBank(
          x_lp_, y_lp_, warped_phase + 0.000001f);
      const float integrated = 0.5f * (a + b);
      const float cutoff = min(float(kWaveSize) * frequency, 1.0f);
      const float gain = (1.0f / (frequency * 131072.0f)) *
          (0.95f - frequency);
      sample = differentiator_.Process(cutoff, integrated) * gain;
    } else {
      const float x = x_lp_ * (1.0f / 7.0f);
      const float y = y_lp_ * (1.0f / 7.0f);
      const float phi = warped_phase * kTwoPi;
      const float a = EvaluateWavetableCase(wavetable_case, phi, x, y);
      const float b = EvaluateWavetableCase(
          wavetable_case, phi + 0.0000062831853f, x, y);
      sample = 0.5f * (a + b);
      CONSTRAIN(sample, -1.0f, 1.0f);
    }

    *out++ = sample;
    *aux++ = static_cast<float>(static_cast<int>(sample * 32.0f)) / 32.0f;
  }
}

}  // namespace plaits
