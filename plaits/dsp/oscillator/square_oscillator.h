// Dedicated minimal square wave oscillator

#ifndef PLAITS_DSP_OSCILLATOR_SQUARE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_SQUARE_OSCILLATOR_H_

#include <string.h>

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

  void Render(float frequency, float* out, size_t size) {
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
    while (size--) {
      increment += step;
      phase += increment;
      // +0.5f is 0x3f000000; setting the sign bit makes it -0.5f.
      const uint32_t bits = 0x3f000000u | (phase & 0x80000000u);
      memcpy(out++, &bits, sizeof(bits));
    }
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
