// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Mean-field coupled phase-oscillator synthesis engine.
//
// OUT: first circular moment of the flock (mean of sines). AUX: second
// circular moment, an octave-flavoured projection that survives two-cluster
// cancellation. In stereo mode, OUT/AUX become L/R: each oscillator holds a
// fixed equal-power pan position spread across the field in detune order,
// with the near-zero-detune oscillator centred, and both channels reuse the
// mono moment's normalization so loudness matches. Synchronization audibly
// collapses the stereo width toward mono — that emergent behaviour is the
// point of this mode.

#ifndef PLAITS_DSP_ENGINE2_PHASE_FLOCK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_PHASE_FLOCK_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kNumPhaseFlockOscillators = 7;

class PhaseFlockEngine : public Engine {
 public:
  PhaseFlockEngine() { }
  ~PhaseFlockEngine() { }

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
  void Scatter();

  float phase_[kNumPhaseFlockOscillators];
  int scatter_count_;
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(PhaseFlockEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_PHASE_FLOCK_ENGINE_H_
