// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' saw-into-comb hybrid: a band-limited exciter through a comb whose
// pitch is decoupled from the played note.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderSawComb -- an
// AnalogOscillator saw written into the buffer, then
// DigitalOscillator::RenderComb filtering it in place. RenderComb carries no
// `size -= 2`, so it is a 96 kHz algorithm; a 4,096-tap line at 48 kHz
// reproduces Braids' 8,192 taps at 96 kHz exactly -- the same 85.3 ms and the
// same 11.72 Hz floor, so the bottom of TIMBRE clamps below MIDI ~70 just as
// it does on hardware.
//
// What distinguishes this from reed-pipe and loopback, which are the two
// palette neighbours: the comb pitch is decoupled +-64 semitones from the
// note, the feedback is genuinely BIPOLAR, and the exciter is band-limited.
//
// MORPH morphs the exciter from saw to pulse and MACRO tilts the loop; both
// are new. At the MACRO detent the loop is flat, which is Braids.
//
// AT HARMONICS NOON THE ENGINE IS NOT SILENT. The write-back is 0.5*in and
// the output is 0.5*dry + one echo -- a fully audible FIR comb. Resonance
// runs from inverted, through that, to ringing.
//
// DC, stated correctly: a comb attenuates DC by 1/(1+|g|) when the feedback
// is NEGATIVE and AMPLIFIES it by 1/(1-g) when positive, so DC parking is a
// POSITIVE-feedback hazard. The damping half of MACRO has unity gain at DC
// and so bounds nothing; only the in-loop clip does, exactly as in Braids.

#ifndef PLAITS_DSP_ENGINE2_SAW_COMB_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SAW_COMB_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/variable_shape_oscillator.h"

namespace plaits {

// 4,096 at 48 kHz == Braids' 8,192 at 96 kHz: 85.3 ms, floor 11.72 Hz.
const size_t kSawCombDelaySize = 4096;

// `pitch_ + ((parameter_[0] - 16384) >> 1)` in 1/128-semitone units.
const float kSawCombPitchRange = 64.0f;

// Braids smooths the comb pitch with `(15 * previous + pitch) >> 4`.
const float kSawCombPitchPole = 15.0f / 16.0f;

// The in-loop shelf. ONE_POLE at 0.35 has a Nyquist response of
// 0.35/1.65 = 0.212, so `x + (lp - x) * tilt` has an HF gain of
// (1 - 0.788 * tilt) -- 1.473 at the bright extreme, which is why the
// feedback needs the reciprocal compensation rather than a fixed pre-scale.
const float kSawCombShelfPole = 0.35f;
const float kSawCombShelfHf = 0.788f;
const float kSawCombTilt = 0.6f;

// 1 / SoftLimit(2), matching ring-mod: Braids warps the resonance knob
// through ws_moderate_overdrive, which is tanh(2x)/tanh(2).
const float kSawCombShaperNorm = 1.016129f;

class SawCombEngine : public Engine {
 public:
  SawCombEngine() { }
  ~SawCombEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: two comb taps a fifth apart on one line.
  virtual bool stereo_capable() const { return true; }

 private:
  VariableShapeOscillator exciter_;
  int16_t* line_;
  uint32_t write_pointer_;
  float comb_pitch_;
  float loop_lp_;

  DISALLOW_COPY_AND_ASSIGN(SawCombEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SAW_COMB_ENGINE_H_
