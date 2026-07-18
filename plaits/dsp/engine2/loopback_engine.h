// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Normalized feedback-amplitude-modulation engine.

#ifndef PLAITS_DSP_ENGINE2_LOOPBACK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_LOOPBACK_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

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

 private:
  enum {
    kDelaySize = 64
  };

  void ClearDelay();
  float ReadDelay(float delay) const;

  float delay_line_[kDelaySize];
  int write_index_;
  float carrier_phase_;
  float feedback_phase_;
  float ratio_;
  float depth_;
  float delay_;
  float polarity_;

  DISALLOW_COPY_AND_ASSIGN(LoopbackEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_LOOPBACK_ENGINE_H_
