// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Pitch-clocked binary feedback synthesis engine.

#include "plaits/dsp/engine2/tapfield_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

void TapfieldEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void TapfieldEngine::Reset() {
  state_ = 1u;
  tap_mask_ = 1u;
  width_mask_ = 0x1ffu;
  clock_counter_ = 0u;
  register_length_ = -1;
  tap_family_ = -1;
  clock_phase_ = 0.0f;
  corruption_phase_ = 0.0f;
  target_ = 0.0f;
  value_ = 0.0f;
  aux_target_ = -1.0f;
  ConfigureTopology(0.0f);
  Seed();
}

void TapfieldEngine::ConfigureTopology(float harmonics) {
  int selection = static_cast<int>(harmonics * 63.999f);
  selection = max(0, min(63, selection));
  const int length = 9 + (selection & 15);
  const int family = selection >> 4;
  if (length == register_length_ && family == tap_family_) {
    return;
  }

  register_length_ = length;
  tap_family_ = family;
  width_mask_ = (1u << register_length_) - 1u;

  // The three interior taps are generated from register geometry rather than
  // selected from a polynomial table. The mandatory top tap keeps updates in
  // the selected word width; occasional controlled mutation avoids relying on
  // maximal-period masks for character.
  const int span = register_length_ - 2;
  const int tap_a = 1 + (register_length_ + 3 * tap_family_) % span;
  const int tap_b = 1 + (register_length_ / 2 + \
      5 * tap_family_ + 1) % span;
  const int tap_c = 1 + (register_length_ / 3 + \
      7 * tap_family_ + 3) % span;
  tap_mask_ = (1u << (register_length_ - 1)) | \
      (1u << tap_a) | (1u << tap_b) | (1u << tap_c);

  state_ &= width_mask_;
  if (!state_) {
    Seed();
  }
}

void TapfieldEngine::Seed() {
  uint32_t seed = 0x6d2b79f5u ^ \
      static_cast<uint32_t>(register_length_ * 0x45d9f3bu) ^ \
      static_cast<uint32_t>(tap_family_ * 0x119de1f3u);
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  state_ = seed & width_mask_;
  if (!state_) {
    state_ = (1u << (register_length_ - 1)) | 1u;
  }
  clock_counter_ = 0u;
  corruption_phase_ = 0.0f;
  aux_target_ = (state_ & 1u) ? 1.0f : -1.0f;
}

void TapfieldEngine::Clock(float corruption) {
  const uint32_t carry = state_ & 1u;
  state_ >>= 1;
  if (carry) {
    state_ ^= tap_mask_;
  }
  state_ &= width_mask_;
  ++clock_counter_;
  aux_target_ = carry ? 1.0f : -1.0f;

  // Corruption is deterministic: it schedules sparse single-bit changes from
  // state and clock history, so repeats are possible after a trigger.
  corruption_phase_ += 0.45f * corruption * corruption;
  if (corruption_phase_ >= 1.0f) {
    corruption_phase_ -= 1.0f;
    const uint32_t folded = state_ ^ (state_ >> 7) ^ \
        (clock_counter_ * 0x9e37u);
    const int bit = static_cast<int>(
        folded % static_cast<uint32_t>(register_length_));
    state_ ^= 1u << bit;
  }
  if (!state_) {
    state_ = (1u << (register_length_ - 1)) | 1u;
  }
}

float TapfieldEngine::Decode(float timbre) const {
  const uint32_t gray = state_ ^ (state_ >> 1);
  const float gray_amount = timbre;
  const float weight_amount = timbre * timbre;
  float value = 0.0f;
  float weight_sum = 0.0f;
  for (int i = 0; i < 8; ++i) {
    const int bit = i * (register_length_ - 1) / 7;
    const float direct_value = (state_ & (1u << bit)) ? 1.0f : -1.0f;
    const float gray_value = (gray & (1u << bit)) ? 1.0f : -1.0f;
    const float bit_value = direct_value + \
        (gray_value - direct_value) * gray_amount;
    const float binary_weight = static_cast<float>(1u << i);
    const float weight = 1.0f + \
        (binary_weight - 1.0f) * weight_amount;
    value += bit_value * weight;
    weight_sum += weight;
  }
  return value / weight_sum;
}

void TapfieldEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  ConfigureTopology(parameters.harmonics);
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Seed();
    clock_phase_ = 0.0f;
    target_ = Decode(parameters.timbre);
  }

  const float clock_frequency = min(
      0.45f, 4.0f * NoteToFrequency(parameters.note));
  const float one_minus_slew = 1.0f - parameters.macro;
  const float slew = 0.0025f + 0.9975f * one_minus_slew * \
      one_minus_slew * one_minus_slew;

  for (size_t i = 0; i < size; ++i) {
    clock_phase_ += clock_frequency;
    if (clock_phase_ >= 1.0f) {
      clock_phase_ -= 1.0f;
      Clock(parameters.morph);
      target_ = Decode(parameters.timbre);
    }
    value_ += slew * (target_ - value_);
    out[i] = 0.9f * value_;
    aux[i] = 0.72f * aux_target_;
  }
}

}  // namespace plaits
