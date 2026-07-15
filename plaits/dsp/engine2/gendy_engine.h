// Copyright 2026 Lyle Mills.
//
// Dynamic stochastic (GENDY-inspired) synthesis engine.

#ifndef PLAITS_DSP_ENGINE2_GENDY_ENGINE_H_
#define PLAITS_DSP_ENGINE2_GENDY_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kMaxGendyBreakpoints = 12;

class GendyEngine : public Engine {
 public:
  GendyEngine() { }
  ~GendyEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  void Randomize(int num_breakpoints);
  void Mutate(float amplitude_step, float duration_step);
  void UpdateBoundaries();
  float Walk(float value, float amount, float minimum, float maximum);

  float amplitude_[kMaxGendyBreakpoints];
  float duration_[kMaxGendyBreakpoints];
  float boundary_[kMaxGendyBreakpoints];
  float phase_;
  int segment_;
  int num_breakpoints_;

  DISALLOW_COPY_AND_ASSIGN(GendyEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_GENDY_ENGINE_H_
