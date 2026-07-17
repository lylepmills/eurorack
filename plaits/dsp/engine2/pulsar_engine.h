// Copyright 2026 Lyle Mills.
//
// Pulsar synthesis engine.

#ifndef PLAITS_DSP_ENGINE2_PULSAR_ENGINE_H_
#define PLAITS_DSP_ENGINE2_PULSAR_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class PulsarEngine : public Engine {
 public:
  PulsarEngine() { }
  ~PulsarEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  float phase_;
  float formant_;
  float duty_;
  float cluster_;
  float skew_;

  DISALLOW_COPY_AND_ASSIGN(PulsarEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_PULSAR_ENGINE_H_
