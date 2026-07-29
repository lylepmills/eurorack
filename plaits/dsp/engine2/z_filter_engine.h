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
// covers both ends of both Braids axes on all four shapes. 28 of its 30 cases
// are within tolerance: below the cutoff clamp, AC RMS within 0.26 dB (0.33 dB
// on `lp-cutoff-sweep`, whose sweep reaches the clamp) and octave bands within
// 0.82 dB, pitch within +0.8 cents after the kCorrectedSampleRate correction.
// (An earlier version of this comment claimed 0.05 dB / 0.26 dB "third-octave"
// agreement for all four models; that figure came from a harness that no
// longer exists, is not what the committed A/B measures, and the band metric
// is per OCTAVE. It also said 26 of 28 -- the file has 30 cases.)
//
// Declared deviations from Braids:
//   - lut_sine is read at 512 points/period against Braids' 257-entry wav_sine
//     at an 8-bit index (256 points/period). Cleaner interpolation, not
//     identical. This is only the RESOLUTION difference between the two tables
//     -- their gain and offset differ too, which is not a deviation but the
//     defect recorded below.
//   - at the top of TIMBRE every model reads 1.5 to 1.7 dB quieter than the
//     reference. That is this file's 3-tap decimator, not the algorithm: its
//     response is cos^2(pi*f/96000), which is -1.70 dB at the 13.28 kHz the
//     pitch clamp puts the modulator at, and BP and HP -- which have no
//     integrator -- read -1.67 dB there with their spectra flat to 0.38 dB.
//     The same decimator also ADDS energy: its stopband is not the harness's
//     127-tap sinc, so images land 1.4 to 1.7 dB high across 320 Hz - 5 kHz in
//     that corner (~5% of the energy). Decimating BOTH sides with this 3-tap
//     filter collapses the whole band table to within 0.05 dB, which is what
//     shows the excess is the decimator and nothing in the loop.
//
// KNOWN DEFECT, not a deviation -- do not read the numbers above as covering
// it. THIS FILE'S SINE IS NOT BRAIDS' SINE. Braids reads wav_sine, which is
// exactly 32639 * -cos(2*pi*x) + 127 (fitted to +-1.7 LSB over all 257
// entries): a -0.034 dB gain and, decisively, a +127 LSB (+0.00388 full-scale)
// DC OFFSET. BraidsSine() below reproduces the PHASE of that table and none of
// its offset, because plaits' lut_sine is a zero-mean unit sine. square_carrier
// carries that offset into `pulse`, `pulse` is integrated at a gain of
// 4 * mod_increment per sample, and the polarity latch flips its sign every
// half carrier cycle -- so on hardware the offset alone ramps the integrator
// into BOTH rails once per half cycle whenever the cutoff is high and the note
// is low. This port's integrator never gets there. On LP and PK, with the
// balance near noon and the cutoff high, that is 5.70 dB of AC RMS and 3.49 dB
// across octave bands (ab.json `lp-integrator-corner` and
// `pk-integrator-corner`, which fail on purpose). It is the integrator and not
// the decimator: `bp-integrator-control` is the same note, cutoff and MORPH on
// a model with no integrator and passes at -1.67 dB / 0.38 dB.
//
// (The comment above BraidsSine in the .cc quotes wav_sine[0] = -32512 and
// [64] = 126 and then calls the table "-cos(2*pi*x)". Those two numbers ARE
// the gain and the offset -- the table is not -cos(2*pi*x). That comment is
// about the PHASE ORIGIN, and on the phase origin it is right: the port's
// Sine(phase + 0.75) is exactly -cos(2*pi*phase).)
//
// The DC the defect leaves behind runs the opposite way to what an earlier
// version of this comment claimed ("-0.05 against -0.25"). Measured on the
// committed A/B's own renders of `lp-integrator-corner`, with out_gain
// divided back out: Braids -0.0035, this port -0.049. Braids has LESS residual
// DC, because it saturates SYMMETRICALLY against both rails; the port, which
// never reaches either, keeps the offset-free integrator's own average.
//
// Measured in a replica of both inner loops that reproduces the committed
// reference renderer's AC RMS to six decimals at three settings (LP MIDI 24
// and 45, PK MIDI 24). Against that reference, at MIDI 24 / top of TIMBRE /
// MORPH at noon, over 288000 sub-samples (3 s at 96 kHz):
//   - this file as shipped:            LP -5.02 dB / 3.77 dB bands, 2 clips
//   - + wav_sine's DC offset only:     LP +0.01 dB / 0.02 dB bands, 8253 clips
//   - Braids itself:                                               8213 clips
// PK behaves the same way (-3.53 dB / 5.15 dB -> +0.02 dB / 0.02 dB). The
// earlier reading of this defect -- that it is Braids' arithmetic-shift FLOOR
// at digital_oscillator.cc:384 and the CLIP that rectifies its drift -- does
// not survive the same test: restoring that floor alone makes LP WORSE
// (-5.20 dB) and reaches the rail 98 times, not 8213. The floor is real and
// biases the accumulator by -0.5 LSB/sample, but it is a hundredth of the
// effect. BP and HP are unaffected at every setting tested, because neither
// reads the integrator; the offset costs them nothing measurable (+0.03 dB).
// A fix is a DSP change and therefore a digest move and a builder rollout, so
// it is not this pass's to make.
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
