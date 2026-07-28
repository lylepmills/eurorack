// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' four "digital filter" models (ZLPF, ZPKF, ZBPF, ZHPF) in one slot.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderDigitalFilter: a
// sine "resonator" burst re-triggered once per carrier cycle, windowed by a
// ramp or triangle, mixed against a polarity-flipped pulse train and its
// integral. RenderDigitalFilter carries no `size -= 2`, so it is a native
// 96 kHz algorithm; this port runs the same loop at 2x the 48 kHz output rate
// and decimates, which is why every rate constant transfers verbatim.
//
// OUT: the model HARMONICS selects. AUX: its complement (LP<->HP, PK<->BP) at
// matched gain, so OUT/AUX are decorrelated with no second render path.
//
// Verified against Braids: rendered side by side at MIDI 48 through the same
// decimator, all four models agree to within 0.05 dB of AC RMS and 0.26 dB
// mean across third-octave bands, at +5 cents (kCorrectedSampleRate).
//
// Declared deviations from Braids:
//   - lut_sine is read at 512 points/period against Braids' 257-entry
//     wav_sine at an 8-bit index (256 points/period). Cleaner, not identical.
//   - DC differs (LP -0.19 against -0.20, PK -0.10 against -0.25). Braids
//     accumulates the integrator through an arithmetic shift, which FLOORS
//     every sample and walks the accumulator negative until the clip catches
//     it; and its `window * (carrier + 32768)` overflows int32 for wide
//     windows at a high carrier. Both are fixed-point artefacts with no
//     audible AC component, and the second is undefined behaviour, so neither
//     is reproduced.
//   - the burst envelopes are raised to a MACRO-controlled power; at the
//     MACRO detent the exponent is exactly 1 and the window is Braids' ramp.
//   - AUX reads the complementary model off the SELECTED model's resonator
//     phases rather than seeding its own from kPhaseReset. The four models
//     differ in two ways -- their output combination and their reset phases --
//     and only the combination is free once the phases have been advanced.
//     Running a second set of phases measured at 94% of the CPU budget under
//     qemu/estimate.py; sharing them costs ~5 operations and lands near half
//     that. OUT, the model the user selected, is unaffected: it is Braids'.

#ifndef PLAITS_DSP_ENGINE2_Z_FILTER_ENGINE_H_
#define PLAITS_DSP_ENGINE2_Z_FILTER_ENGINE_H_

#include "stmlib/dsp/hysteresis_quantizer.h"

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' kPhaseReset, normalized. Index by filter type for the windowed
// resonator, by (filter_type & 1) + 2 for the pulse resonator.
const float kZFilterPhaseReset[4] = { 0.0f, 0.5f, 0.25f, 0.5f };

// Braids clamps the modulated pitch to 16383 (MIDI 127.99, 13.29 kHz). The
// port applies the same ceiling, expressed at the 96 kHz internal rate.
const float kZFilterMaxPitch = 128.0f;

// TIMBRE spans Braids' (parameter_[0] - 2048) >> 1 in 1/128-semitone units.
const float kZFilterCutoffOffset = -8.0f;
const float kZFilterCutoffRange = 128.0f;

class ZFilterEngine : public Engine {
 public:
  ZFilterEngine() { }
  ~ZFilterEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: OUT and AUX are two different filter models sharing one
  // resonator, already decorrelated at matched gain. No second render path,
  // so no PLAITS_STEREO_Z_FILTER gate.
  virtual bool stereo_capable() const { return true; }

 private:
  float phase_;
  float mod_increment_;
  bool previous_half_;

  float modulator_phase_;
  float square_phase_;
  float integrator_;
  bool polarity_;

  // 3-tap [0.25, 0.5, 0.25] decimator history: the last sub-sample of the
  // previous output period, per stream.
  float out_decimator_;
  float aux_decimator_;

  stmlib::HysteresisQuantizer2 model_quantizer_;

  DISALLOW_COPY_AND_ASSIGN(ZFilterEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_Z_FILTER_ENGINE_H_
