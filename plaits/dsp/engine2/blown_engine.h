// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' BLOW model: a blown-bore waveguide driven by a nonlinear reed and a
// noisy breath pressure.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderBlown
// (braids/digital_oscillator.cc:1303-1362).
//
// WHAT THE MODEL ACTUALLY IS. One delay line, one reflection, one reed. The
// bore is a single int16 line read at a fractional delay (1338-1342) and
// closed by an INVERTING reflection of -3891/4096 = -0.94995 (1299, 1347), so
// it behaves as a stopped pipe and voices odd harmonics. The excitation is a
// static breath pressure of 26214/32768 = 0.8 (1298) multiplied by white noise
// (1334-1336), and the reed is a two-segment characteristic -- a line of slope
// -1229/4096 = -0.30005 (1300) about an offset of 22938/32768 = 0.69995
// (1301), hard-clipped (1349-1350) -- whose output multiplies the pressure
// difference (1351).
//
// THE TABLES, checked rather than inherited. The brief for this port said to
// read RenderBlown's "own tables (blowing_envelope, blowing_jet)". It has
// neither. RenderBlown reads exactly ONE table, lut_flute_body_filter (1330);
// lut_blowing_envelope and lut_blowing_jet belong to RenderFluted (1419, 1445),
// the model next door. BLOWN has no excitation envelope at all -- its breath
// pressure is a constant, which is why the model sustains indefinitely and why
// a trigger only flushes the bore (1311-1314).
//
// lut_flute_body_filter reproduces at 0 LSB across all 128 entries from its
// generator in braids/resources/lookup_tables.py:171,
//
//     floor(4096 * min(0.7, 0.4 * 2^((n - 69) / 12)))
//
// and it is the FLOOR that reproduces: rounding instead misses by 1 LSB. The
// truncation is proportionally largest where the stored value is smallest --
// -1.46% at n = 0 and n = 12, against -0.02% at n = 48 -- so the port reads
// the truncated integers rather than the closed form (the phase-2 guide's
// section 4, and the particle-burst finding from wave A). Entries 79..127 are
// all 2867, the
// min(0.7, ...) ceiling, so only the first 80 are stored here.
//
// THE OUT-OF-BOUNDS READ NEXT DOOR: BLOWN IS ON THE SAFE SIDE. RenderFluted
// indexes the same 128-entry table with a raw `lut_flute_body_filter[pitch_ >>
// 7]` (1404), and Render() clamps pitch_ only to kHighestNote = 140 * 128
// (digital_oscillator.cc:44, 120-124), so notes above MIDI 127 read past the
// end. RenderBlown computes the same index but CLAMPS it to 0..127 first
// (1325-1329). Confirmed by reading both: BLOWN never reads out of bounds.
//
// THE BRAIDS CONTROLS, with lines.
//
//   TIMBRE is parameter_[0], line 1322: `parameter = 28000 - (parameter_[0] >>
//   1)`, which is the DEPTH of the breath noise at 1334. It runs BACKWARDS --
//   TIMBRE up means less turbulence -- and it is the model's loudness control
//   as much as its timbre control, because turbulence disrupts the standing
//   wave. Measured at MIDI 45, COLOR 0.5: TIMBRE 0.0 renders at -31.1 dBFS AC
//   RMS and TIMBRE 1.0 at -6.3 dBFS, a 24.8 dB swing.
//
//   COLOR is parameter_[1], line 1324: the whole sum is shifted right by 7,
//   so COLOR's contribution is `parameter_[1] >> 8` -- it moves the index into
//   the body filter by up to +127 semitones, one step per 256 counts of
//   parameter_[1]. RenderBlown uses no INTERPOLATE_PARAMETER macro, so the
//   fold trap in the phase-2 guide's section 1 cannot arise here, but the
//   indices still have to be read the right way round.
//
// A MEASURED PROPERTY OF COLOR WORTH KNOWING. Because the index is
// `(pitch_ - 8192 + (COLOR >> 1)) >> 7` clamped to 0..127, and because the
// table saturates at entry 79, COLOR's useful travel depends on the note and
// is never the whole knob. At MIDI 45 the index leaves 0 at COLOR = 0.1563
// (the raw sum crosses zero at 0.1484, but the `>> 7` keeps the index at 0
// until the sum reaches 128) and reaches the 2867 ceiling at COLOR = 0.766;
// outside that the knob does nothing. Verified by rendering the module: COLOR
// 0.0, 0.10, 0.14, 0.15 and 0.156 come out bit-identical to each other, 0.157
// differs, and 0.766, 0.78, 0.80 and 1.0 are bit-identical while 0.75 differs.
// The port reproduces this rather than
// spreading the knob out, because spreading it would stop being the same
// instrument.
//
//   HARMONICS  Body    Braids' COLOR: the body filter's corner.
//   TIMBRE     Focus   Braids' TIMBRE: turbulent and quiet through to steady
//                      and loud. Backwards on purpose -- it is Braids' axis.
//   MORPH      Blow    the static breath pressure, welded at 0.8 in Braids.
//   MACRO      Reed    the reed's rest offset, welded at 0.69995 in Braids.
//
// WHY THOSE TWO. Both are constants in Braids that the module cannot reach,
// and neither is a knob on the Plaits neighbour: reed-pipe puts breath
// pressure on TIMBRE but welds its rest opening (kReedPipeRestOpening = 0.40),
// and spends MORPH on reed stiffness and MACRO on the reflection coefficient
// and its brightness. Blown's TIMBRE is spent on turbulence instead, so
// pressure is unreachable from inside the model, and the reed offset is
// unreachable from either engine.
//
// RATE (SPEC R5). RenderBlown's loop is `while (size--)` at
// digital_oscillator.cc:1331 with no `size -= 2` -- compare RenderBowed, which
// has one at 1285 -- so it is a 96 kHz algorithm, and it has no internal
// oversampling: one waveguide step per 96 kHz sample. The port therefore runs
// 2x from 48 kHz to reach the same 96 kHz internal rate, and every rate
// constant transfers verbatim: the bore delay (1316), the half-sample
// averaging filter inside the loop (1344-1345), and the body-filter one-pole
// (1355-1356). Running the waveguide at 48 kHz instead would have needed all
// three re-derived, and the in-loop averager is the one that cannot be fixed
// by a gain -- its zero sits at the internal Nyquist, so at 48 kHz it would
// damp the upper harmonics twice as hard inside the feedback path.
//
// The pitch is checked against that: Braids' bore delay is `(delay_ >> 1) -
// (1 << 16)` = period/2 - 1 sample at 96 kHz, and the in-loop averager adds
// half a sample, so the round trip is period - 1 and the model plays slightly
// sharp of the note. At MIDI 45 that predicts 110.13 Hz against a nominal
// 110.00, and the reference renders 110.129 Hz. The port keeps the same
// `1 / frequency - 1` so it inherits the same sharpening rather than
// correcting it.
//
// HOW THE PORT MEASURES AGAINST THE MODULE, and why the A/B is mostly
// triggered. BLOWN is a noise-driven chaotic self-oscillator: which limit
// cycle it settles into depends on the noise realisation, and a HELD render is
// not reproducible even against itself. Twenty renders of the MODULE at MIDI
// 45 differing only in RNG seed span 2.59 dB of AC RMS and up to 8.49 dB of
// pairwise spectral difference at COLOR noon, and 10.49 dB / 9.86 dB at COLOR
// 0. Longer renders make it worse, because the model wanders rather than
// averaging. Retriggering collapses it -- a strike flushes the bore, so both
// sides then average over many independent settlings -- and the same
// twenty-seed measurement at triggerHz 8 gives at most 1.18 dB and 0.69 dB.
// Against that, and across four different RNG seeds for the PORT, its
// triggered cases read -0.49 to +0.43 dB of AC RMS, 0.14 to 1.21 dB of
// spectrum, and -0.3 to +0.2 cents on the cases where a cents tolerance is
// meaningful at all. tests/ab.json carries the numbers and the reasoning,
// including the two cases where ab_engine's f0 estimator fails on the MODULE
// and the tolerance is therefore omitted rather than widened.
//
// OUT: the bore read through the body filter, which is what Braids emits.
// AUX (mono): the raw bore pressure before the body filter -- the same
// waveguide, undressed. Measured at MIDI 45 it sits level with OUT at the top
// of COLOR and 6.75 dB above it at the bottom, where the filter is doing most
// of its work. In stereo OUT/AUX become L/R and the right channel is a second
// pickup 0.62 of the way down the bore, so the two sides are the same standing
// wave heard at two points: measured over 1 s of steady tone the two channels
// correlate at +0.009, against +0.88 for the mono out/aux pair, at equal
// level.
//
// Declared deviations from Braids:
//   - the bore line and the loop arithmetic are float, so the port does not
//     reproduce Braids' int16 quantisation of the delay line, of Mix()'s
//     interpolated read, or of the `>> 1` floors in the averaging filter. This
//     is a declared deviation rather than an oversight, and it was measured
//     before it was declared: a float transcription of the loop, Braids' own
//     loop, and the float transcription with the int16 store and every one of
//     Braids' floors reinstated were run off the SAME noise sequence at the
//     same bore delay. The three float variants agreed with each other to
//     0.1 dB on every odd harmonic, and all three tracked Braids to within
//     1.7 dB on its four strongest and 5 dB on its weakest -- so the
//     quantisation is not what shapes this model's harmonic envelope. The
//     noise realisation is; see the paragraph above.
//   - the 96 -> 48 kHz decimator is a 31-tap Kaiser halfband. Braids has no
//     decimator at all (it drives its DAC at 96 kHz); the reference renderer
//     uses a 127-tap Blackman-Harris halfband. Measured against that reference
//     filter on the worst case for it -- maximum breath noise, maximum
//     brightness, where 3.0% of the native energy sits between 24 and 33 kHz
//     -- the 31-tap costs 0.11 dB of spectral difference and 0.03 dB of AC
//     RMS, all of it in the top octave band.
//   - a one-pole DC blocker on both outputs. The model's static breath
//     pressure leaves a real DC offset in the bore (predicted 0.023 from the
//     loop's fixed point, measured 0.015 to 0.018 on the reference), which
//     Braids emits and a Plaits engine must not.

