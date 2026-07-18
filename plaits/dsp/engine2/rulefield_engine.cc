// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Cellular-automaton wavecycle engine.

#include "plaits/dsp/engine2/rulefield_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

uint32_t RulefieldEngine::RotateLeft(uint32_t value) {
  return (value << 1) | (value >> 31);
}

uint32_t RulefieldEngine::RotateRight(uint32_t value) {
  return (value >> 1) | (value << 31);
}

float RulefieldEngine::Density(uint32_t value) {
  value -= (value >> 1) & 0x55555555u;
  value = (value & 0x33333333u) + ((value >> 2) & 0x33333333u);
  value = (value + (value >> 4)) & 0x0f0f0f0fu;
  value += value >> 8;
  value += value >> 16;
  return static_cast<float>(value & 0x3fu) * (1.0f / 32.0f);
}

void RulefieldEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void RulefieldEngine::Reset() {
  rule_ = 30;
  generation_ = 0u;
  phase_ = 0.0f;
  evolution_phase_ = 0.0f;
  Seed();
}

void RulefieldEngine::Seed() {
  uint32_t seed = 0xa3619d27u ^ \
      (static_cast<uint32_t>(rule_) * 0x45d9f3bu);
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  row_ = seed;
  if (!row_ || row_ == 0xffffffffu) {
    row_ = 0x80000001u;
  }
  previous_row_ = RotateRight(row_);
  generation_ = 0u;
  UpdateReadouts();
}

void RulefieldEngine::UpdateReadouts() {
  edge_row_ = row_ ^ RotateRight(row_);
  activity_row_ = row_ ^ previous_row_;
  row_density_ = Density(row_);
  edge_density_ = Density(edge_row_);
  activity_density_ = Density(activity_row_);
}

void RulefieldEngine::Evolve() {
  const uint32_t left = RotateLeft(row_);
  const uint32_t center = row_;
  const uint32_t right = RotateRight(row_);
  uint32_t next = 0u;
  for (int neighborhood = 0; neighborhood < 8; ++neighborhood) {
    if (!(rule_ & (1u << neighborhood))) {
      continue;
    }
    const uint32_t left_match = neighborhood & 4 ? left : ~left;
    const uint32_t center_match = neighborhood & 2 ? center : ~center;
    const uint32_t right_match = neighborhood & 1 ? right : ~right;
    next |= left_match & center_match & right_match;
  }

  previous_row_ = row_;
  row_ = next;
  ++generation_;
  // Keep absorbing all-zero and all-one rows audible without introducing a
  // random dependency. The injected cell follows a long deterministic walk.
  const int injection = static_cast<int>((generation_ * 13u + rule_) & 31u);
  if (!row_) {
    row_ = 1u << injection;
  } else if (row_ == 0xffffffffu) {
    row_ &= ~(1u << injection);
  }
  UpdateReadouts();
}

float RulefieldEngine::ReadWave(
    uint32_t row,
    float density,
    float phase,
    float shape) const {
  const float address = phase * 32.0f;
  const int index = static_cast<int>(address) & 31;
  const int next_index = (index + 1) & 31;
  const float fraction = address - static_cast<int>(address);
  const float a = (row & (1u << index)) ? 1.0f - density : -density;
  const float b = (row & (1u << next_index)) ? 1.0f - density : -density;
  const float linear = a + (b - a) * fraction;
  const float smooth_fraction = fraction * fraction * \
      (3.0f - 2.0f * fraction);
  const float smooth = a + (b - a) * smooth_fraction;
  return shape < 0.5f
      ? a + (linear - a) * (2.0f * shape)
      : linear + (smooth - linear) * (2.0f * shape - 1.0f);
}

void RulefieldEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const uint8_t rule = static_cast<uint8_t>(
      1 + static_cast<int>(253.999f * parameters.harmonics));
  if (rule != rule_) {
    rule_ = rule;
  }
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Seed();
    phase_ = 0.0f;
    evolution_phase_ = 0.0f;
  }

  const float frequency = min(0.24f, NoteToFrequency(parameters.note));
  const float evolution_rate = 0.125f + 7.875f * \
      parameters.morph * parameters.morph;

  for (size_t i = 0; i < size; ++i) {
    phase_ += frequency;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
      evolution_phase_ += evolution_rate;
      int iterations = 0;
      while (evolution_phase_ >= 1.0f && iterations < 8) {
        evolution_phase_ -= 1.0f;
        Evolve();
        ++iterations;
      }
    }

    const float cells = ReadWave(
        row_, row_density_, phase_, parameters.macro);
    const float edges = ReadWave(
        edge_row_, edge_density_, phase_, parameters.macro);
    const float activity = ReadWave(
        activity_row_, activity_density_, phase_, parameters.macro);
    out[i] = 0.9f * (cells + (edges - cells) * parameters.timbre);
    aux[i] = 0.9f * activity;
  }
}

}  // namespace plaits
