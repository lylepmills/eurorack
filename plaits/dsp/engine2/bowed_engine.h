// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' BOWD: a bowed-string waveguide with a stick-slip friction exciter.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderBowed. It ends in
// `size -= 2`, so it is a 48 kHz algorithm writing a 96 kHz stream through a
// 2x linear interpolator -- which means every rate constant, the bridge
// filter and the body biquad transfer verbatim, and only the output stage has
// to be re-derived. Nothing in the stock palette is a continuous stick-slip
// friction voice.
//
// MORPH is new: Braids welds the nut reflection to -1.0 and this opens it.
// MACRO is new: it moves the body resonance +-1 octave around the stock pole.
//
// OUT: the bridge pickup through the body filter.
//
// AUX (mono): the BOW, not the string -- `new_velocity`, the stick-slip
// friction output, before the waveguide or the body has coloured it. A dry
// unpitched scrape against a resonated string, and the in-tree idiom for a
// physical model: inharmonic-string, modal-resonator and particle-noise all
// put their raw exciter on AUX, and all three drop it in stereo. It is
// DC-blocked (the bowing envelope is unipolar, so the friction output carries
// bow pressure as offset) and scaled to sit at OUT's level.
//
// In stereo OUT/AUX become L/R and AUX reverts to the neck pickup through its
// own copy of the body filter -- a genuine pair of pickup positions at matched
// gain, which the exciter would not be. Only one branch runs per block, so the
// split costs no CPU; the stereo path is the more expensive of the two and
// therefore still sets the engine's peak. NOTE the package manifest and the
// catalog both describe AUX as the neck pickup unconditionally, which is only
// the STEREO branch; EngineParameters::stereo defaults false and voice.cc:194
// is the only thing that raises it, so a mono patch gets the exciter. Both
// records feed a shipped digest, so the mismatch is reported, not corrected.
//
// MEMORY: the delay lines are int8 exactly as in Braids -- 1024 + 4096 = 5 KB
// of the 16 KB arena. Braids quantizes every write to int8 anyway (R8 keeps
// that, it is audibly part of the model), so storing float would spend 4x the
// memory to hold values that have already been rounded. Keeping Braids' own
// lengths keeps Braids' own octave-fold floor: 11.44 Hz at HARMONICS 0, 12.64
// at HARMONICS 1, and a minimum of 9.38 Hz at parameter_1 = 51 where the neck
// and bridge overflow thresholds cross. Float lines halved to fit the arena
// (512 + 2048, 10 KB) would put that floor at 22.9 Hz at HARMONICS 0 and no
// lower than 18.8 Hz anywhere -- so the residual fidelity gap the spec raised
// as an open question does not arise.
//
// Declared deviations from Braids:
//   - int8 writes FLOOR and SATURATE (R8). Braids floors and WRAPS; wrap
//     inside a feedback loop is a stability hazard with no analogue to its
//     int32 accumulator bounds.
//   - lut_bowing_friction (2,018 B) is replaced by min(1, 1/(d+0.75)^4),
//     which is the curve the table holds: exact at both ends (32768 at d=0,
//     64 at d=4). Flash, not speed -- a table lookup is not slower here.
//     Braids' table is that curve FLOORED to integers, so this closed form
//     reads up to one LSB high, worst +1.48% relative at index 253 where the
//     entry is 66 and the exact value 66.97. Rebuilding this file with the
//     floor restored moves every point of the 36-point note x COLOR x TIMBRE
//     grid below by less than 0.06 dB, so it is a real but inaudible
//     quantisation loss, not the one that matters (see KNOWN DEFECT).
//   - lut_bowing_envelope (1,504 B) is replaced by three line segments. The
//     table's 600-step rise is linear to under one LSB and its tail is
//     constant; the 120-step decay between them is within 0.17% (measured:
//     0.978 LSB worst on the rise, and 10.8 LSB of 6553 at BOTH ends of the
//     decay -- the segment spans 599..720 where the table decays 600..719,
//     so it runs low at the start and high at the finish).
//   - the bridge tap is CLAMPED rather than allowed to go degenerate. In
//     Braids a bridge delay under one sample wraps the modulo and reads
//     1024 samples back instead; from MIDI 84.5 upward at HARMONICS 0
//     that is what the model actually does, and it is not worth reproducing.
//     Note the clamp floor is TWO samples, not one, so it engages from
//     MIDI 72.9 upward at HARMONICS 0 -- a full octave below the point Braids
//     goes degenerate, and over that octave Braids' tap is a perfectly valid
//     1..2 samples that the port lengthens. It is applied AFTER neck_delay
//     has been taken from delay (bowed_engine.cc:142-146), so it lengthens
//     the whole loop rather than moving the tap within it, and the note runs
//     FLAT over that octave: 0.4 cents at MIDI 73, 36.3 cents at MIDI 84.
//     The A/B corner is dead on both sides (-61 dBFS against -40), so nothing
//     audible turns on it, but the deviation is wider than "where Braids goes
//     degenerate" implies.
//   - the output stage is re-derived. Braids' `(out + previous) >> 1` then
//     `out` is a 2x linear-interpolating UPSAMPLER writing 96 kHz, not a
//     filter; its baseband effect is -1.4 dB at 12 kHz. Re-implementing it
//     literally at 48 kHz would give -3.0 dB there plus a hard null at
//     24 kHz -- darker than BOWD, the opposite of the intent -- so it is a
//     one-pole matched to the real response instead.
//   - SoftClip replaces CLIP, with the make-up gain inside it (R3), and the
//     output carries a 1.6x make-up Braids does not, because BOWD runs at
//     about -15 dBFS and the palette expects near-full-scale engines.
//
// ON MATCHING THIS ONE AGAINST HARDWARE: bowed is a nonlinear self-oscillator,
// not an oscillator. The port renders 4.61 cents sharp -- the standard
// kCorrectedSampleRate offset every engine here carries -- and at MIDI 45 that
// is a quarter of a percent of a 434-sample loop. In a stick-slip feedback
// system that is enough to settle into a neighbouring limit cycle, so
// sample- or bin-level agreement is not a meaningful target for this engine.
//
// It is now measured rather than asserted: tests/ab.json in the package runs
// sixteen cases through ab_engine.py, sweeping both ends of both Braids axes,
// four notes, a re-strike, and the octave fold. Where the model agrees, it
// agrees more closely than this comment used to claim -- pitch inside
// +-2 cents after the kCorrectedSampleRate correction (not 8), and
// octave-band spectra 0.22 to 3.19 dB apart (not 3-5), with level running
// +4.10 dB at stock, which is the declared 1.6x make-up almost exactly. Nine
// cases agree, SIX FAIL on the defect below, and high-bridge-clamp declares no
// tolerance because both sides are dead there.
//
// KNOWN DEFECT, NOT A DECLARED DEVIATION (found by that A/B, 2026-07; left
// unfixed pending a decision, because a fix moves the package digest).
// Braids' fractional-delay read is `Mix(a, b, frac) << 8`
// (digital_oscillator.cc:1247), and stmlib::Mix (stmlib/utils/dsp.h:86)
// returns an int16 -- so the interpolation between the two delay-line taps is
// QUANTISED to whole int8 counts, 1/128 of full scale, before the shift. This
// port interpolates in float (bowed_engine.cc:190). That truncation is a loss
// term inside the feedback loop, and it is what lets Braids' bow SLIP.
//
// SCOPE, measured -- and wider than "the low-TIMBRE end". Braids is BISTABLE
// along TIMBRE rather than uniformly quiet at the light-bow end: at note 45,
// COLOR 0.5 it renders -44.1 / -18.4 / -41.3 / -19.1 / -18.0 / -15.1 / -13.2 /
// -10.3 dBFS at TIMBRE 0 / .1 / .2 / .3 / .4 / .5 / .7 / 1, collapsing in
// bands (TIMBRE 0 to .075 and .175 to .225) and bowing normally between them.
// The port is flat across that axis, -14.2 to -7.5 dBFS, so it misses every
// collapse. It also misses collapses at MID and HIGH bow force elsewhere on
// the grid: over 36 points (notes 24/45/60 x COLOR 0/.5/1 x TIMBRE
// .25/.4/.5/.75) the port breaks the A/B's 5 dB tolerance at 13 of them, worst
// at note 24 / TIMBRE 0.5 / COLOR 0.0, where Braids slips to -49.1 dBFS and
// the port sustains -11.9 (+37.2 dB).
//
// ISOLATION. Rebuilding THIS FILE with only the Mix truncation restored (the
// exact integer `(a*(65535-bal) + b*bal) >> 16`) moves note 45 / TIMBRE 0 from
// -14.21 to -39.96 dBFS against Braids' -44.08, and brings all 36 grid points
// into -2.8 to +5.5 dB, i.e. back inside the declared 1.6x make-up. Rebuilding
// it with Braids' int8 WRAP instead of saturation, or with the friction curve
// floored to Braids' integer table, moves every one of those points by less
// than 0.06 dB. The truncation is the whole of it.

