// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Finite discrete-summation-formula sideband engine.

#ifndef PLAITS_DSP_ENGINE2_SIDEBAND_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SIDEBAND_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class SidebandEngine : public Engine {
 public:
  SidebandEngine() { }
  ~SidebandEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  float carrier_phase_;
  float sideband_phase_;

  DISALLOW_COPY_AND_ASSIGN(SidebandEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SIDEBAND_ENGINE_H_
