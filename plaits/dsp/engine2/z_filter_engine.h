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
// Measured against Braids by a committed, rerunnable A/B --
// plaits_lab_sdk/packages/mutable-instruments/z-filter/tests/ab.json, run with
// `python3 ab_engine.py packages/mutable-instruments/z-filter --bands`. It
// covers both ends of both Braids axes on all four shapes. ALL 30 CASES are
// within tolerance. Below the cutoff clamp, AC RMS is within 0.11 dB and the
// octave bands within 0.34 dB, pitch within +0.8 cents after the
// kCorrectedSampleRate correction. At the clamp the figures are the decimator
// and are itemised below.
// (An earlier version of this comment claimed 0.05 dB / 0.26 dB "third-octave"
// agreement for all four models; that figure came from a harness that no
// longer exists, is not what the committed A/B measures, and the band metric
// is per OCTAVE. It also said 26 of 28 -- the file has 30 cases.)
//
// THE PULSE INTEGRATOR RUNS IN FIXED POINT, and has to. Braids' wav_sine is
// not a sine: fitted programmatically against all 257 entries of
// braids/resources.cc it is exactly 32639 * -cos(2*pi*x) + 127 (max residual
// 1.7 LSB) -- a -0.034 dB gain and a +127 LSB (+0.00388 full-scale) DC OFFSET
// on every sample. square_carrier carries that offset into `pulse`, `pulse` is
// integrated at a gain of 4 * mod_increment per sample, and the polarity latch
// flips its sign every half carrier cycle, so the offset ALONE ramps the
// accumulator into both rails once per half cycle whenever the cutoff is high
// and the note is low. Reading a zero-mean lut_sine there, and accumulating in
// float, is what shipped in wave 1: the integrator never reached a rail, and
// LP and PK measured 5.70 dB and 4.82 dB down in AC RMS with 3.49 dB and
// 4.44 dB of band spread (ab.json `lp-integrator-corner` /
// `pk-integrator-corner`, which used to fail on purpose and now pass at
// -0.20 dB / 0.74 dB and -0.52 dB / 0.96 dB).
//
// So the whole square path is now Braids' arithmetic rather than its idealised
// value: BraidsSineFixed() reproduces wav_sine's gain and its +127 offset, and
// carries the same ~-0.5 LSB downward bias as Interpolate824's arithmetic
// shift -- it does not reproduce that shift operation-for-operation, since
// Interpolate824 floors only the interpolation delta it adds to an exact table
// entry while this floors the whole ideal value, but the two differ by under
// 2 LSB and the loop measures identical (see the 96 kHz check below);
// double_saw is quantised to the
// uint16 Braids builds with `~(phase_ >> 15)`; integrator_gain is the uint16
// `modulator_phase_increment >> 14`; both `>> 16`s are arithmetic shifts that
// floor; and the accumulator is an int32 rectified by stmlib's CLIP against
// both rails. The windowed-resonator carrier carries wav_sine's gain and
// offset too -- worth nothing on its own (-48 dB) but it is what Braids reads.
//
// Verified by replaying the fixed loop at 96 kHz, so the decimator below is
// out of the comparison, against native 96 kHz reference renders: AC RMS
// +0.000 dB and octave bands 0.00 dB at `lp-cutoff-max`, `pk-cutoff-max`,
// `lp-integrator-corner` and `pk-integrator-corner`. The residual the 48 kHz
// A/B still prints is therefore the decimator and nothing in the loop. The
// integer replica used for that check reproduces the reference renderer
// sample-for-sample (0 mismatches in 288000 samples).
//
// Two earlier readings of this defect are recorded here because both are
// wrong and both are plausible. It is NOT the arithmetic-shift floor at
// digital_oscillator.cc:384 and the CLIP that rectifies its drift: restoring
// that floor alone, in the bit-exact replica, leaves LP at -5.25 dB and
// reaches the rail 98 times against Braids' 8213. And it is NOT the
// decimator: `bp-integrator-control` is the same note, cutoff and MORPH on a
// model with no integrator and reads -1.71 dB with its bands flat to 0.37 dB,
// which is the decimator figure and nothing else.
//
// Declared deviations from Braids:
//   - lut_sine is read at 512 points/period against Braids' 257-entry wav_sine
//     at an 8-bit index (256 points/period). Cleaner interpolation, not
//     identical. Resolution only -- the table's GAIN and OFFSET are now
//     reproduced (see BraidsSineFixed in the .cc).
//   - at the top of TIMBRE every model reads quieter than the reference --
//     0.6 dB (lp-cutoff-max) to 1.7 dB (hp-high-note), with the four models'
//     own ceiling cases at -0.56, -1.15, -1.64 and -1.55 dB for LP, PK, BP and
//     HP. That is this file's 3-tap decimator, not the algorithm: its
//     response is cos^2(pi*f/96000), which is -1.70 dB at the 13.28 kHz the
//     pitch clamp puts the modulator at, and BP and HP -- which have no
//     integrator -- read -1.64 and -1.55 dB there with their spectra flat to
//     0.36 dB. The same decimator also ADDS energy: its stopband is not the
//     harness's 127-tap sinc, so images land high across 320 Hz - 5 kHz in
//     that corner, which is the 0.70 to 0.96 dB of band spread the four
//     clamped cases print. Running the loop at 96 kHz against a native 96 kHz
//     reference collapses all of it to 0.00 dB.
//   - the phase accumulators and the windowing arithmetic stay in float; only
//     the pulse/integrator path is fixed point, because that is the only place
//     the quantisation sets an equilibrium rather than adding a rounding
//     error.
//
// Not a deviation, contrary to an earlier version of this comment: Braids'
// `window * (carrier + 32768)` does overflow int32 (on ~18% of samples), but
// the two's-complement wrap is exactly undone by the int16_t assignment to
// saw_tri_signal, so the clean float form below is bit-equivalent to it --
// 0 mismatches in 192000 samples at each of three settings.
//   - the burst envelopes are raised to a MACRO-controlled power; at the
//     MACRO detent the exponent is exactly 1 and the window is Braids' ramp.
//   - AUX reads the complementary model off the SELECTED model's resonator
//     phases rather than seeding its own from kPhaseReset. The four models
//     differ in two ways -- their output combination and their reset phases --
//     and only the combination is free once the phases have been advanced.
//     Running a second set of phases measured at 94% of the CPU budget under
//     qemu/estimate.py; sharing them costs ~5 operations and lands near half
//     that. OUT, the model the user selected, is unaffected: it is Braids'.
//
// WHAT AN EXISTING PATCH WILL SOUND LIKE AFTER THIS FIX. This is an audible
// change wherever the integrator is in the mix -- on OUT that is LP and PK, on
// AUX it is the BP and HP selections, whose complements ARE LP and PK (see the
// last bullet; that is where the biggest change of all lands). Not a rounding
// change. Measured OLD LOOP against NEW LOOP
// directly, both at 96 kHz so the decimator is out of it, TIMBRE at the top
// and MORPH at the balance peak:
//
//        note 24   note 36   note 48   note 60
//   LP   +5.03 dB  +2.89 dB  +1.28 dB  +0.56 dB
//   PK   +3.48 dB  +1.23 dB  +0.65 dB  +0.21 dB
//
//   - LP and PK gain BODY. Their square_signal is the integrator, and the
//     integrator now saturates against both rails instead of hovering near
//     zero, so the low end goes from thin to the squared-off, hard-limited
//     buzz the module makes. A bass patch mixed to sit under something else
//     will now be too loud and want its level pulled back by a few dB.
//   - The character changes with it, not just the level: a rectified
//     accumulator is a square-ish waveform carrying the burst's rhythm, where
//     the float one was a small smooth ramp. The A/B's band table at that
//     corner moved by 3.49 dB (LP) and 4.44 dB (PK) across the whole
//     spectrum, not in one band.
//   - It is continuous, not a corner case -- see the table -- and it fades
//     with MORPH: at note 24 the change is +0.69 dB at MORPH 0.1, +1.05 dB at
//     0.9, and -0.01 dB at either end, where balance excludes the integrator
//     from the mix entirely.
//   - It does NOT go away at the bottom of TIMBRE, and the reason is not
//     "the effect scales with the integrator gain". A low cutoff does shrink
//     the gain, but it also puts the modulator BELOW the carrier, so the DC
//     still walks the accumulator into a rail long before the polarity latch
//     flips: in the bit-exact replica Braids clips 5832 to 7867 times per
//     96000 sub-samples at TIMBRE 0, where the wave-1 loop clipped 0 times.
//     What is small there is the LEVEL change, not the change. Through the
//     harness at TIMBRE 0 / MORPH 0.5 the LP level moves +0.18 / +0.30 /
//     +0.51 dB at notes 24 / 48 / 72 and PK under +0.1 dB, while LP's
//     octave-band agreement with Braids goes from 0.83 dB to 0.02 dB at note
//     24 and 1.14 dB to 0.07 dB at note 48. A cutoff-down LP patch therefore
//     changes TIMBRE at roughly constant level, rather than not changing.
//   - BP and HP are unchanged in character ON OUT -- neither combination reads
//     the integrator -- but their AUX is not. AUX runs the complementary
//     model, so a BP selection's AUX is PK and an HP selection's AUX is LP,
//     and both read the same integrator off the selected model's phases.
//     Measured old loop against new loop at 96 kHz, TIMBRE at the top, MORPH
//     at the balance peak:
//
//               note 24   note 36   note 48   note 60
//       AUX of HP  +7.41 dB  +3.51 dB  +2.62 dB  +1.56 dB
//       AUX of BP  +2.61 dB  +1.36 dB  +0.63 dB  +0.23 dB
//
//     The HP row is the LARGEST change this fix makes anywhere -- larger than
//     the +5.03 dB on LP's own OUT -- so a patch that takes BP or HP from OUT
//     and its complement from AUX is affected as much as an LP or PK patch,
//     and a patch using AUX alone on HP is affected more.
//     Every model, on both outputs, is now 0.034 dB quieter, because the
//     resonator carries wav_sine's true 32639 amplitude rather than a unit
//     one. That is inaudible on its own and it is what the module does.

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
  // Braids' `int32_t square_integrator`, kept in its own int16-count units.
  // See the .cc: its equilibrium is a quantisation effect, not a signal one.
  int32_t integrator_;
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
