// Copyright 2026 Lyle Mills.
//
// Glisson/granular chirp synthesis engine.

#ifndef PLAITS_DSP_ENGINE2_GLISSON_ENGINE_H_
#define PLAITS_DSP_ENGINE2_GLISSON_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Five grains keep the densest setting inside a safer real-time CPU budget on
// the STM32F3 while still producing a continuous cloud.
const int kNumGlissonGrains = 5;

class GlissonEngine : public Engine {
 public:
  GlissonEngine() { }
  ~GlissonEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  struct Grain {
    float phase;
    float aux_phase;
    float envelope_phase;
    float envelope_increment;
    float start_ratio;
    float end_ratio;
  };

  void StartGrain(
      Grain* grain,
      float scatter,
      float direction,
      float duration,
      float delay);

  Grain grain_[kNumGlissonGrains];
  int num_grains_;
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(GlissonEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_GLISSON_ENGINE_H_
