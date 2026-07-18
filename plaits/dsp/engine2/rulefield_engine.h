// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Cellular-automaton wavecycle engine.

#ifndef PLAITS_DSP_ENGINE2_RULEFIELD_ENGINE_H_
#define PLAITS_DSP_ENGINE2_RULEFIELD_ENGINE_H_

#include <stdint.h>

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class RulefieldEngine : public Engine {
 public:
  RulefieldEngine() { }
  ~RulefieldEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  static uint32_t RotateLeft(uint32_t value);
  static uint32_t RotateRight(uint32_t value);
  static float Density(uint32_t value);

  void Seed();
  void Evolve();
  void UpdateReadouts();
  float ReadWave(uint32_t row, float density, float phase, float shape) const;

  uint32_t row_;
  uint32_t previous_row_;
  uint32_t edge_row_;
  uint32_t activity_row_;
  uint32_t generation_;
  uint8_t rule_;
  float row_density_;
  float edge_density_;
  float activity_density_;
  float phase_;
  float evolution_phase_;

  DISALLOW_COPY_AND_ASSIGN(RulefieldEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_RULEFIELD_ENGINE_H_
