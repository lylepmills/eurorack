// Dedicated minimal square wave oscillator

#ifndef PLAITS_DSP_OSCILLATOR_SQUARE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_SQUARE_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/resources.h"

namespace plaits {

class SquareOscillator {
 public:
  SquareOscillator() { }
  ~SquareOscillator() { }

  void Init() {
    phase_ = 0.0f;
    frequency_ = 0.0f;
  }
  
  void Render(float frequency, float* out, size_t size) {
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }
    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);

    while (size--) {
      phase_ += fm.Next();
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      }

      if (phase_ < 0.5f) {
        *out++ = 0.5f;
      } else {
        *out++ = -0.5f;
      }
    }
  }
  
  // Oscillator state.
  float phase_;

  // For interpolation of parameters.
  float frequency_;
  
  DISALLOW_COPY_AND_ASSIGN(SquareOscillator);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_SQUARE_OSCILLATOR_H_
