// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
// A predictable oscillator for testing the host's controls independently of
// the selected engine's tuning, envelope, noise, or fixed-frequency partials.
#ifndef PLAITS_LAB_VCV_PROBE_ENGINE_H_
#define PLAITS_LAB_VCV_PROBE_ENGINE_H_

#include <cmath>
#include "plaits/dsp/engine/engine.h"

namespace plaits {

inline EngineParameters& LastProbeParameters() {
  static EngineParameters parameters;
  return parameters;
}

class ProbeEngine {
 public:
  void Init(stmlib::BufferAllocator*) { phase_ = 0.0f; }
  void LoadUserData(const uint8_t*) { }
  void Reset() { phase_ = 0.0f; }
  void Render(const EngineParameters& p, float* out, float* aux,
              size_t size, bool*) {
    LastProbeParameters() = p;
    const float increment = 440.0f / 48000.0f * std::pow(2.0f, (p.note - 69.0f) / 12.0f);
    for (size_t i = 0; i < size; ++i) {
      phase_ += increment;
      phase_ -= std::floor(phase_);
      out[i] = aux[i] = 0.25f * std::sin(phase_ * 6.28318530718f);
    }
  }
 private:
  float phase_;
};

}  // namespace plaits
#endif
