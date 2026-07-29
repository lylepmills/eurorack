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
// Two controls Braids does not have: MORPH fades the two modulators in from a
// bare carrier, and MACRO drives the shaper. At the MACRO detent the drive is
// 1.0 and the shaper is Braids'.
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
// costs 271 instructions/sample against the stereo path's 358, and carries two
// divides per sample rather than four.
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
//   - Braids reseeds the carrier a quarter cycle in on sync. That is kept,
//     but the attack transient still will not A/B against hardware, because
//     the three phases' relative alignment at note start is what shapes it.

#ifndef PLAITS_DSP_ENGINE2_RING_MOD_ENGINE_H_
#define PLAITS_DSP_ENGINE2_RING_MOD_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' `(parameter_ - 16384) >> 2` in 1/128-semitone units: +-32 semitones
// of detune on each modulator.
const float kRingModDetuneRange = 32.0f;

// Braids clamps every increment through ComputePhaseIncrement, which tops out
// at MIDI 128 (13.29 kHz) -- tighter than kMaxFrequency, so it is the ceiling
// the port uses too.
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
