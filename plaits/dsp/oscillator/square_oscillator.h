// Dedicated minimal square wave oscillator

#ifndef PLAITS_DSP_OSCILLATOR_SQUARE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_SQUARE_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"

#include "plaits/resources.h"

namespace plaits {

// A naive square at +-0.5 (the voice's suboscillator), on a 32-bit integer
// phase: the top bit is the square and overflow is the wrap, so each sample
// is a few integer instructions. The float phase this replaced needed two
// float compares per sample, and each one stalls the M4 pipeline to move the
// FPU flags. The frequency is ramped across the block as before (the same
// linear interpolation, in increments).
class SquareOscillator {
 public:
  SquareOscillator() { }
  ~SquareOscillator() { }

  void Init() {
    phase_ = 0;
    increment_ = 0;
  }

  // Out of line and not unrolled: inlined into Voice::Render, register
  // pressure spilled `step` to the stack and reloaded it every sample, and
  // -funroll-loops added an eight-way entry dispatch for a 12-sample block.
#if defined(__clang__)
  __attribute__((noinline)) void Render(
#else
  __attribute__((noinline, optimize("no-unroll-loops"))) void Render(
#endif
      float frequency, float* out, size_t size) {
    if (size == 0) {
      return;
    }
    if (frequency < 0.0f) {
      frequency = 0.0f;
    }
    // At most just under 0.5 cycles/sample (2^31), so the block's change in
    // increment always fits the signed step.
    uint32_t target = frequency >= 0.5f
        ? 0x7fffffffu
        : static_cast<uint32_t>(frequency * 4294967296.0f);
    if (target > 0x7fffffffu) {
      target = 0x7fffffffu;
    }
    const int32_t step = static_cast<int32_t>(target - increment_) /
        static_cast<int32_t>(size);
    uint32_t increment = increment_;
    uint32_t phase = phase_;
    // Write the float's bits as a word. memcpy into a float made gcc 4.8 round-
    // trip every sample through the stack (store, reload, store: about 12
    // instructions a sample with loop overhead); a may_alias word store is one
    // instruction, and may_alias keeps it defined for the float reads that
    // follow in Voice::Render.
    typedef uint32_t __attribute__((__may_alias__)) FloatBits;
    // An end pointer, not a down-counter: gcc 4.8 kept two copies of the
    // counter, an extra instruction a sample. size > 0 is checked above.
    FloatBits* words = reinterpret_cast<FloatBits*>(out);
    FloatBits* const end = words + size;
    do {
      increment += step;
      phase += increment;
      // +0.5f is 0x3f000000; setting the sign bit makes it -0.5f.
      *words++ = 0x3f000000u | (phase & 0x80000000u);
    } while (words != end);
    phase_ = phase;
    increment_ = target;
  }

 private:
  uint32_t phase_;
  uint32_t increment_;

  DISALLOW_COPY_AND_ASSIGN(SquareOscillator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_SQUARE_OSCILLATOR_H_
