// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Three-state cyclic attractor synthesis engine.

#ifndef PLAITS_DSP_ENGINE2_ATTRACTOR_ENGINE_H_
#define PLAITS_DSP_ENGINE2_ATTRACTOR_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class AttractorEngine : public Engine {
 public:
  AttractorEngine() { }
  ~AttractorEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  void Seed(float accent);
  void Step(
      float rate_x,
      float rate_y,
      float rate_z,
      float damping,
      float argument_scale);

  float state_[3];
  float dc_[3];
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(AttractorEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_ATTRACTOR_ENGINE_H_
