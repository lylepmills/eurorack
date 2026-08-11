// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// A diagnostic, not a musical model. It keeps Mutable Instruments' Wave
// Terrain scan path and evaluates one selected expression four times per output
// sample: two trajectory oversamples times the two adjacent terrain reads the
// production engine performs. This makes each case a conservative stand-in for
// putting the same custom native equation on both sides of a HARMONICS blend.

#include "terrain_equation_bench_engine.h"

#include <algorithm>
#include <cmath>
#include <stdint.h>

#include "stmlib/dsp/atan.h"
#include "stmlib/dsp/rsqrt.h"
#include "plaits/dsp/oscillator/wavetable_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

const int kTerrainEquationBenchCases = 19;
const float kInvTwoPi = 0.15915494309189535f;
const float kPi = 3.14159265358979323846f;

// Leave this at -1 for the HARMONICS-multiplexed probe. build_matrix.py copies
// the package and changes the value to emit one linker-prunable firmware per
// case, which is how we measure the formula's individual flash cost.
#ifndef PLAITS_TERRAIN_BENCH_FIXED_CASE
#define PLAITS_TERRAIN_BENCH_FIXED_CASE -1
#endif

// The normal diagnostic remains knob-selectable so the QEMU sweep and fixed
// flash builds keep working. build_autosweep.py changes this one definition in
// a temporary package to make a self-sequencing hardware probe.
#ifndef PLAITS_TERRAIN_BENCH_AUTOSWEEP
#define PLAITS_TERRAIN_BENCH_AUTOSWEEP 0
#endif

// One autosweep is 116.5 seconds:
//   6 s sync gap
//   19 x (1.5 s settling gap + 4 s measured case)
//   6 s sync gap
// The adjacent trailer/leader gaps form a unique 12-second cycle marker. Two
// full cycles therefore guarantee one complete pass in an arbitrarily aligned
// capture. All durations are exact multiples of Plaits' 48 kHz sample rate.
const uint32_t kAutosweepLeaderSamples = 6u * 48000u;
const uint32_t kAutosweepGapSamples = 3u * 24000u;
const uint32_t kAutosweepCaseSamples = 4u * 48000u;
const uint32_t kAutosweepSlotSamples =
    kAutosweepGapSamples + kAutosweepCaseSamples;
const uint32_t kAutosweepTrailerSamples = 6u * 48000u;
const uint32_t kAutosweepCycleSamples = kAutosweepLeaderSamples +
    kTerrainEquationBenchCases * kAutosweepSlotSamples +
    kAutosweepTrailerSamples;

// A real 4 KB flash object for the sampled-grid baseline. The sparse initializer
// keeps this diagnostic source readable; the variable coordinates prevent the
// compiler from replacing the lookup with a constant.
static const int8_t kBenchTerrain[64 * 64] = {
  -127, -113, -96, -74, -49, -22, 7, 35,
  61, 84, 102, 116, 124, 127, 123, 113,
};

inline float FastSinRadians(float value) {
  return Sine(8.0f + value * kInvTwoPi);
}

inline float FastCosRadians(float value) {
  return Sine(8.25f + value * kInvTwoPi);
}

// Plaits' bare-metal libm cannot link expf/logf/powf because they pull in
// __errno. These are the same bounded, branch-light approximations already used
// by shipping Plaits engines. A native equation compiler would target these
// primitives, not the unavailable C library calls.
inline float FastLog2(float x) {
  union { float f; uint32_t i; } u = { x };
  const float exponent = static_cast<float>((u.i >> 23) & 0xFFu) - 127.0f;
  u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
  const float mantissa = u.f;
  return exponent + (-1.7417939f + (2.8212026f + (-1.4699568f +
      (0.44717955f - 0.056570851f * mantissa) * mantissa) * mantissa) *
      mantissa);
}

inline float FastExp2(float x) {
  const float kRoundMagic = 12582912.0f;
  const float rounded = (x + kRoundMagic) - kRoundMagic;
  const float fraction = x - rounded;
  const float polynomial = 1.0f + fraction * (0.6931472f + fraction *
      (0.2402265f + fraction * (0.0555041f + fraction * 0.0096181f)));
  union { float f; int32_t i; } power;
  power.i = (static_cast<int32_t>(rounded) + 127) << 23;
  return polynomial * power.f;
}

inline float FastExp(float x) {
  return FastExp2(x * 1.4426950408889634f);
}

inline float FastLog(float x) {
  return FastLog2(x) * 0.6931471805599453f;
}

inline float FastPower(float x, float exponent) {
  return FastExp2(exponent * FastLog2(x));
}

