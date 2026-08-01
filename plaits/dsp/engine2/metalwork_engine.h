// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_DSP_ENGINE2_METALWORK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_METALWORK_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kMetalworkNumModes = 6;

class MetalworkEngine : public Engine {
 public:
  MetalworkEngine() { }
  ~MetalworkEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  int object_;
  float phase_a_[kMetalworkNumModes];
  float phase_b_[kMetalworkNumModes];
  float amplitude_[kMetalworkNumModes];
  float strike_envelope_;
  float strike_lowpass_;

  DISALLOW_COPY_AND_ASSIGN(MetalworkEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_METALWORK_ENGINE_H_
