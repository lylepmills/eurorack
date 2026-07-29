// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' VOSM model: two sine formants summed on a pedestal and multiplied by
// a bell window that restarts on every cycle of the played note, with both
// formant phases hard-reset at that restart. Classic VOSIM.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderVosim
// (braids/digital_oscillator.cc:410-438).
//
// LINEAGE, and the drop that was reversed. BRAIDS_PORT_SPEC.md §7.1 dropped
// VOSM in wave 1 because granular-formant already ships the topology: two
// GrainletOscillators share an f0-locked window and carry one sine formant
// each, so the only unreachable content looked like an absolute rather than
// ratio second formant plus the 2:1 amplitude balance. §7.1 named a
// reinstatement condition -- an A/B against granular-formant with TIMBRE at the
// same absolute formant pitch, HARMONICS at the f2/f1 ratio, MORPH in the
// shape-branch-1 region near breakpoint 0.0625, and MACRO at full carrier
// bleed. That was run, and it does not all go one way.
//
// Solving those four settings exactly (grain_engine.cc:66-78 and
// grainlet_oscillator.h Carrier(); breakpoint 0.0625 needs carrier_shape
// 0.500783, which at note 40 is MORPH 0.5081) and rendering against Braids
// VOSM at TIMBRE 0.60 / COLOR 0.72 -- formants at 690 and 1676 Hz, 15.4
// semitones apart, well inside granular-formant's +/-24-semitone ratio --
// granular-formant lands 1.88 dB away, where this port is at 0.06 dB. But that
// prescribed point is not granular-formant's best: a coordinate-descent search
// over all four of its macros, level-matched, gets to 0.63 dB. Under a metric
// where the port reads 0.06, 0.63 dB is close. For the pairs granular-formant
// can set, §7.1's "re-macro of granular-formant" reading is basically right.
//
// Where it stops being right is the ratio limit. granular-formant's HARMONICS
// spans -24 to +24 semitones -- a hard 4:1 cap on f2/f1 either way -- and its
// TIMBRE spans MIDI 24 to 108, while VOSM's two knobs are independent MIDI
// 0-128 pitches and so reach any ratio up to about 1600:1. Integrating both
// constraints over the knob plane numerically, granular-formant can set only
// 24.6 % of the formant pairs VOSM can. Best-fit searches at two pairs outside
// that window, same method:
//
//   Braids setting (note 40)           formants        port      grain best
//   TIMBRE 0.60 COLOR 0.72   +15.4 st  690 / 1676 Hz   0.061 dB    0.629 dB
//   TIMBRE 0.45 COLOR 0.85   +51.2 st  228 / 4381 Hz   0.039 dB    1.701 dB
//   TIMBRE 0.85 COLOR 0.45   -51.2 st  4381 / 228 Hz   0.036 dB    1.528 dB
//   TIMBRE 0.10 COLOR 0.30   +25.6 st  16.6 / 97 Hz    0.137 dB    0.144 dB
//
// So the honest verdict is a split one. Inside the ratio window granular-formant
// gets within about half a dB of this model and the engine earns its slot on
// the balance knob and the exactness, not on reach. Outside it -- three
// quarters of the knob plane -- it cannot get closer than 1.5-1.7 dB, and no
// setting of its four macros will, because the pair is not addressable. The last
// row is the counter-example worth keeping: a pair that is out of range on
// PAPER (both the ratio and TIMBRE's floor) but where both formants are so far
// below the note that the grain is almost the bare window, and granular-formant
// matches it as well as this port does. Parameter-space unreachability is not
// the same claim as audible unreachability, and only the second one counts.
//
// NOTE ON THE BRAIDS CONTROLS. Both knobs are formant PITCHES, and both are
// ABSOLUTE -- neither tracks the played note.
//
//   digital_oscillator.cc:415   formant_increment[i] = ComputePhaseIncrement(
//                                   parameter_[i] >> 1)
//
// `i` runs 0 then 1, so parameter_[0] drives formant 1 and parameter_[1]
// drives formant 2. braids.cc:217-226 fills parameter_[0] from SETTING_AD_TIMBRE
// and parameter_[1] from SETTING_AD_COLOR, so:
//
//   parameter_[0] = TIMBRE = formant 1 pitch
//   parameter_[1] = COLOR  = formant 2 pitch
//
// `>> 1` turns a 15-bit knob into a Braids pitch in 1/128-semitone units, so
// each knob sweeps MIDI 0 to 127.99 (:415, with ComputePhaseIncrement clamping
// at kPitchTableStart = 128*128, :52-54).
//
// The rest of the loop, with line numbers, because the amplitudes and the
// pedestal are load-bearing and none of them is a knob:
//
//   :422   sample = 16384 + 8192            the pedestal, 0.75 of full scale
//   :424   += Interpolate824(wav_sine, formant_phase[0]) >> 1     formant 1, 0.50
//   :427   += Interpolate824(wav_sine, formant_phase[1]) >> 2     formant 2, 0.25
//   :429   sample = sample * (Interpolate824(lut_bell, phase_) >> 1) >> 15
//   :430   if (phase_ < phase_increment_) { both formant phases = 0; sample = 0; }
//   :435   sample -= 16384 + 8192
//
// The pedestal is the sum of the two formant amplitudes, which is what keeps the
// windowed pulse unipolar: the grain bottoms out just above zero rather than
// crossing it, so the window can open and close without a step. (Just above,
// not exactly at -- wav_sine's amplitude is 0.4 % short of full scale, leaving a
// floor of 0.0059. See kVosimSineAmplitude.) Any port that changes the balance
// has to hold a1 + a2 fixed or it introduces a step. This engine does.
//
//   TIMBRE     Formant 1   Braids' TIMBRE: absolute pitch, MIDI 0 to 128.
//   HARMONICS  Formant 2   Braids' COLOR: absolute pitch, MIDI 0 to 128.
//   MORPH      Window      the bell's attack fraction. Braids hardwires 1/16
//                          of the cycle; noon is exactly that, and the two
//                          halves run to 1/256 and to a symmetric half-and-half.
//   MACRO      Balance     the formant amplitude ratio. Braids hardwires 2:1;
//                          noon is exactly that, and the ends are either
//                          formant alone. The pedestal tracks the sum, so the
//                          grain stays unipolar at every setting.
//
// RATE. RenderVosim does NOT end in `size -= 2` (the loop at :417-437 writes
// one sample per iteration), so it is a 96 kHz algorithm and its rate constants
// do not transfer to 48 kHz. Two of them matter:
//
//   - `if (phase_ < phase_increment_)` (:430) is a window ONE SAMPLE wide at
//     96 kHz. Run at 48 kHz it would be twice as long a slice of the cycle, and
//     the formant phases would be held at zero for twice as long every period.
//   - the formant phase resolution at the restart, which is what puts the
//     grain's opening edge where Braids puts it.
//
// So the port runs its inner loop 2x oversampled -- 96 kHz, Braids' own rate --
// and decimates. Every constant above then transfers verbatim, which is the
// check SPEC R5 exists to force rather than a licence to transplant.
//
// OUT: the grain train at the set balance. AUX (mono): the same grain train
// with the balance mirrored about noon, so it is always favouring the formant
// OUT is not. In stereo OUT/AUX become L/R and the two sides take a small
// opposing balance offset instead. a1 + a2 is fixed, so both channels carry the
// SAME pedestal and therefore the same window pulse in phase -- the mono sum
// stays at the played note rather than jumping an octave, which is what a
// half-period AUX offset would have done.
//
// Declared deviations from Braids:
//   - the decimator is a 15-tap halfband, where Braids' 96 kHz stream is simply
//     the codec rate. Measured: +0.02 dB at 12 kHz, -1.42 dB at 20 kHz,
//     -6.01 dB at 24 kHz, and never above -19.62 dB anywhere from 28.8 kHz to
//     48 kHz, so what Braids puts in the 24-29 kHz transition folds back into
//     the top octave here instead of being removed. Across the eight A/B cases
//     the two bands above 5.12 kHz run +0.33 to -2.50 dB against the reference's
//     127-tap decimation, on at most 1.3 % of the energy.
//   - lut_bell is embedded verbatim and read with Braids' interpolation, but in
//     float: the `>> 1` before the multiply and the int16 store of the result
//     are not reproduced, so the port does not carry Braids' per-sample output
//     quantisation.
//   - the two sines come from Plaits' 512-entry lut_sine rather than Braids'
//     257-entry wav_sine. wav_sine's phase (it is -cos, not sin), its +127
//     offset and its 0.4 %-short amplitude are all reproduced; its own
//     interpolation ripple is not -- 2.46 LSB peak of chord error against the
//     continuous form, 3.04 LSB once the table's per-entry rounding is counted
//     with it.
//   - formant pitch is resolved through NoteToFrequency rather than Braids'
//     interpolated lut_oscillator_increments. The knob's 1/128-semitone
//     quantisation IS reproduced; the table's own interpolation error, worst
//     case 0.012 cents over MIDI 0-128, is not.
//   - a one-pole DC blocker, SEEDED at the model's own offset by integrating
//     one grain on the first block after a Reset (see kVosimSeedSteps). Braids
//     emits a large negative offset -- between -0.35 and -0.62 FS depending on
//     where the two formants sit against the note -- and leaves it there;
//     Plaits' output is DC-coupled and every neighbouring engine blocks it.
//     R1 then applies: removing the offset lifts the peak to a measured 1.169
//     (worst over a 5x5x3x3 macro grid at note 40), so the engine registers
//     negative gains and takes the limiter path.
//
// What is left, measured. Every band carrying 2 % or more of the energy is
// within 0.20 dB in all eight A/B cases. The residual is in two places,
// neither of them load-bearing:
//
//   - the three bands above 5.12 kHz, which is the decimator above: +0.33 to
//     -2.50 dB, on at most 1.3 % of the energy;
//   - on `high-note` only, the four bands BELOW the played note (20 Hz to
//     320 Hz, -6.7 to +2.6 dB) -- together 0.2 % of the energy, and neither
//     side has anything there but its own noise floor. At the seven other
//     cases those same bands are within 0.5 dB.
//
// The near-DC skirt used to be the third place, at +11.6 dB on `formant1-low`.
// That was this port's own DC-blocker startup ramp, not a difference in the
// model, and it is what the integrated seed above fixed: that band now reads
// +0.17 dB, and the engine's first 100 ms carries the same mean as its
// hundredth (-0.002 against -0.0002, where it used to be -0.041).

