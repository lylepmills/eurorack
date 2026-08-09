// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' RING: a carrier ring-modulated by two independently detuned sines,
// through a saturating shaper.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderTripleRingMod.
// It carries no `size -= 2`, so it is a native 96 kHz algorithm; the port runs
// the same loop at 2x the 48 kHz output rate and decimates.
//
// Braids' TIMBRE and COLOR are the two detunes, in that order: parameter_[0]
// sets modulator 1's increment and parameter_[1] modulator 2's
// (digital_oscillator.cc:136-141), so HARMONICS carries the first and TIMBRE
// the second. Two more controls Braids does not have: MORPH fades the two
// modulators in from a bare carrier, and MACRO drives the shaper.
//
// The module's own sound is at MACRO 0.5 AND MORPH 1.0, not at noon on both.
// MACRO has a detent and ApplyMacro returns drive 1.0 there. MORPH does not
// (SPEC 2) -- it is a 0-to-full depth fade, so half depth is what noon gives
// and Braids' `carrier * sine * sine` is only at the top. Every A/B case
// therefore holds MORPH at 1.0; at 0.5 the same case measures 5.21 dB of
// spectral difference against the module.
//
// OUT: the full three-way ring product.
//
// AUX (mono): modulator 1 on its own -- a clean sine at note + detune 1, at
// full level whatever MORPH is doing. The modulator is the one thing in the
// engine you cannot otherwise hear, which is the same reason two-op-fm puts
// its sub-oscillator on AUX; and it is the only signal here that is not simply
// OUT with a knob moved (turn MORPH down and the old AUX walks into OUT).
// Because its increment is clamped to 13.29 kHz it is under Nyquist at the
// output rate, so it skips the shaper AND the halfband entirely: the mono path
// costs 309.6 instructions/sample against the stereo path's 357.6 -- 60% of
// budget against 69% -- and carries two divides per sample rather than four.
//
// In stereo OUT/AUX become L/R and AUX reverts to carrier times modulator 1,
// at matched gain and the same scale -- a bare sine would be a poor right
// channel against a saturated ring product.
//
// Declared deviations from Braids:
//   - the shaper is `SoftLimit(2x) / SoftLimit(2)` rather than a lookup into
//     Braids' 257-entry ws_moderate_overdrive. The table is tanh(2x)/tanh(2)
//     to within a thousandth, SoftLimit is the Pade form of tanh, and the
//     substitution measures within 0.0061 over the range Braids actually
//     drives. Saves 514 B and a table.
//   - the decimator is a 15-tap halfband (Kaiser beta 3.25) rather than
//     nothing: Braids at 96 kHz sends its 24-48 kHz content out through the
//     DAC, while the port has to fold it. Measured, per R7: -55.8 dB at
//     36 kHz (folding to 12 kHz), -57.1 dB at 43.2 kHz (to 4.8 kHz),
//     -76.8 dB at 46 kHz (to 2 kHz), and -44.4 dB worst case anywhere in
//     32-48 kHz, which is the band that folds into 0-16 kHz. Passband is
//     flat to -0.4 dB at 18 kHz. The spec's 7-tap alternative manages only
//     -24.7 dB at the 36 kHz fold, so this is 31 dB better where it counts.
//   - Plaits' lut_sine is a unit-amplitude sine; Braids' wav_sine is not.
//     Fitting the 257-entry table gives `127.0 - 32639.0 * cos(2*pi*phase)`
//     with a worst residual of 1.7 LSB -- amplitude 0.996063 of full scale
//     and a DC offset of +0.003876, both artefacts of the generator's
//     mean-centring over an array whose last point is a duplicate
//     (braids/resources/waveforms.py:87-93, 96-98, 117). Three of those
//     multiply together here, so the port's ring product arrives at the
//     shaper 0.103 dB hot, and Braids' DC term additionally seeds two-way
//     ring products at about -48 dB that the port does not have. Measured
//     against a bit-exact emulation of RenderTripleRingMod: substituting an
//     ideal sine is +0.10 dB, substituting the Pade shaper is -0.17 dB, and
//     the two very nearly cancel, for a net -0.07 dB. Kept rather than
//     corrected -- reintroducing the deviation would mean a private sine
//     table, and the A/B measures 0.08 dB of spectral difference with it.
//   - the trigger. Braids' sync input zeroes the LOCAL carrier phase and both
//     modulator phases so all three start together (digital_oscillator.cc:
//     145-149); the module's own strike/note-on does NOT reach this shape at
//     all. The port maps Plaits' TRIGGER_RISING_EDGE onto that sync, so a
//     played note resets the phases where the module would need sync patched.
//     That makes the attack deterministic but it is not the module's
//     behaviour, and it is why every A/B case uses triggerHz 0.
//
// A/B against the module (tests/ab.json, 11 cases, both ends of both Braids
// axes): AC RMS within 0.05 dB and spectrum within 0.25 dB everywhere the
// output stays under Nyquist, pitch within 0.2 cents on the four cases whose
// f0 the harness can track. The two outliers are both decimator cases and are
// explained there: 0.62 dB with the sum product folded, and 1.24 dB at note
// 108 where 31% of the reference's energy sits above 20.5 kHz.

