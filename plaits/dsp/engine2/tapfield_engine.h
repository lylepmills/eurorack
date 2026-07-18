// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Pitch-clocked binary feedback synthesis engine.

#ifndef PLAITS_DSP_ENGINE2_TAPFIELD_ENGINE_H_
#define PLAITS_DSP_ENGINE2_TAPFIELD_ENGINE_H_

#include <stdint.h>

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class TapfieldEngine : public Engine {
 public:
  TapfieldEngine() { }
  ~TapfieldEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  void ConfigureTopology(float harmonics);
  void Seed();
  void Clock(float corruption);
  float Decode(float timbre) const;

  uint32_t state_;
  uint32_t tap_mask_;
  uint32_t width_mask_;
  uint32_t clock_counter_;
  int register_length_;
  int tap_family_;
  float clock_phase_;
  float corruption_phase_;
  float target_;
  float value_;
  float aux_target_;

  DISALLOW_COPY_AND_ASSIGN(TapfieldEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_TAPFIELD_ENGINE_H_
