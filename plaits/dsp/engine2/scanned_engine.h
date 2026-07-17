// Copyright 2026 Lyle Mills.
//
// Scanned synthesis engine.

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

  float position_[kScannedMasses];
  float velocity_[kScannedMasses];
  float scan_phase_;
  float physics_phase_;
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(ScannedEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SCANNED_ENGINE_H_