#ifndef PLAITS_DSP_ENGINE2_VOSIM_ENGINE_H_
#define PLAITS_DSP_ENGINE2_VOSIM_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// The pedestal, 16384 + 8192 out of 32768, and the share of it that Braids
// gives formant 1: 16384 / 24576. Both from digital_oscillator.cc:422-427.
const float kVosimPedestal = 24576.0f / 32768.0f;
const float kVosimStockShare = 16384.0f / 24576.0f;

// Braids' wav_sine is NOT a unit sine, and the difference is not only the
// quarter-cycle phase. Fitted over all 257 entries of braids/resources.cc, the
// table is `127 - 32639 * cos(2 * pi * i / 256)` to within 1.68 LSB peak and
// 0.668 LSB RMS -- comparable to the 2.4 LSB that Braids' own 257-point linear
// interpolation costs against that same continuous form, so this is the table
// rather than an idealisation of it. It carries a +127 offset and its amplitude
// is 0.4 % short of full scale.
//
// Reading the table's own numbers rather than a unit sine is worth about a
// third of the port's remaining spectral error. Measured across the eight A/B
// cases with everything else held fixed, a unit sine reads
// 0.06 / 0.09 / 0.12 / 0.11 / 0.07 / 0.09 / 0.06 / 0.05 dB and the table's
// offset and amplitude read 0.04 / 0.06 / 0.08 / 0.09 / 0.04 / 0.07 / 0.04 /
// 0.04 dB. Small, but it is the whole margin at this level of agreement, and
// it is free: both fold into the pedestal and the two gains once per block.
//
// It also changes the shape of the grain floor rather than only its level. The
// pedestal is what makes the grain unipolar; with the table's numbers the floor
// is 0.75 * (1 + 127/32768 - 32639/32768) = 0.0059 rather than exactly zero.
// (Wave A found this same class of defect in particle-burst, from the other
// direction: a float closed form used where Braids reads a truncated integer.)
const float kVosimSineOffset = 127.0f / 32768.0f;
const float kVosimSineAmplitude = 32639.0f / 32768.0f;

