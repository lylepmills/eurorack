// Copyright 2012 Olivier Gillet.
// Copyright 2015 Tim Churches.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Bees-in-the-Trees' four BYTEBEAT models merged into one slot.

#include "plaits/dsp/engine2/bytebeat_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// The four expressions, transcribed from Tim Churches'
// DigitalOscillator::RenderBytebeat0..3. The arithmetic is unsigned 32-bit and
// wraps, exactly as it did on the STM32F1; the only edit is the modulo guard
// in formula 3, and `& 0xFF` becoming `& mask` so MACRO can open the width.
inline uint32_t Bytebeat(int formula, uint32_t t, uint32_t p0, uint32_t p1) {
  switch (formula) {
    case 0:
      // "atmospheric, hopeful"
      return ((t * 3) & (t >> 10))
          | ((t * p0) & (t >> 10))
          | ((t * 10) & ((t >> 8) * p1) & 128);
    case 1:
      return ((t * p0) & (t >> 4))
          | ((t * 5) & (t >> 7))
          | ((t * p1) & (t >> 10));
    case 2:
      return ((t >> p0) & t) * (t >> p1);
    default:
      // p1 is clamped away from zero by the caller -- upstream allowed 0 here
      // and divided by it.
      return ((((((t >> p0) | t) | (t >> p0)) * 10) & ((5 * t) | (t >> 10)))
          | (t ^ (t % p1)));
  }
}

// Braids read the low byte as a signed 8-bit sample: `(expr & 0xFF) << 8`
// stored into an int16 wraps everything from 128 up into the negative half.
// Generalised to `width` bits, that is a two's-complement read of the low
// `width` bits, normalised by half the range. At width == 8 this is bit-exact
// with the original.
inline float MaskedSample(uint32_t value, int width) {
  const uint32_t span = 1u << width;
  const uint32_t half = span >> 1;
  const uint32_t masked = value & (span - 1u);
  const int32_t centred = masked >= half
      ? static_cast<int32_t>(masked) - static_cast<int32_t>(span)
      : static_cast<int32_t>(masked);
  return static_cast<float>(centred) / static_cast<float>(half);
}

}  // namespace

void BytebeatEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void BytebeatEngine::Reset() {
  tick_phase_ = 0.0f;
  aux_tick_phase_ = 0.0f;
  t_ = kBytebeatRestartTick;
  aux_t_ = kBytebeatRestartTick;
  dc_in_ = 0.0f;
  dc_out_ = 0.0f;
  dc_aux_in_ = 0.0f;
  dc_aux_out_ = 0.0f;
}

void BytebeatEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // Only the stream restarts; the DC blocker keeps its state so a retrigger
    // does not punch a step through it.
    tick_phase_ = 0.0f;
    aux_tick_phase_ = 0.0f;
    t_ = kBytebeatRestartTick;
    aux_t_ = kBytebeatRestartTick;
  }

  int formula = static_cast<int>(parameters.morph * kBytebeatNumFormulas);
  CONSTRAIN(formula, 0, kBytebeatNumFormulas - 1);

  // Upstream took different bit-widths off the two knobs per formula; those
  // ranges are what each expression was tuned against, so they are kept.
  uint32_t p0;
  uint32_t p1;
  if (formula == 0) {
    p0 = static_cast<uint32_t>(parameters.harmonics * 63.0f);
    p1 = static_cast<uint32_t>(parameters.timbre * 15.0f);
  } else if (formula == 3) {
    p0 = static_cast<uint32_t>(parameters.harmonics * 15.0f);
    // 1..127. Upstream's 0 was a division by zero.
    p1 = 1u + static_cast<uint32_t>(parameters.timbre * 126.0f);
  } else {
    p0 = static_cast<uint32_t>(parameters.harmonics * 15.0f);
    p1 = static_cast<uint32_t>(parameters.timbre * 15.0f);
  }

  int width = static_cast<int>(ApplyMacro(8.0f, 2.0f, 12.0f, parameters.macro));
  CONSTRAIN(width, 2, 12);

  const float f0 = NoteToFrequency(parameters.note);
  const float tick_increment = f0 * kBytebeatTicksPerCycle;
  const float aux_tick_increment = tick_increment * kBytebeatAuxRatio;

  for (size_t i = 0; i < size; ++i) {
    // Fractional tick advance. Below 1.0 this holds the previous value -- the
    // zero-order hold that gives a bytebeat its stair-stepped character -- and
    // above it, ticks are skipped.
    tick_phase_ += tick_increment;
    while (tick_phase_ >= 1.0f) {
      tick_phase_ -= 1.0f;
      ++t_;
    }
    aux_tick_phase_ += aux_tick_increment;
    while (aux_tick_phase_ >= 1.0f) {
      aux_tick_phase_ -= 1.0f;
      ++aux_t_;
    }

    const float sample = MaskedSample(
        Bytebeat(formula, t_, p0, p1), width);
    const float aux_sample = MaskedSample(
        Bytebeat(formula, aux_t_, p0, p1), width);

    dc_out_ = sample - dc_in_ + kBytebeatDcPole * dc_out_;
    dc_in_ = sample;
    out[i] = dc_out_;

    dc_aux_out_ = aux_sample - dc_aux_in_ + kBytebeatDcPole * dc_aux_out_;
    dc_aux_in_ = aux_sample;
    aux[i] = dc_aux_out_;
  }
}

}  // namespace plaits
