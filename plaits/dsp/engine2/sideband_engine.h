// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Finite discrete-summation-formula sideband engine.
//
// OUT: upper sideband bank. AUX: lower sideband bank. In stereo mode,
// OUT/AUX become L/R: both channels carry the full spectrum with a
// complementary 85/15 power balance — the left channel favours the upper
// bank, the right channel the lower bank — so neither side of the image
// loses half of the sound.

#ifndef PLAITS_DSP_ENGINE2_SIDEBAND_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SIDEBAND_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class SidebandEngine : public Engine {
 public:
  SidebandEngine() { }
  ~SidebandEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_SIDEBAND_BANK; }

 private:
  float carrier_phase_;
  float sideband_phase_;

  DISALLOW_COPY_AND_ASSIGN(SidebandEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SIDEBAND_ENGINE_H_