// The bell's peak sits at entry 16 of 256 -- lut_bell is
// `hanning(32)[:16] + hanning(480)[240:] + [0]`, one sixteenth attack against
// fifteen sixteenths decay (braids/resources/lookup_tables.py:239-247).
const float kVosimStockBreakpoint = 16.0f / 256.0f;
const float kVosimMinBreakpoint = 1.0f / 256.0f;
const float kVosimMaxBreakpoint = 0.5f;

// The DC blocker is SEEDED at the model's own offset rather than started from
// zero, and the seed is INTEGRATED over one grain on the first block after a
// Reset rather than taken from a constant.
//
// It is not cosmetic. A one-pole blocker at 0.001 has a 20.8 ms time constant,
// so it takes about 100 ms to walk to its target, and that walk is a large
// low-frequency event at the engine's first block.
//
// The constant that used to sit here -- pedestal * (mean(lut_bell) *
// (1 + sine offset) - 1) = -0.3750 -- is only the PEDESTAL's share of that
// offset. It is not the model's DC, because both formants restart with the
// carrier: every grain multiplies the window by the SAME stretch of each sine,
// so the sines do not average out against the window and their contribution is
// part of the offset. It is largest exactly where a constant seed is worst --
// a formant slower than the played note, where -cos barely moves off -1 across
// the whole grain. Measured against the module (note 45, MORPH noon):
//
//   TIMBRE 0.5 COLOR 0.5   true DC -0.3542    constant seed -0.3750
//   TIMBRE 0.5 COLOR 0.0   true DC -0.4832    constant seed -0.3750
//   TIMBRE 0.0 COLOR 0.5   true DC -0.6123    constant seed -0.3750
//
// so on the last of those the constant seed left the blocker 0.237 FS from its
// target and the engine opened with a 100 ms DC ramp -- +11.6 dB in the
// 20-40 Hz band of that A/B case, which drops to -0.4 dB once the first 250 ms
// is discarded. Integrating the grain instead lands within 0.0015 FS of the
// measured offset on every case above, and the ramp is gone.
//
// The integral is a plain Riemann sum over one carrier cycle, evaluated once,
// on the first Render after a Reset (engine selection) -- so it costs nothing
// per sample and nothing per note. It pairs each step's window value with the
// formant phases the engine itself would hold at that point in the grain, so it
// follows MORPH's warp and MACRO's balance exactly.
const int kVosimSeedSteps = 1024;

