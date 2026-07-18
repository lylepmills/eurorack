// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Complex frequency-shift feedback synthesis engine.

#ifndef PLAITS_DSP_ENGINE2_SPECTRAL_SPIRAL_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SPECTRAL_SPIRAL_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kSpectralSpiralDelay = 32;

class SpectralSpiralEngine : public Engine {
 public:
  SpectralSpiralEngine() { }
  ~SpectralSpiralEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  void ClearLoop();

  float delay_i_[kSpectralSpiralDelay];
  float delay_q_[kSpectralSpiralDelay];
  int write_index_;
  float source_phase_;
  float shift_phase_;
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(SpectralSpiralEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SPECTRAL_SPIRAL_ENGINE_H_
