// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_DSP_ENGINE2_SWEPT_KICK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SWEPT_KICK_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// A 909-style kick: a waveshaped VCO swept down by its own pitch envelope,
// with a separate click-and-noise attack layered on top. See the .cc file for
// how each control maps onto the original drum's TUNE, ATTACK and DECAY.
class SweptKickEngine : public Engine {
 public:
  SweptKickEngine() { }
  ~SweptKickEngine() { }

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
  float phase_;
  float pitch_envelope_;
  float amplitude_envelope_;
  float amplitude_smoothed_;
  int hold_samples_;
  float click_envelope_;
  float click_phase_;
  float noise_envelope_;
  float noise_lp_left_;
  float noise_hp_left_;
  float noise_lp_right_;
  float noise_hp_right_;
  float sustain_gain_;
  float out_dc_;
  float aux_dc_;

  DISALLOW_COPY_AND_ASSIGN(SweptKickEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SWEPT_KICK_ENGINE_H_
