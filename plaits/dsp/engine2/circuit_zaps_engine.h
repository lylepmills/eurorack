// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_DSP_ENGINE2_CIRCUIT_ZAPS_ENGINE_H_
#define PLAITS_DSP_ENGINE2_CIRCUIT_ZAPS_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class CircuitZapsEngine : public Engine {
 public:
  CircuitZapsEngine() { }
  ~CircuitZapsEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return true; }

 private:
  float phase_a_;
  float phase_b_;
  float body_envelope_;
  float sweep_ratio_;
  float spark_envelope_;
  float spark_lowpass_;
  float spark_lowpass_aux_;
  float out_dc_;
  float aux_dc_;

  DISALLOW_COPY_AND_ASSIGN(CircuitZapsEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_CIRCUIT_ZAPS_ENGINE_H_
