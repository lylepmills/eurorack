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
// reproduces Braids' 8,192 taps at 96 kHz -- 85.29 ms against 85.33 ms and an
// 11.73 Hz floor against 11.72 Hz, the difference being the two taps the
// interpolation guard reserves -- so the bottom of TIMBRE clamps below
// MIDI ~70 just as it does on hardware. Measured: the `comb-lowest` A/B case,
// which runs both sides into their clamp, is the tightest case in the file.
//
// What distinguishes this from reed-pipe and loopback, which are the two
// palette neighbours: the comb pitch is decoupled +-64 semitones from the
// note, the feedback is genuinely BIPOLAR, and the exciter is band-limited.
//
// MORPH morphs the exciter from saw to pulse and MACRO tilts the loop; both
// are new. At the MACRO detent the loop is flat, which is Braids. MORPH,
// which has no detent (SPEC R11), is Braids' OSC_SHAPE_SAW exciter at
// **MORPH 0**, not at noon: noon gives waveshape 0.75 and pw 0.375, a
// half-square at 37.5% duty that the module cannot make. Every A/B case
// therefore holds MORPH at 0.0 and MACRO at 0.5.
//
// AT HARMONICS NOON THE ENGINE IS NOT SILENT. The write-back is 0.5*in and
// the output is 0.5*dry + one echo -- a fully audible FIR comb. Resonance
// runs from inverted, through that, to ringing.
//
// DC, stated correctly: a comb attenuates DC by 1/(1+|g|) when the feedback
// is NEGATIVE and AMPLIFIES it by 1/(1-g) when positive, so DC parking is a
// POSITIVE-feedback hazard. The damping half of MACRO has unity gain at DC
// and so bounds nothing; only the in-loop clip does, exactly as in Braids.
//
// DECLARED DEVIATIONS, as measured by tests/ab.json (2026-07-29). These are
// reported, not fixed -- the code below is unchanged, and moving any of it is
// a digest move and a builder rollout, so it is Lyle's call.
//
//   1. COMB PITCH IS 11.72 CENTS SHARP OF THE MODULE. Braids smooths the comb
//      pitch with `filtered_pitch = (15 * filtered_pitch + pitch) >> 4`
//      (digital_oscillator.cc:250-252) on an integer in 1/128-semitone units.
//      `>> 4` floors, so every value in [p-15, p] is a fixed point: rising
//      from the 0 that Init() leaves in state_.ffm.previous_sample, the filter
//      STALLS 15 LSB = 11.72 cents FLAT of the requested pitch and stays
//      there for the life of the note. `comb_pitch_` below is a float pole
//      that converges exactly, so the port's comb delay is 0.680% short of
//      the module's. Measured at note 45 / TIMBRE noon: the module's
//      effective delay is 879.05 taps @96 kHz where ComputeDelay returns
//      873.12. Audibly this is a comb null the module has and the port does
//      not -- +10.18 dB in the 5-10 kHz band of the `stock` case, and the
//      dominant residual in every other case in the file.
//   2. THE OUTPUT CLIPPER IS SOFT WHERE BRAIDS' IS HARD. Braids ends on
//      `CLIP(out)` (digital_oscillator.cc:278); Render() below ends on
//      SoftClip, which is SoftLimit under +-3 and so attenuates everywhere,
//      not only at the rail: SoftLimit(1.0) = 0.778. Costs -1.25 dB AC RMS on
//      the `stock` case, where the reference peaks at 1.000 and the port at
//      0.775.
//      Reverting only 1 and 2 in a scratch build moves `stock` from
//      -1.25 dB / 1.10 dB spectrum to -0.00 dB / 0.39 dB, with every band
//      from 80 Hz to 10 kHz inside 0.5 dB.
//   3. THE BLOCK-RATE BOUND BELOW IS NOT A BACKSTOP; ON THE DAMPING HALF IT
//      IS WRONG AND BINDING. `worst_case = |feedback| * (1 + |tilt|)` assumes
//      the shelf peaks at 1 + |tilt|. It does not: H(z) = (1-tilt) +
//      tilt*L(z) is exactly 1 at DC for any tilt, so for tilt > 0 -- the
//      whole damping half -- its peak gain is 1.0, not 1.6. At HARMONICS 1
//      the bound therefore cuts feedback 1.000 -> 0.625 at MACRO 0, removing
//      4.08 dB of resonance range for no stability reason, and 0.679 ->
//      0.625 at MACRO 1. Only at the MACRO detent is it inert, which is why
//      no A/B case can see it.

#ifndef PLAITS_DSP_ENGINE2_SAW_COMB_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SAW_COMB_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/variable_shape_oscillator.h"

namespace plaits {

// 4,096 at 48 kHz == Braids' 8,192 at 96 kHz: 85.3 ms, floor 11.72 Hz.
const size_t kSawCombDelaySize = 4096;

// `pitch_ + ((parameter_[0] - 16384) >> 1)` in 1/128-semitone units.
const float kSawCombPitchRange = 64.0f;

// Braids smooths the comb pitch with `(15 * previous + pitch) >> 4`. The pole
// matches; the FLOOR does not -- see declared deviation 1 above. Braids'
// integer form settles 15 LSB (11.72 cents) flat of its target and this one
// settles on it.
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