// A formant this many times the note's frequency contributes less than 1e-5 to
// the integral above (the bell's transform falls off as 1/ratio^2), while a
// 1024-step grid would alias it. Past this ratio the term is dropped, which is
// both cheaper and more accurate than sampling it.
const float kVosimSeedMaxRatio = 256.0f;

// The two channels take opposing balance offsets in stereo. Small, because a
// wide split would make the two sides different formant voices rather than a
// stereo image of one.
const float kVosimStereoBalance = 0.12f;

// 15-tap halfband, Kaiser beta 3.25, normalized to unity DC gain; only the
// centre and the odd offsets are non-zero. Re-derived here rather than shared
// (SPEC R9); the coefficients agree with ring-mod's to nine decimals because
// both are the same filter, not because either was copied from the other.
const float kVosimHalfbandCentre = 0.500547367f;
const float kVosimHalfband1 = 0.310005574f;
const float kVosimHalfband3 = -0.082226011f;
const float kVosimHalfband5 = 0.029547365f;
const float kVosimHalfband7 = -0.007600612f;

class VosimEngine : public Engine {
 public:
  VosimEngine() { }
  ~VosimEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_VOSIM; }

 private:
  inline float Decimate(const float* history) const;

  // Braids' carrier phase, raw: it indexes lut_bell and it is what the
  // once-per-cycle reset is tested against.
  float phase_;

  // The two formant phases, STORED with the quarter-cycle offset that turns
  // Plaits' sin into Braids' -cos already applied, so the table read is the
  // cheap non-wrapping one. Braids' zero is 0.75 here.
  float formant_phase_[2];

  // The last 16 internal sub-samples, so the halfband reads by mask.
  float history_[16];
  float history_aux_[16];
  int history_position_;

  float dc_input_;
  float dc_input_aux_;

  // Cleared by Reset(); the first Render() integrates the grain and seeds both
  // blockers before it writes a sample, so Reset()'s own value never reaches
  // the output.
  bool dc_seeded_;

  DISALLOW_COPY_AND_ASSIGN(VosimEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_VOSIM_ENGINE_H_