#ifndef PLAITS_DSP_ENGINE2_BLOWN_ENGINE_H_
#define PLAITS_DSP_ENGINE2_BLOWN_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' kWGBoreLength (digital_oscillator.h:43). Keeping it keeps the
// model's 23.4 Hz floor, below which Braids folds the bore up an octave
// (digital_oscillator.cc:1317-1319).
const size_t kBlownBoreLength = 2048;

// digital_oscillator.cc:1298-1301, normalized by the fixed-point scale each
// constant is applied at.
const float kBlownBreathPressure = 26214.0f / 32768.0f;      // 0.80000
const float kBlownReflection = -3891.0f / 4096.0f;           // -0.94995
const float kBlownReedSlope = -1229.0f / 4096.0f;            // -0.30005
const float kBlownReedOffset = 22938.0f / 32768.0f;          // 0.69995

// Braids' CLIP() macro pins to +/-32767, not +/-32768 (stmlib/stmlib.h:39).
const float kBlownClip = 32767.0f / 32768.0f;

// The two added macros are bounded by the model's OSCILLATION WINDOW, which is
// narrow and closed at BOTH ends. Linearising the loop about its static fixed
// point -- solve `slope*P^2 + (offset - 1/reflection)*P + blow*(1 - 1/
// reflection) = 0` for the pressure difference P, then the small-signal loop
// gain is |reflection * (2*slope*P + offset)| -- the pipe self-oscillates only
// where that exceeds 1:
//
//   reed offset, at Braids' blow of 0.8: predicted to oscillate between 0.53
//   and 0.79. Below that the loop gain falls under 1 (0.885 at 0.35). Above
//   it the reed's OPERATING POINT hits its own clip, the loop linearises and
//   the gain drops to a flat 0.950 -- so a wide-open reed does not overblow,
//   it stops speaking.
//
//   blow, at Braids' offset of 0.69995: predicted 0.55 to 1.04, choking above
//   that for the same reason.
//
// Rendering confirms both walls and puts the upper one slightly lower than the
// prediction: at reed offset 1.02 the engine measures 0.0001 RMS (dead), at
// 0.78 still only 0.018, at 0.34 0.014, and at blow 0.42 0.0036. The ranges
// below stay inside the measured window, with Braids' constants at noon.
//
// Both then do real spectral work rather than just gating the level. Swept
// held at MIDI 45, TIMBRE 0.7: Blow runs -33.4 dBFS with a 3430 Hz centroid at
// its floor, through -12.2 dB / 1826 Hz at noon, to -17.3 dB / 2478 Hz at its
// ceiling -- blowing harder brightens the tone while barely moving its level.
// Reed runs -29.9 dB / 3868 Hz, through -9.8 dB / 1716 Hz just below noon, to
// -28.4 dB / 3439 Hz at its ceiling -- a hump, with the tone thinning back
// into breath as the reed pins toward its clip at either end.
const float kBlownBlowMin = 0.55f;
const float kBlownBlowMax = 1.00f;

