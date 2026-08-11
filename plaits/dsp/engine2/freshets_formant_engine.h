// Copyright 2026 Dylan Bolink.
// SPDX-License-Identifier: MIT
//
// Serge-style formant oscillator, ported from the Freshets fork of the Tides 2
// firmware (tides2/modulators/formant.h).

#ifndef PLAITS_DSP_ENGINE2_FRESHETS_FORMANT_ENGINE_H_
#define PLAITS_DSP_ENGINE2_FRESHETS_FORMANT_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

// stereo_config.h only carries flags for catalog engines, so a community
// package declares its own using the same naming rule (catalog id, upper-cased,
// '-' becomes '_'). Defaulting to 1 here is load-bearing: an *undefined* macro
// evaluates to 0 inside #if, which would silently dead-strip the stereo branch
// and make stereo_capable() false. The hosted builder can still force it off
// with -DPLAITS_STEREO_FRESHETS_FORMANT=0.
#ifndef PLAITS_STEREO_FRESHETS_FORMANT
#define PLAITS_STEREO_FRESHETS_FORMANT 1
#endif

#include "stmlib/dsp/filter.h"

namespace plaits {

class FreshetsFormantEngine : public Engine {
 public:
  FreshetsFormantEngine() { }
  ~FreshetsFormantEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void LoadUserData(const uint8_t* user_data) { }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_FRESHETS_FORMANT; }
  virtual bool fast_fm_capable() const { return true; }

 private:
  float driver_phase_;
  float env_phase_;
  float env_phase_r_;

  // Hollow (AUX) state
  float next_sample_hollow_;
  float next_sample_sub_;
  bool sub_state_;
  int prev_hollow_state_;

  // Per-block control smoothing.
  float prev_f0_;
  float prev_ratio_;
  float prev_pw_;
  float prev_shape_;

  float lp_1_;
  float lp_2_;
  float lp_1_r_;
  float lp_2_r_;

  stmlib::OnePole dc_blocker_[2];

  DISALLOW_COPY_AND_ASSIGN(FreshetsFormantEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_FRESHETS_FORMANT_ENGINE_H_
