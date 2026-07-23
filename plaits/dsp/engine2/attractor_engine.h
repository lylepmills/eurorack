// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Three-state cyclic attractor synthesis engine.
//
// OUT: the MORPH-selected attractor coordinate. AUX: the same orbit one
// coordinate behind. Because the Thomas flow is cyclically symmetric, its
// three state variables are naturally decorrelated (roughly 120 degrees
// apart around the orbit), so OUT and AUX are already a matched-gain
// decorrelated pair. Stereo mode just relabels them as a left/right pair;
// the audio path is unchanged and Render() ignores EngineParameters::stereo.

#ifndef PLAITS_DSP_ENGINE2_ATTRACTOR_ENGINE_H_
#define PLAITS_DSP_ENGINE2_ATTRACTOR_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

class AttractorEngine : public Engine {
 public:
  AttractorEngine() { }
  ~AttractorEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // The two coordinate taps are inherently decorrelated (see header comment):
  // stereo mode just relabels the existing OUT/AUX pair as left/right.
  virtual bool stereo_capable() const { return true; }

 private:
  void Seed(float accent);
  void Step(
      float rate_x,
      float rate_y,
      float rate_z,
      float damping,
      float argument_scale);

  float state_[3];
  float dc_[3];
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(AttractorEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_ATTRACTOR_ENGINE_H_
