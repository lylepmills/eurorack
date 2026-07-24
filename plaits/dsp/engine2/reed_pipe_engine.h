// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Lightweight nonlinear reed and fractional-delay bore engine.
//
// OUT: bore pressure read through a movable pickup, DC-blocked and soft-clipped.
// AUX: the reed flow, given make-up gain. In stereo mode, OUT/AUX become L/R:
// the waveguide runs once and a second pickup, mirrored down the bore
// (1 - pickup_position), reads the same standing wave from a different point,
// so the two taps a fixed distance apart give genuine spatial stereo; the
// reed-flow AUX is skipped.

#ifndef PLAITS_DSP_ENGINE2_REED_PIPE_ENGINE_H_
#define PLAITS_DSP_ENGINE2_REED_PIPE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/physical_modelling/delay_line.h"

namespace plaits {

const size_t kReedPipeDelaySize = 2048;

// Rest opening of the reed, held clear of both stops so that the static
// breath pressure can never park the reed shut.
const float kReedPipeRestOpening = 0.40f;

// Stiffness acts on the oscillating part of the pressure difference only, so
// it can span a wide range without moving the reed's operating point.
const float kReedPipeStiffnessMin = 2.0f;
const float kReedPipeStiffnessRange = 12.0f;

// Reflection-filter corner, in harmonics of the played note. Tracking pitch
// keeps MACRO, MORPH and TIMBRE responsive across the whole keyboard; with a
// fixed coefficient they collapsed on low notes.
const float kReedPipeBrightnessMin = 1.0f;
const float kReedPipeBrightnessRange = 12.0f;

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
  virtual bool stereo_capable() const { return PLAITS_STEREO_REED_PIPE; }

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
