// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Undertone and subharmonic mixture engine.
//
// OUT: mix of the pitch anchor and five integer undertones, drawn from the
// selected registration. AUX: anchor-free lattice with alternating divider
// polarities. In stereo mode, OUT/AUX become L/R: each voice keeps its
// registration level but sits at a fixed equal-power pan position, with the
// pitch anchor and the deepest undertone at the centre and the midrange
// undertones spread widest; the alternating-polarity lattice is skipped.

#ifndef PLAITS_DSP_ENGINE2_UNDERTOW_ENGINE_H_
#define PLAITS_DSP_ENGINE2_UNDERTOW_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kNumUndertowVoices = 6;

class UndertowEngine : public Engine {
 public:
  UndertowEngine() { }
  ~UndertowEngine() { }

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
  void ComputeRegistration(float registration, float* amplitude) const;

  float phase_[kNumUndertowVoices];
  float frequency_[kNumUndertowVoices];
  float main_amplitude_[kNumUndertowVoices];
  float aux_amplitude_[kNumUndertowVoices];
  float colour_;

  DISALLOW_COPY_AND_ASSIGN(UndertowEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_UNDERTOW_ENGINE_H_
