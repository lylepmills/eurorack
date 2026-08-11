// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_LAB_TERRAIN_EQUATION_BENCH_ENGINE_H_
#define PLAITS_LAB_TERRAIN_EQUATION_BENCH_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

class TerrainEquationBenchEngine : public Engine {
 public:
  TerrainEquationBenchEngine() { }
  ~TerrainEquationBenchEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters, float* out,
      float* aux, size_t size, bool* already_enveloped);

 private:
  FastSineOscillator path_;
  float offset_;
  float y_offset_;
  float* temp_buffer_;

  DISALLOW_COPY_AND_ASSIGN(TerrainEquationBenchEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_TERRAIN_EQUATION_BENCH_ENGINE_H_