const float kBlownReedMin = 0.50f;
const float kBlownReedMax = 0.76f;

// Second pickup for the stereo render, as a fraction of the bore delay. Far
// enough from the driven end to hear a different set of nodes, not so far
// that the right channel loses the fundamental.
const float kBlownStereoPickup = 0.62f;

// The mono AUX is the raw bore pressure at unity -- no trim and no make-up.
// Measured at MIDI 45: the body filter is unity at DC and the bore's energy is
// mostly in the fundamental and the low odd harmonics, so filtering costs very
// little at the top of COLOR and a lot at the bottom. AUX therefore sits level
// with OUT above COLOR 0.8 (+0.03 dB) and runs 6.75 dB above it at COLOR 0,
// and any fixed trim would only move where the two happen to match. Peaks
// track OUT's closely -- 0.481 against 0.476 at COLOR 1, worst measured 0.521
// -- so unity is also the safe choice.
const float kBlownAuxTrim = 1.0f;

// One-pole DC blocker, ~7.6 Hz at 48 kHz. Far below anything the bore makes.
const float kBlownDcCoefficient = 0.001f;

class BlownEngine : public Engine {
 public:
  BlownEngine() { }
  ~BlownEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_BLOWN; }

 private:
  float* bore_;
  uint32_t delay_pointer_;

  // The loop's half-sample averager (digital_oscillator.cc:1344-1345).
  float lp_state_;
  // The output body filter (1355-1356).
  float body_state_;
  float body_state_aux_;

  float noise_depth_;
  float body_coefficient_;
  float blow_;
  float reed_offset_;

  uint32_t rng_state_;

  // 96 -> 48 kHz halfband history, one per output channel.
  float history_[32];
  float history_aux_[32];
  uint32_t history_write_;

  float out_dc_input_;
  float out_dc_output_;
  float aux_dc_input_;
  float aux_dc_output_;

  DISALLOW_COPY_AND_ASSIGN(BlownEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_BLOWN_ENGINE_H_
