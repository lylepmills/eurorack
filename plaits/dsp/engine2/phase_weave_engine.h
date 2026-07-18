// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Phase-cancellation oscillator bank.

#ifndef PLAITS_DSP_ENGINE2_PHASE_WEAVE_ENGINE_H_
#define PLAITS_DSP_ENGINE2_PHASE_WEAVE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class PhaseWeaveEngine : public Engine {
 public:
  PhaseWeaveEngine() { }
  ~PhaseWeaveEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  float phase_;
  float constellation_;
  float spread_;
  float cancellation_;
  float rotation_;

  DISALLOW_COPY_AND_ASSIGN(PhaseWeaveEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_PHASE_WEAVE_ENGINE_H_
