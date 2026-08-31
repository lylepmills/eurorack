// Copyright 2026 Combust.
// SPDX-License-Identifier: MIT
//
// 1-Bit PFM — Tim Follin-style pulse-frequency-modulation click synthesis.
// HARMONICS selects the chord through the firmware's ChordBank (so Palette
// chord tables apply), MORPH builds the stack (low half) or runs the arp
// with continuous rate (high half), TIMBRE is pin width (the level control),
// and macro is exaggerated pitch pull (CCW) / echo (CW), neutral at noon
// with the authentic <=3-cent pull always on. Voiced two octaves below the
// panel pitch: pin trains carry weak fundamentals, so the register is chosen
// by ear (hardware pass, 2026-08-14).

#ifndef PLAITS_DSP_ENGINE2_ZXPULSE48K_ENGINE_H_
#define PLAITS_DSP_ENGINE2_ZXPULSE48K_ENGINE_H_

#ifndef PLAITS_STEREO_ZXPULSE48K
#define PLAITS_STEREO_ZXPULSE48K 1
#endif

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/chords/chord_bank.h"

#include "plaits/dsp/engine2/pulse_core.h"

namespace plaits {

class ZxPulse48kEngine : public Engine {
 public:
  ZxPulse48kEngine() { }
  ~ZxPulse48kEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void LoadUserData(const uint8_t* user_data) { }
  virtual bool stereo_capable() const { return PLAITS_STEREO_ZXPULSE48K; }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);

 private:
  static const int kNumVoices = 5;
  static const int kHistorySize = 16;

  ChordBank chords_;
  pulse::PinChannel channels_[kNumVoices];
  pulse::FrameTick tick_;

  bool arp_mode_;        // hysteretic MORPH halves: stack vs arp
  int stack_count_;      // hysteretic voice count in stack mode
  uint32_t tick_count_;
  int arp_index_;

  // Per-tick history of the lead voice's frequency/width, read by the echo.
  float dt_history_[kHistorySize];
  float width_history_[kHistorySize];
  int history_pos_;

  float pull_;           // smoothed pitch-pull amount, semitones

  DISALLOW_COPY_AND_ASSIGN(ZxPulse48kEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_ZXPULSE48K_ENGINE_H_
