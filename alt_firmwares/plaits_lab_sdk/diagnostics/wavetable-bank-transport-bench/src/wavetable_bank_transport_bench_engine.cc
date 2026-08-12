// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// A temporary diagnostic for the intended Plaits Palette bank transport. It
// keeps the original duplicated endpoints when mirroring:
//   1, 2, 3, 3, 2, 1
// and uses a direct monotonic path when mirroring is disabled.

#include "wavetable_bank_transport_bench_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

const float kTransportPi = 3.14159265358979323846f;
const float kTransportTwoPi = 6.28318530717958647692f;
const float kTransportInvTwoPi = 0.15915494309189535f;

#ifndef PLAITS_WAVETABLE_TRANSPORT_AUTOSWEEP
#define PLAITS_WAVETABLE_TRANSPORT_AUTOSWEEP 0
#endif

const int kTransportAutosweepTests = 12;
const uint32_t kTransportLeaderSamples = 6u * 48000u;
const uint32_t kTransportGapSamples = 3u * 24000u;
const uint32_t kTransportCaseSamples = 4u * 48000u;
const uint32_t kTransportSlotSamples =
    kTransportGapSamples + kTransportCaseSamples;
const uint32_t kTransportTrailerSamples = 6u * 48000u;
const uint32_t kTransportCycleSamples = kTransportLeaderSamples +
    kTransportAutosweepTests * kTransportSlotSamples +
    kTransportTrailerSamples;

struct TransportConfiguration {
  int banks;
  bool mirror;
};

static const TransportConfiguration kConfigurations[6] = {
  { 1, true },
  { 2, true },
  { 3, true },
  { 8, true },
  { 8, false },
  { 16, false },
};

inline float TransportSin(float radians) {
  return Sine(8.0f + radians * kTransportInvTwoPi);
}

inline float TransportSign(float value) {
  return value >= 0.0f ? 1.0f : -1.0f;
}

inline int BankAtPathIndex(int path_index, int banks, bool mirror) {
  if (!mirror) return min(max(path_index, 0), banks - 1);
  const int path_size = banks * 2;
  path_index = min(max(path_index, 0), path_size - 1);
  return path_index < banks ? path_index : path_size - 1 - path_index;
}

inline float EvaluateBank(int bank, float phi, float x, float y) {
  const int family = bank & 7;
  const float octave_marker = bank >= 8 ? 1.0f : 0.0f;
  float value = 0.0f;
  switch (family) {
    case 0:
      value = TransportSin(phi) +
          0.42f * x * TransportSin((2.0f + 3.0f * y) * phi);
      break;
    case 1:
      value = TransportSin(phi + (0.35f + 4.0f * x) *
          TransportSin((1.0f + 3.0f * y) * phi));
      break;
    case 2:
      value = TransportSin(phi) + 0.38f * TransportSin((3.0f + 4.0f * x) * phi)
          + 0.24f * TransportSin((5.0f + 5.0f * y) * phi);
      break;
    case 3:
      value = 0.72f * TransportSign(TransportSin(phi) - (0.7f * x - 0.35f))
          + 0.18f * TransportSin((2.0f + 5.0f * y) * phi);
      break;
    case 4:
      value = TransportSin(phi + (0.25f + 2.8f * x) *
          TransportSin(phi + kTransportPi * y));
      break;
    case 5:
      value = TransportSin(phi) + 0.55f * x * TransportSin(2.0f * phi)
          + 0.45f * y * TransportSin(3.0f * phi)
          + 0.24f * x * y * TransportSin(5.0f * phi);
      break;
    case 6:
      value = TransportSin(phi + 1.8f * x * TransportSin(2.0f * phi)) *
          TransportSin((2.0f + 3.0f * y) * phi);
      break;
    default:
      value = TransportSin(kTransportPi *
          TransportSin(phi + (0.2f + 2.4f * y) * TransportSin(2.0f * phi)))
          + 0.2f * x * TransportSin(7.0f * phi);
      break;
  }
  if (octave_marker > 0.0f) {
    value = 0.78f * value + 0.22f * TransportSin(
        static_cast<float>(bank + 2) * phi + kTransportPi * x);
  }
  value *= 0.72f;
  CONSTRAIN(value, -1.0f, 1.0f);
  return value;
}

void WavetableBankTransportBenchEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  phase_ = 0.0f;
  previous_f0_ = a0;
  previous_harmonics_ = 0.0f;
  previous_timbre_ = 0.5f;
  previous_morph_ = 0.5f;
  sequence_samples_ = 0;
}

void WavetableBankTransportBenchEngine::Reset() {
  phase_ = 0.0f;
  sequence_samples_ = 0;
}

void WavetableBankTransportBenchEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  (void) already_enveloped;
#if PLAITS_WAVETABLE_TRANSPORT_AUTOSWEEP
  int test = -1;
  uint32_t within_case = 0;
  uint32_t position = sequence_samples_;
  if (position >= kTransportLeaderSamples) {
    position -= kTransportLeaderSamples;
    const uint32_t test_region =
        kTransportAutosweepTests * kTransportSlotSamples;
    if (position < test_region) {
      const int slot = static_cast<int>(position / kTransportSlotSamples);
      const uint32_t within_slot = position % kTransportSlotSamples;
      if (within_slot >= kTransportGapSamples) {
        test = slot;
        within_case = within_slot - kTransportGapSamples;
      }
    }
  }
  sequence_samples_ += static_cast<uint32_t>(size);
  if (sequence_samples_ >= kTransportCycleSamples) {
    sequence_samples_ -= kTransportCycleSamples;
  }
  if (test < 0) {
    while (size--) {
      *out++ = 0.0f;
      *aux++ = 0.0f;
    }
    return;
  }
  const int mode = test % 6;
  const bool high_profile = test >= 6;
  const float note = high_profile ? 84.0f : 48.0f;
  const float timbre = high_profile ? ((mode & 1) ? 1.0f : 0.0f) : 0.5f;
  const float morph = high_profile ? ((mode & 1) ? 0.0f : 1.0f) : 0.5f;
  const float harmonics = min(
      static_cast<float>(within_case) /
          static_cast<float>(kTransportCaseSamples - 1),
      1.0f);
#else
  int mode = static_cast<int>(parameters.macro * 6.0f);
  if (mode < 0) mode = 0;
  if (mode > 5) mode = 5;
  const float note = parameters.note;
  const float timbre = parameters.timbre;
  const float morph = parameters.morph;
  const float harmonics = parameters.harmonics;
#endif
  const TransportConfiguration configuration = kConfigurations[mode];
  const int path_size = configuration.mirror
      ? configuration.banks * 2 : configuration.banks;
  const float path_max = static_cast<float>(path_size - 1);

  ParameterInterpolator f0_modulation(
      &previous_f0_, NoteToFrequency(note), size);
  ParameterInterpolator bank_modulation(
      &previous_harmonics_, harmonics * path_max, size);
  ParameterInterpolator x_modulation(
      &previous_timbre_, timbre, size);
  ParameterInterpolator y_modulation(
      &previous_morph_, morph, size);

  while (size--) {
    phase_ += f0_modulation.Next();
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    const float position = bank_modulation.Next();
    const int left_path = static_cast<int>(position);
    const int right_path = min(left_path + 1, path_size - 1);
    const float fraction = position - static_cast<float>(left_path);
    const int left_bank = BankAtPathIndex(
        left_path, configuration.banks, configuration.mirror);
    const int right_bank = BankAtPathIndex(
        right_path, configuration.banks, configuration.mirror);
    const float phi = phase_ * kTransportTwoPi;
    const float x = x_modulation.Next();
    const float y = y_modulation.Next();
    const float left = EvaluateBank(left_bank, phi, x, y);
    const float right = EvaluateBank(right_bank, phi, x, y);
    const float sample = left + (right - left) * fraction;
    *out++ = sample;
    *aux++ = static_cast<float>(static_cast<int>(sample * 32.0f)) / 32.0f;
  }
}

}  // namespace plaits
