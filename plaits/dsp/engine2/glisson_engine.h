// Copyright 2026 Lyle Mills.
//
// Glisson/granular chirp synthesis engine.
//
// OUT: grains gliding along their pitch trajectory. AUX: the same grains
// with the glide reversed. In stereo mode, OUT/AUX become left/right: each
// grain picks a random pan trajectory whose endpoints mirror across the
// center, so grains sweep the stereo field the way they sweep pitch.

#ifndef PLAITS_DSP_ENGINE2_GLISSON_ENGINE_H_
#define PLAITS_DSP_ENGINE2_GLISSON_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Four grains keep the densest MONO setting below the practical 90% red line
// on the STM32F3. Stereo is cheaper because it pans one chirp rather than
// rendering separate forward/reverse chirps, and four still form a continuous
// cloud at the top of TIMBRE.
const int kNumGlissonGrains = 4;

class GlissonEngine : public Engine {
 public:
  GlissonEngine() { }
  ~GlissonEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_GLISSON; }

 private:
  struct Grain {
    float phase;
    float aux_phase;
    float envelope_phase;
    float envelope_increment;
    float start_ratio;
    float end_ratio;
    float start_gain_left;
    float start_gain_right;
    float end_gain_left;
    float end_gain_right;
  };

  void StartGrain(
      Grain* grain,
      float scatter,
      float direction,
      float duration,
      float delay,
      bool stereo);

  Grain grain_[kNumGlissonGrains];
  int num_grains_;
  bool reset_pending_;

  DISALLOW_COPY_AND_ASSIGN(GlissonEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_GLISSON_ENGINE_H_