#ifndef PLAITS_DSP_ENGINE2_RING_MOD_ENGINE_H_
#define PLAITS_DSP_ENGINE2_RING_MOD_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' `(parameter_ - 16384) >> 2` in 1/128-semitone units: +-32 semitones
// of detune on each modulator.
const float kRingModDetuneRange = 32.0f;

// Braids clamps every increment through ComputePhaseIncrement, which tops out
// one 1/128-semitone step below MIDI 128 -- `kPitchTableStart - 1`, i.e. MIDI
// 127.9921875, 13.28 kHz (digital_oscillator.cc:52-54). That is tighter than
// kMaxFrequency, so it is the ceiling the port uses too, rounded to 128.0: a
// port modulator that reaches the clamp therefore runs 0.78 cents above the
// module's. The `clamp` A/B case is the one that reaches it.
const float kRingModMaxPitch = 128.0f;

// The three int16 multiplies each shift down by 16, so the product reaching
// the shaper is a quarter of full scale.
const float kRingModPreShaperScale = 0.25f;

// 1 / SoftLimit(2), so the shaper reaches unity at unity input.
const float kRingModShaperNorm = 1.016129f;

// The mono AUX sine is a full-scale waveform where OUT is a ring product that
// has been through the shaper, so it arrives hot. Measured over the parameter
// grid this lands AUX within a fraction of a dB of OUT, keeping aux_gain equal
// to out_gain.
const float kRingModAuxGain = 0.51f;

// 15-tap halfband, Kaiser beta 3.25, normalized to unity DC gain. Only the
// centre and the odd offsets are non-zero.
const float kRingModHalfbandCentre = 0.500547367f;
const float kRingModHalfband1 = 0.310005574f;
const float kRingModHalfband3 = -0.082226011f;
const float kRingModHalfband5 = 0.029547365f;
const float kRingModHalfband7 = -0.007600612f;

class RingModEngine : public Engine {
 public:
  RingModEngine() { }
  ~RingModEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool linear_tzfm_capable() const { return true; }
  // Pattern B: mono AUX is the bare modulator; the stereo branch replaces it
  // with the two-way ring product so L/R stay a matched pair.
  virtual bool stereo_capable() const { return PLAITS_STEREO_RING_MOD; }

 private:
  inline float Decimate(const float* history) const;

  float phase_;
  float modulator_phase_;
  float modulator_phase_2_;

  // Power-of-two ring buffers of the last 16 internal sub-samples, so the
  // halfband reads by mask rather than shifting fifteen floats twice per
  // output sample.
  float history_[16];
  float history_aux_[16];
  int history_position_;

  DISALLOW_COPY_AND_ASSIGN(RingModEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_RING_MOD_ENGINE_H_
