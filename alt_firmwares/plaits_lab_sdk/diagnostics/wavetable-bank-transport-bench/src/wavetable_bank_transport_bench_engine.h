// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_LAB_WAVETABLE_BANK_TRANSPORT_BENCH_ENGINE_H_
#define PLAITS_LAB_WAVETABLE_BANK_TRANSPORT_BENCH_ENGINE_H_

#include <stdint.h>

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class WavetableBankTransportBenchEngine : public Engine {
 public:
  WavetableBankTransportBenchEngine() { }
  ~WavetableBankTransportBenchEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { (void) user_data; }
  virtual void Render(const EngineParameters& parameters, float* out,
      float* aux, size_t size, bool* already_enveloped);

 private:
  float phase_;
  float previous_f0_;
  float previous_harmonics_;
  float previous_timbre_;
  float previous_morph_;
  uint32_t sequence_samples_;

  DISALLOW_COPY_AND_ASSIGN(WavetableBankTransportBenchEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_WAVETABLE_BANK_TRANSPORT_BENCH_ENGINE_H_
