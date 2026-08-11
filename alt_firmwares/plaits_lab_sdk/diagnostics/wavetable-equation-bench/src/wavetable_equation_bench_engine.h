// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_LAB_WAVETABLE_EQUATION_BENCH_ENGINE_H_
#define PLAITS_LAB_WAVETABLE_EQUATION_BENCH_ENGINE_H_

#include <stdint.h>

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"

namespace plaits {

class WavetableEquationBenchEngine : public Engine {
 public:
  WavetableEquationBenchEngine() { }
  ~WavetableEquationBenchEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters, float* out,
      float* aux, size_t size, bool* already_enveloped);

 private:
  float ReadSampledBank(float x, float y, float phase);

  float phase_;
  float x_lp_;
  float y_lp_;
  float previous_x_;
  float previous_y_;
  float previous_f0_;
  uint32_t sequence_samples_;
  Differentiator differentiator_;

  DISALLOW_COPY_AND_ASSIGN(WavetableEquationBenchEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_WAVETABLE_EQUATION_BENCH_ENGINE_H_
