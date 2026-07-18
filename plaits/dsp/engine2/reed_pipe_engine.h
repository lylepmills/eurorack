// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Lightweight nonlinear reed and fractional-delay bore engine.

#ifndef PLAITS_DSP_ENGINE2_REED_PIPE_ENGINE_H_
#define PLAITS_DSP_ENGINE2_REED_PIPE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/physical_modelling/delay_line.h"

namespace plaits {

const size_t kReedPipeDelaySize = 2048;

class ReedPipeEngine : public Engine {
 public:
  ReedPipeEngine() { }
  ~ReedPipeEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  DelayLine<float, kReedPipeDelaySize> bore_;

  float delay_;
  float return_filter_;
  float outgoing_;
  float breath_envelope_;
  float excitation_;
  float reflection_coefficient_;
  float reflection_brightness_;
  float reed_stiffness_;
  float breath_noise_;
  float pickup_position_;
  float pickup_amount_;
  float out_dc_input_;
  float out_dc_output_;
  float aux_dc_input_;
  float aux_dc_output_;
  uint32_t noise_state_;

  DISALLOW_COPY_AND_ASSIGN(ReedPipeEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_REED_PIPE_ENGINE_H_
