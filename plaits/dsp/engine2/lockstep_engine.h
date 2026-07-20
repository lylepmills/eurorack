// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Internal phase-locked-loop synthesis engine.

#ifndef PLAITS_DSP_ENGINE2_LOCKSTEP_ENGINE_H_
#define PLAITS_DSP_ENGINE2_LOCKSTEP_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// MACRO's steady-state voice: reference feedthrough FM index, in cycles. A
// real PLL never fully suppresses ripple at the reference on its control line,
// so the VCO is continuously FM'd. Turning MACRO up loosens the loop and lets
// more feedthrough survive, which is audible while a note is held -- not just
// during the capture transient the damping/capture terms already shape.
const float kLockstepFeedthrough = 0.35f;

class LockstepEngine : public Engine {
 public:
  LockstepEngine() { }
  ~LockstepEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  float reference_phase_;
  float follower_phase_;
  float follower_frequency_;
  float detector_lp_;
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(LockstepEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_LOCKSTEP_ENGINE_H_