#ifndef PLAITS_DSP_ENGINE2_BOWED_ENGINE_H_
#define PLAITS_DSP_ENGINE2_BOWED_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' kWGBridgeLength / kWGNeckLength, both powers of two so the wrap is
// a mask rather than a modulo.
const size_t kBowedBridgeLength = 1024;
const size_t kBowedNeckLength = 4096;

// Bridge reflection one-pole: kBridgeLPGain 14008 and kBridgeLPPole1 18022,
// over 32768. DC gain 0.4275 / (1 - 0.55) = 0.95, the reflection loss.
const float kBowedBridgeLpGain = 14008.0f / 32768.0f;
const float kBowedBridgeLpPole = 18022.0f / 32768.0f;

// Body biquad: r = sqrt(2959/4096), a1 = 6948/4096 = 2*r*cos(theta),
// gain 6553/32768. Braids' own pole angle is acos(a1 / 2r) = 0.0651607 rad,
// which is 497.8 Hz at 48 kHz -- the constant below is 0.94% high, putting the
// stock pole at 502.5 Hz and a1 at 1.696221 against Braids' 1.696289. On a
// pole of radius 0.85, a resonance over 2 kHz wide, that is inaudible; but the
// detent therefore does NOT reduce the biquad to Braids' coefficients exactly.
const float kBowedBodyGain = 6553.0f / 32768.0f;
const float kBowedBodyRadius = 0.849949f;
const float kBowedStockTheta = 0.0657752f;

