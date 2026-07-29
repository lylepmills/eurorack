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
//     int32 accumulator bounds. Measured cost: rebuilding this file with
//     Braids' wrap restored moves every point of the 36-point note x COLOR x
//     TIMBRE grid by less than 0.06 dB.
//   - lut_bowing_friction (2,018 B) is replaced by the curve the table holds,
//     min(1, 1/(d+0.75)^4), FLOORED to 1/32768ths exactly as the table is.
//     Verified entry by entry: floor matches all 257, round matches only 137.
//     Flash, not speed -- a table lookup is not slower here -- and the values
//     now match the table at 255 of the 256 indices the engine can reach,
//     against up to one LSB high at many of them before. Index 27 alone still
//     reads one LSB high (table 17374, closed form 17375): a float32 rounding
//     artefact of evaluating 1/(d+0.75)^4 right on a floor boundary, 1/32768
//     of full scale and so 256x below the int8 line grid.
//   - lut_bowing_envelope (1,504 B) is replaced by three line segments. The
//     table's 600-step rise is linear to under one LSB and its tail is
//     constant; the 120-step decay between them is within 0.17% (measured:
//     0.978 LSB worst on the rise, and 10.8 LSB of 6553 at BOTH ends of the
//     decay -- the segment spans 599..720 where the table decays 600..719,
//     so it runs low at the start and high at the finish). This is the one
//     table not floored to the integer grid; at 1/32768 it sits 256x below
//     the int8 line quantisation that dominates the loop.
//   - the bridge tap is CLAMPED rather than allowed to go degenerate, at a
//     floor of ONE sample -- which is exactly where Braids goes degenerate.
//     Below one sample the integral part of the tap is zero and Braids' read
//     wraps its modulo to a sample 1024 back instead; that is what the module
//     does from MIDI 84.46 upward at HARMONICS 0, and it is not worth
//     reproducing. The clamp is applied BEFORE neck_delay is taken from delay
//     (bowed_engine.cc:161-168), so it moves the tap within the loop instead
//     of lengthening the loop, and the note does not run flat.
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
// It is measured rather than asserted: tests/ab.json in the package runs
// sixteen cases through ab_engine.py, sweeping both ends of both Braids axes,
// four notes, a re-strike, and the octave fold. ALL FIFTEEN CASES THAT DECLARE
// A TOLERANCE AGREE (high-bridge-clamp declares none and still counts as
// neither -- both sides are dead there). Level runs -2.88 to +4.89 dB against
// Braids, against a declared 1.6x (+4.08 dB) make-up; octave-band spectra sit
// 0.05 to 2.01 dB apart; pitch stays inside +-3 cents after the
// kCorrectedSampleRate correction.
//
// Residual worth knowing, found re-running a wider grid than ab.json samples:
// at note 60 / TIMBRE 0.4 / COLOR 0.5 -- a point no case visits -- the port
// settles an OCTAVE above Braids and its octave-band spectrum reads 4.5-6.0 dB,
// past this package's own 4.0 dB figure, where the pre-fix port read 1.6-1.7.
// It is the neighbouring-limit-cycle effect described above rather than a
// regression in kind: the same knife edge exists pre-fix one COLOR step away
// (note 60 / TIMBRE 0.4 / COLOR 0.45, an octave BELOW), and over the 36-point
// grid the fix takes the points past 4 dB of spectrum from four to one and the
// worst from 10.7 dB to 4.5.
//
// FIXED 2026-07 (the fix that moved the package digest; wave 1 shipped with
// this defect and it is the reason the builder was rolled out). Braids'
// fractional-delay read is `Mix(a, b, frac) << 8` (digital_oscillator.cc:1247)
// against int8 line storage, and stmlib::Mix (stmlib/utils/dsp.h:86) is
// `(a * (65535 - balance) + b * balance) >> 16` returning an int16 -- so the
// interpolation between the two delay-line taps is FLOORED to whole int8
// counts, 1/128 of full scale, before the shift. This port used to interpolate
// in float. That truncation is a loss term inside the stick-slip feedback
// loop, and it is what lets the bow SLIP; without it the model has no slip
// regime at all. MixInt8 in bowed_engine.cc reproduces the integer expression.
//
// WHAT THAT CHANGES FOR A PATCH ALREADY USING THIS ENGINE. Braids is BISTABLE
// along TIMBRE -- it collapses into a near-silent slipping regime in bands
// (at note 45, COLOR 0.5: TIMBRE 0 to .075 and .175 to .225) and bows normally
// between them -- and it also collapses at MID bow force on low notes near the
// bridge. The shipped port was FLAT across all of that, -14.2 to -7.5 dBFS, so
// it played a full-level note at every setting. It now collapses where the
// module collapses: at note 45 / COLOR 0.5 / TIMBRE 0 it renders -41.4 dBFS
// where it used to render -14.2 (Braids -44.1), and at note 24 / COLOR 0 /
// TIMBRE 0.5 it renders -46.4 where it used to render -11.9 (Braids -49.1).
// SO: an existing patch parked in one of those bands will go from a sustained
// full-level bowed tone to a thin near-silent whistle -- about 30 dB quieter,
// which reads as the note dropping out. That is the module's own behaviour and
// the reason its control copy says "from a thin whistle to a hard scrape", but
// it is an audible change, not a rounding difference, and a patch sitting in a
// collapse band will need TIMBRE moved off it.
//
// AND IT IS NOT ONLY THE BANDS -- do not read the paragraph above as "quiet
// corners change, everything else is the same". The truncation is a loss term
// in the loop everywhere, so ordinary bowing settings move too, by up to about
// 5.5 dB and in BOTH directions. Measured, port level before -> after at 3 s,
// at points where Braids bows normally and nothing collapses:
//   note 45 / TIMBRE .75 / COLOR .5 (the `firm-pressure` case, Braids -16.4):
//       -8.5 -> -14.0 dBFS, 5.5 dB QUIETER
//   note 60 / TIMBRE .4  / COLOR 1  (the `mid-neck` case,      Braids -17.5):
//       -9.2 -> -13.9 dBFS, 4.8 dB QUIETER
//   note 60 / TIMBRE .5  / COLOR 1                            (Braids -11.9):
//      -12.9 ->  -8.7 dBFS, 4.3 dB LOUDER
//   note 24 / TIMBRE .4  / COLOR 0                            (Braids -19.5):
//      -13.0 -> -16.3 dBFS, 3.3 dB quieter
// In every one of those the port moved TOWARD Braids -- that is the point of
// the fix -- but a user hears a patch that changed level by half its perceived
// loudness, not a rounding difference. The cases that move least are the ones
// nearest stock. Of the sixteen: SEVEN move under 0.7 dB (stock 0.32,
// pressure-light 0.39, bow-at-bridge 0.13, bow-up-neck 0.62, corner-light-neck
// 0.39, low 0.33, high-clean 0.26); retrigger moves 1.0 and octave-fold 1.6;
// mid-neck 4.8 and firm-pressure 5.5; and the five collapse or dead corners
// move 19 to 33 dB.
//
// Two smaller quantisation corrections landed with it, both measured at under
// 0.4 dB on every A/B case EXCEPT high-bridge-clamp, which the clamp change
// carries from +7.96 dB level / 12.69 dB spectrum to +3.45 dB / 0.62 dB (both
// sides are dead there, so that is two residuals converging, not a fidelity
// gain worth quoting): the friction curve is now floored to the table's
// integer grid, and the bridge-tap clamp dropped from two samples to one. The
// clamp change is audible in one narrow place -- between MIDI 72.9 and 84.5 at
// HARMONICS 0 the engine used to run flat, by up to 36 cents at MIDI 84, and
// now tracks. Anyone who tuned a patch by ear against that flatness will find
// it in tune. Keep the size of that in proportion, though: the clamp only
// engaged with the bow hard against the bridge at the top of the keyboard
// (HARMONICS must be at 0 for the 72.9 figure; by HARMONICS 0.1 the old clamp
// did not engage below MIDI 85), and Braids renders -44 to -61 dBFS across
// that whole corner. So a patch parked there is one the Mix fix ALSO drops by
// 20-30 dB -- measured at note 82 / TIMBRE 1 / HARMONICS 0, port -22.6 ->
// -49.6 dBFS while its pitch error went -33.5 -> +3.8 cents. The note going
// nearly silent is what a user will notice there; the tuning is the footnote.

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