inline float FastSqrt(float x) {
  return x > 0.0f ? x * fast_rsqrt_accurate(x) : 0.0f;
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

inline float Squash(float value, float amount) {
  value *= amount;
  return value / (1.0f + fabsf(value));
}

inline float Ball(float x, float y, float xm, float ym, float width) {
  const float dx = x - xm;
  const float dy = y - ym;
  return FastExp(-(dx * dx + dy * dy) / (width * width));
}

inline float TerrainLookup(float x, float y) {
  const int terrain_size = 64;
  const float coord_scale = float(terrain_size - 2) * 0.5f;
  x = (x + 1.0f) * coord_scale;
  y = (y + 1.0f) * coord_scale;
  MAKE_INTEGRAL_FRACTIONAL(x);
  MAKE_INTEGRAL_FRACTIONAL(y);
  const int8_t* terrain = kBenchTerrain + y_integral * terrain_size;
  const float a = InterpolateWave(terrain, x_integral, x_fractional);
  terrain += terrain_size;
  const float b = InterpolateWave(terrain, x_integral, x_fractional);
  return (a + (b - a) * y_fractional) * (1.0f / 128.0f);
}

inline float EvaluateTerrainCase(int index, float x, float y) {
  switch (index) {
    case 0:  // Original terrain 1: known-good native reference.
      return (Squash(Sine(4.0f + x * 1.273f), 2.0f) -
          Sine(4.0f + y * (x + 1.571f) * 0.637f)) * 0.57f;

    case 1:  // Current 4 KB sampled representation.
      return TerrainLookup(x, y);

    case 2: {  // Soft rings: sin(10 * r)
      const float r = FastSqrt(x * x + y * y);
      return FastSinRadians(10.0f * r);
    }

    case 3: {  // Lone island: max() + sqrt().
      const float dx = x + 0.25f;
      const float dy = y - 0.15f;
      return max(0.0f, 1.0f - 2.2f * FastSqrt(dx * dx + dy * dy));
    }

    case 4:  // Tilted terraces: round().
      return Round(3.0f * (x + 0.35f * y)) + 0.4f * y;

    case 5:  // River bend: abs() + polynomial.
      return fabsf(y - 0.45f * x * x) + 0.18f * x;

    case 6: {  // Rippled saddle: polynomial + sin(r).
      const float r = FastSqrt(x * x + y * y);
      return 0.7f * (x * x - y * y) + 0.3f * FastSinRadians(11.0f * r);
    }

    case 7: {  // Four chambers: sign() + r.
      const float r = FastSqrt(x * x + y * y);
      return Sign(x * y) * (1.0f - 0.55f * r) + 0.2f * x;
    }

    case 8: {  // Spiral current: atan2() + sin().
      const float r = FastSqrt(x * x + y * y);
      const float theta = static_cast<float>(fast_atan2(y, x)) *
          (1.0f / 65536.0f);
      return Sine(8.0f + 10.0f * r * kInvTwoPi + 3.0f * theta);
    }

    case 9:  // Twin pulses: two exp() calls.
      return Ball(x, y, -0.38f, -0.22f, 0.32f) -
          0.85f * Ball(x, y, 0.38f, 0.27f, 0.18f);

    case 10: {  // Log crater: log() + sin().
      const float r = FastSqrt(x * x + y * y);
      return FastSinRadians(7.0f * FastLog(0.14f + r));
    }

    case 11:  // Pinched diamond: two pow() calls.
      return FastPower(fabsf(x), 0.45f) +
          FastPower(fabsf(y), 0.45f) - 1.0f;

    case 12:  // Saturated saddle: atan().
      return FastAtan(7.0f * (x * y + 0.18f * x));

    case 13:  // Warped fault: tan() + sin().
      {
        const float angle = 1.1f *
            (x + 0.25f * FastSinRadians(4.0f * y));
        return 0.35f * FastSinRadians(angle) / FastCosRadians(angle);
      }

    case 14:  // Four fast sine lookups.
      return 0.25f * (
          FastSinRadians(5.0f * x) + FastSinRadians(7.0f * y) +
          FastSinRadians(9.0f * (x + y)) + FastCosRadians(11.0f * (x - y)));

    case 15:  // Eight fast sine lookups.
      return 0.125f * (
          FastSinRadians(3.0f * x) + FastSinRadians(4.0f * y) +
          FastSinRadians(5.0f * (x + y)) + FastSinRadians(6.0f * (x - y)) +
          FastCosRadians(7.0f * x) + FastCosRadians(8.0f * y) +
          FastCosRadians(9.0f * (x + y)) + FastCosRadians(10.0f * (x - y)));

    case 16: {  // Tilted terraces blended with Log crater.
      const float terraces = Round(3.0f * (x + 0.35f * y)) + 0.4f * y;
      const float r = FastSqrt(x * x + y * y);
      const float crater = FastSinRadians(7.0f * FastLog(0.14f + r));
      return 0.5f * terraces + 0.5f * crater;
    }

    case 17: {  // Spiral current layered with Twin pulses.
      const float r = FastSqrt(x * x + y * y);
      const float theta = static_cast<float>(fast_atan2(y, x)) *
          (1.0f / 65536.0f);
      const float spiral = Sine(
          8.0f + 10.0f * r * kInvTwoPi + 3.0f * theta);
      const float pulses = Ball(x, y, -0.38f, -0.22f, 0.32f) -
          0.85f * Ball(x, y, 0.38f, 0.27f, 0.18f);
      return spiral + 0.5f * pulses;
    }

    case 18: {  // theta + mu derived variables.
      const int16_t theta = static_cast<int16_t>(fast_atan2(y, x));
      const float mu = 1.0f - fabsf(static_cast<float>(theta)) /
          32768.0f;
      return FastSinRadians(4.0f * kPi * mu) + 0.2f * x;
    }

    default:
      return 0.0f;
  }
}

void TerrainEquationBenchEngine::Init(BufferAllocator* allocator) {
  path_.Init();
  offset_ = 0.0f;
  y_offset_ = 0.0f;
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize * 4);
  sequence_samples_ = 0;
}

