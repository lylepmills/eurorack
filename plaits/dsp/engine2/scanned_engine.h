// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Scanned synthesis engine.
//
// OUT: interpolated scan of a 32-mass circular spring string, blended toward
// its own spatial derivative by TIMBRE and then sine-wavefolded by MORPH.
// AUX: the raw spatial derivative. In stereo mode, OUT/AUX become L/R: two
// pickups a quarter of the scan span apart read the same string through the
// identical readout and wavefolder, and the derivative output is skipped.

#ifndef PLAITS_DSP_ENGINE2_SCANNED_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SCANNED_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kScannedMasses = 32;
// ReadPickup wraps mass indices with a mask. (The firmware is C++98, hence
// stmlib's macro rather than static_assert.)
STATIC_ASSERT((kScannedMasses & (kScannedMasses - 1)) == 0,
              scanned_masses_must_be_a_power_of_two);

class ScannedEngine : public Engine {
 public:
  ScannedEngine() { }
  ~ScannedEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_SCANNED; }

#if PLAITS_BUILD_EXTENDED_TZFM
  // Qualified separately from the stock Plaits catalog's CPU policy.
  virtual bool linear_tzfm_capable() const { return true; }
#endif

 private:
  void Excite(float position, float width, float amount);
  void Step(
      float inharmonicity,
      float structure,
      float damping,
      float nonlinearity,
      bool driven,
      int drive_index);
  float ReadPickup(
      float phase,
      float timbre,
      float fold_amount,
      float* derivative) const;

  float position_[kScannedMasses];
  float velocity_[kScannedMasses];
  // 1 / mass for each mass, for the (inharmonicity, structure) it was computed
  // for: Step() runs every few blocks at high TWIST, and dividing by all 32
  // masses there made its blocks the engine's worst.
  float inverse_mass_[kScannedMasses];
  float mass_inharmonicity_;
  float mass_structure_;
  float scan_phase_;
  float physics_phase_;
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(ScannedEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SCANNED_ENGINE_H_
