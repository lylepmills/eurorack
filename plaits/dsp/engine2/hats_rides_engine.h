// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_DSP_ENGINE2_HATS_RIDES_ENGINE_H_
#define PLAITS_DSP_ENGINE2_HATS_RIDES_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "stmlib/dsp/filter.h"

namespace plaits {

// Hi-hats, rides and crashes aimed at the 606 and 909: an analog six-square
// cluster crossfading into a denser, ring-modulated cluster that is read back
// through a 6-bit, 32 kHz "sample" stage the way the 909's cymbal ROMs were.
class HatsRidesEngine : public Engine {
 public:
  HatsRidesEngine() { }
  ~HatsRidesEngine() { }

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
  struct Channel {
    stmlib::Svf body;
    stmlib::Svf high_pass;
    float hold_phase;
    float held;
  };

  void InitChannel(Channel* channel, float hold_phase);
  float ProcessChannel(Channel* channel, float source, float crush,
      float* body);

  uint32_t phase_[12];
  float envelope_;
  float strike_;
  float sustain_gain_;
  Channel left_;
  Channel right_;

  DISALLOW_COPY_AND_ASSIGN(HatsRidesEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_HATS_RIDES_ENGINE_H_
