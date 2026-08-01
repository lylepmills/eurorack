// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_DSP_ENGINE2_HANDS_STICKS_ENGINE_H_
#define PLAITS_DSP_ENGINE2_HANDS_STICKS_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kHandsSticksNumModes = 3;

class HandsSticksEngine : public Engine {
 public:
  HandsSticksEngine() { }
  ~HandsSticksEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  int instrument_;
  int bursts_remaining_;
  int burst_timer_;
  int burst_spacing_;
  float phase_[kHandsSticksNumModes];
  float amplitude_[kHandsSticksNumModes];
  float burst_envelope_;
  float tail_envelope_;
  float click_envelope_;
  float noise_lowpass_;

  DISALLOW_COPY_AND_ASSIGN(HandsSticksEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_HANDS_STICKS_ENGINE_H_