// `parameter_0 = 172 - (TIMBRE >> 8)` in [45, 172], scaled by 1/32.
const float kBowedPressureMin = 45.0f;
const float kBowedPressureMax = 172.0f;

// `parameter_1 = 6 + (HARMONICS >> 9)` in [6, 69], over 256.
const float kBowedBowPositionMin = 6.0f / 256.0f;
const float kBowedBowPositionMax = 69.0f / 256.0f;

// Bowing envelope, as fractions of full scale: 6553/32768 peak after a
// 600-step linear rise, decaying over 120 steps to a 5242/32768 sustain.
const float kBowedEnvelopePeak = 6553.0f / 32768.0f;
const float kBowedEnvelopeSustain = 5242.0f / 32768.0f;
const float kBowedEnvelopeRise = 599.0f;
const float kBowedEnvelopeHold = 720.0f;

// The one-pole standing in for Braids' 2x interpolating upsampler: -1.5 dB at
// 12 kHz against its -1.4 dB.
const float kBowedTilt = 0.15f;

// Mono AUX carries the bow exciter, whose envelope is unipolar -- so it needs
// a blocker. Corner near 7.6 Hz, an octave-and-more below the model's lowest
// note, so the scrape's own shape is untouched.
const float kBowedExciterDcPole = 0.999f;

// Make-up for the exciter, INSIDE the SoftClip as the output stage already is
// (R3). It runs far COLDER than the string: in steady state the string
// velocity approaches the bow velocity, so the friction output -- which is
// the DIFFERENCE -- shrinks as the resonance builds. Over a 500 ms note across
// the parameter grid the raw exciter sits about 14 dB under OUT; 6.77 brings
// it to -0.44 dB at a peak of 0.81, so aux_gain stays equal to out_gain.
//
// Measure this over a whole NOTE, not a settled tail. The string keeps
// building for ~100 ms while the exciter settles toward equilibrium, so the
// same two signals measure -10 dB apart over 30 ms and -16 dB apart once
// settled. Neither window is what a player hears.
const float kBowedExciterGain = 6.77f;

class BowedEngine : public Engine {
 public:
  BowedEngine() { }
  ~BowedEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern B: mono AUX is the bow exciter, a different signal in kind; the
  // stereo branch replaces it with the neck pickup so L/R stay a matched pair.
  virtual bool stereo_capable() const { return PLAITS_STEREO_BOWED; }

 private:
  int8_t* bridge_line_;
  int8_t* neck_line_;

  uint32_t delay_pointer_;
  float excitation_;
  float bridge_lp_;

  float body_y0_;
  float body_y1_;
  float body_aux_y0_;
  float body_aux_y1_;

  float tilt_state_;
  float tilt_state_aux_;

  float exciter_dc_in_;
  float exciter_dc_out_;

  DISALLOW_COPY_AND_ASSIGN(BowedEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_BOWED_ENGINE_H_
