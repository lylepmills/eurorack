// Copyright 2026 Lyle Mills.
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
  virtual bool stereo_capable() const { return true; }

 private:
  void Excite(float position, float width, float amount);
  void Step(
      float inharmonicity,
      float structure,
      float damping,
      float nonlinearity,
      bool driven,
      int drive_index);
  float Scan(const float* data, float phase) const;
  float ReadPickup(
      float phase,
      float timbre,
      float fold_amount,
      float* derivative) const;

  float position_[kScannedMasses];
  float velocity_[kScannedMasses];
  float scan_phase_;
  float physics_phase_;
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(ScannedEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SCANNED_ENGINE_H_
