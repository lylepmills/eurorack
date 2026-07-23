// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Pulsar synthesis engine.
//
// OUT: the main pulsaret train (cluster window x formant carrier). AUX: a
// second train whose cluster window is skew-complemented and offset by half
// a period, with a higher formant carrier. The two trains share a pitch but
// are otherwise decorrelated at matched gain, so stereo mode just relabels
// OUT/AUX as a left/right pair; the audio path is unchanged and Render()
// ignores EngineParameters::stereo.

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
  // The two pulsaret trains are inherently decorrelated (see header comment):
  // stereo mode just relabels the existing OUT/AUX pair as left/right.
  virtual bool stereo_capable() const { return true; }

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
