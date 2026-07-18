// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Mean-field coupled phase-oscillator synthesis engine.

#ifndef PLAITS_DSP_ENGINE2_PHASE_FLOCK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_PHASE_FLOCK_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kNumPhaseFlockOscillators = 7;

class PhaseFlockEngine : public Engine {
 public:
  PhaseFlockEngine() { }
  ~PhaseFlockEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  void Scatter();

  float phase_[kNumPhaseFlockOscillators];
  int scatter_count_;
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(PhaseFlockEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_PHASE_FLOCK_ENGINE_H_