void TerrainEquationBenchEngine::Reset() {
  sequence_samples_ = 0;
}

void TerrainEquationBenchEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const size_t kOversampling = 2;
  const float kScale = 1.0f / float(kOversampling);
  float* path_x = &temp_buffer_[0];
  float* path_y = &temp_buffer_[kOversampling * size];

  // The autosweep deliberately ignores the panel and CV inputs. Every case is
  // measured at exactly the same nominal scan: MIDI note 48 and the three Wave
  // Terrain controls at 0.5. This removes knob-placement error from the probe.
#if PLAITS_TERRAIN_BENCH_AUTOSWEEP
  const float note = 48.0f;
  const float timbre = 0.5f;
  const float morph = 0.5f;
  const float macro = 0.5f;
#else
  const float note = parameters.note;
  const float timbre = parameters.timbre;
  const float morph = parameters.morph;
  const float macro = parameters.macro;
#endif

  const float f0 = NoteToFrequency(note);
  const float attenuation = max(1.0f - 8.0f * f0, 0.0f);
  const float radius = 0.1f + 0.9f * timbre * attenuation *
      (2.0f - attenuation);
  path_.RenderQuadrature(
      f0 * kScale, radius, path_x, path_y, size * kOversampling);

  ParameterInterpolator offset(&offset_, 1.9f * morph - 1.0f, size);
  ParameterInterpolator y_offset(
      &y_offset_, 1.9f * macro - 0.95f, size);

#if PLAITS_TERRAIN_BENCH_FIXED_CASE >= 0
  const int terrain_case = PLAITS_TERRAIN_BENCH_FIXED_CASE;
#elif PLAITS_TERRAIN_BENCH_AUTOSWEEP
  int terrain_case = -1;
  uint32_t position = sequence_samples_;
  if (position >= kAutosweepLeaderSamples) {
    position -= kAutosweepLeaderSamples;
    const uint32_t case_region =
        kTerrainEquationBenchCases * kAutosweepSlotSamples;
    if (position < case_region) {
      const int slot = static_cast<int>(position / kAutosweepSlotSamples);
      const uint32_t within_slot = position % kAutosweepSlotSamples;
      if (within_slot >= kAutosweepGapSamples) {
        terrain_case = slot;
      }
    }
  }
  sequence_samples_ += static_cast<uint32_t>(size);
  if (sequence_samples_ >= kAutosweepCycleSamples) {
    sequence_samples_ -= kAutosweepCycleSamples;
  }
#else
  const int terrain_case = min(
      int(parameters.harmonics * float(kTerrainEquationBenchCases)),
      kTerrainEquationBenchCases - 1);
#endif

  size_t path_index = 0;
  for (size_t i = 0; i < size; ++i) {
    const float x_offset = offset.Next();
    const float current_y_offset = y_offset.Next();
    float out_sample = 0.0f;
    float aux_sample = 0.0f;
    for (size_t j = 0; j < kOversampling; ++j) {
      const float x = path_x[path_index] * (1.0f - fabsf(x_offset)) + x_offset;
      const float y = path_y[path_index] *
          (1.0f - fabsf(current_y_offset)) + current_y_offset;
      // Production always reads both sides of the HARMONICS blend. Calling the
      // selected equation twice preserves that workload while isolating one
      // expression at a time.
      const float z0 = EvaluateTerrainCase(terrain_case, x, y);
      const float z1 = EvaluateTerrainCase(terrain_case, x, y);
      float z = 0.5f * (z0 + z1);
      // The browser normalizes every saved equation before it becomes terrain
      // data. A future native compiler can bake that equation-specific scale
      // and offset into two cheap arithmetic operations. The diagnostic uses a
      // common clamp instead so an intentionally extreme stress case cannot
      // send the stock AUX sine lookup outside its safe input range.
      CONSTRAIN(z, -1.0f, 1.0f);
      out_sample += z;
      aux_sample += y + z;
      ++path_index;
    }
    out[i] = kScale * out_sample;
    aux[i] = Sine(1.0f + 0.5f * kScale * aux_sample);
  }
}

}  // namespace plaits
