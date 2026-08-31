// Copyright 2026 Combust.
// SPDX-License-Identifier: MIT
//
// 1-Bit Phaser — ZX Spectrum beeper-style boolean interference synthesis
// (Shiru Phaser1 / utz PhaserX family). Two square oscillators run on an
// internal 1-bit bus at 4x the sample rate and are combined bitwise
// (XOR = boolean ring mod, OR/AND = Squeeker-style decouplings), then
// decimated by a plain 4-sample box average so partial fold-down survives —
// that is the timbre, not a defect.

#ifndef PLAITS_DSP_ENGINE2_ZXPHASE48K_ENGINE_H_
#define PLAITS_DSP_ENGINE2_ZXPHASE48K_ENGINE_H_

#ifndef PLAITS_STEREO_ZXPHASE48K
#define PLAITS_STEREO_ZXPHASE48K 1
#endif

#include "plaits/dsp/engine/engine.h"

#include "plaits/dsp/engine2/pulse_core.h"

namespace plaits {

class ZxPhase48kEngine : public Engine {
 public:
  ZxPhase48kEngine() { }
  ~ZxPhase48kEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void LoadUserData(const uint8_t* user_data) { }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_ZXPHASE48K; }

 private:
  pulse::FrameTick tick_;

  uint32_t phase1_;
  uint32_t phase2_;
  uint32_t ghost_phase_;
  uint16_t lfsr_;

  int zone_;             // hysteretic mix-method zone: 0 XOR, 1 OR, 2 AND
  int interval_;         // hysteretic MORPH interval-ladder step
  bool noise_mode_;      // hysteretic MORPH extreme: osc2 becomes an LFSR

  // SID-style tick-clocked duty sweep on osc2 (4-bit quantized duty).
  int duty2_index_;
  int sweep_direction_;
  uint32_t sweep_count_;

  // DC blockers for MAIN and AUX.
  float dc_main_;
  float dc_aux_;

  // 1-bit self-XOR feedback: a 512-bit ring of past bus bits at the
  // internal rate — the module finally earns its name by interfering with
  // itself.
  uint32_t feedback_bits_[16];
  uint32_t feedback_pos_;

  // TRIG kick-slide state, in semitones above played pitch.
  float kick_;

  // A second full 4x boolean bus exceeds the M4 deadline. Two short
  // magnitude-preserving phase networks retain the spectrum while giving the
  // channels different phase responses at a fraction of that cost.
  StereoPhaseAllpass<7> stereo_phase_left_;
  StereoPhaseAllpass<13> stereo_phase_right_;
  bool stereo_was_active_;

  DISALLOW_COPY_AND_ASSIGN(ZxPhase48kEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_ZXPHASE48K_ENGINE_H_
