// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Normalized feedback-amplitude-modulation engine.

#ifndef PLAITS_DSP_ENGINE2_LOOPBACK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_LOOPBACK_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// MORPH drives phase feedback into the feedback oscillator, in cycles at full
// index. This is the classic feedback-operator sweep (sine -> bright buzz) and
// is a far stronger timbral lever than the spectral tilt it replaced, which
// barely perturbed the AM depth.
const float kLoopbackFeedbackDepth = 0.5f;

// OUT: AM carrier body (main). AUX: sideband product. In stereo mode OUT/AUX
// become L/R and the two components are panned apart (main to 0.35, sidebands
// to 0.65), each keeping its own scale so the timbre is preserved; both reach
// both channels so a mono sum stays in phase.
class LoopbackEngine : public Engine {
 public:
  LoopbackEngine() { }
  ~LoopbackEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return true; }

 private:
  float carrier_phase_;
  float feedback_phase_;
  float feedback_state_;
  float fb_osc_;
  float ratio_;
  float depth_;
  float morph_;
  float polarity_;

  DISALLOW_COPY_AND_ASSIGN(LoopbackEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_LOOPBACK_ENGINE_H_
