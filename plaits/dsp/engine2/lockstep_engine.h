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

// OUT: the follower oscillator (soft-square shaped). AUX: charge-pump + the
// reference/follower difference, which fades to silence at a clean lock. In
// stereo mode OUT/AUX become L/R and the fading AUX is dropped; RIGHT instead
// carries a matching soft-square shaping of the REFERENCE oscillator, so the
// two channels are two locked oscillators (OUT to 0.3, reference to 0.7) that
// spread on non-integer ratios and converge at lock. Both reach both channels
// (equal-power), so a mono sum stays in phase.
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
  virtual bool stereo_capable() const { return PLAITS_STEREO_LOCKSTEP; }

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
