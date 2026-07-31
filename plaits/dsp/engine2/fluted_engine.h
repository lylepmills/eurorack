// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// A jet-flute engine derived from Braids' FLUT model: a bore delay line and a
// jet delay line closed into one loop, driven by a cubic jet characteristic
// and a breath pressure the noise multiplies.
//
// The topology, the tables and every constant come from Emilie Gillet's
// DigitalOscillator::RenderFluted (braids/digital_oscillator.cc:1367-1461).
//
// THIS IS A FLUT-DERIVED ENGINE, NOT A CLAIM OF BIT-FIDELITY, and the
// distinction is why it exists. Wave 1 of this port gated FLUT on a
// measurement, measured 37 cents of pitch error, and dropped the model with
// the written conclusion that it could not be ported at 48 kHz. That
// conclusion was right about the arithmetic and wrong about the only fix:
// the engine does not have to run at 48 kHz. What follows is the rate
// analysis wave 1 stopped at, the fix, and the measurements that show it
// works. The catalog copy says "derived from" rather than "a port of", and
// nothing here claims bit-fidelity (R8) -- the deviations are listed at the
// end.
//
// -- RATE (SPEC R5), which is the whole story -------------------------------
//
// RenderFluted's loop is `while (size--)` at digital_oscillator.cc:1405 with
// no `size -= 2` -- compare RenderBowed, which has one at 1285. So it is a
// 96 kHz algorithm: one waveguide step per 96 kHz sample, no internal
// oversampling. Its rate constants therefore do NOT transfer into a 48 kHz
// loop, and three of them break in ways a make-up gain cannot reach because
// all three sit inside the feedback path:
//
//   1. `- (2 << 16)` (line 1390) subtracts two SAMPLES of loop delay, not a
//      fraction of a period. At 48 kHz two samples is twice the time.
//   2. The reflection one-pole (1425-1426) has coefficient
//      lut_flute_body_filter[note] / 4096 and so a group delay of (1 - k) / k
//      INTERNAL SAMPLES. At MIDI 45 the index is 45, k = 409/4096 = 0.09985,
//      and that is 9.01 samples: 0.094 ms at 96 kHz, 0.188 ms at 48 kHz.
//   3. The in-loop DC blocker (1428-1431) has pole 4055/4096 = 0.98999, a
//      corner of 152.9 Hz at 96 kHz and 76.5 Hz at 48 kHz. Its phase at the
//      resonance is a LEAD, and the lead is part of what sets which mode the
//      loop locks into: computed at 110 Hz it is +54.4 deg at 96 kHz against
//      +34.9 deg at 48 kHz, a 19.5 deg swing, which is 0.054 of a period of
//      loop delay appearing out of nowhere.
//
// So the fix is not to re-derive those constants for 48 kHz -- two of the
// three are filters whose phase cannot be re-derived into a delay at all --
// but to run the waveguide at the rate they were written for. This engine
// takes two internal steps per 48 kHz output sample, exactly as blown_engine
// does, and decimates through a 31-tap Kaiser halfband on the way out. Every
// constant above then transfers verbatim.
//
// MEASURED, against braids/test/render_braids_model at --rate 48000, held at
// TIMBRE 1.0 and COLOR 0.5 where the module is deterministic (its own pitch
// across six RNG seeds spans at most 0.05 cents), reading f0 as the
// strongest spectral peak of a 32768-point window rather than by
// autocorrelation:
//
//     note   module Hz   engine Hz   raw cents   less R6's 4.61
//     33      275.303     275.935      +3.97        -0.64
//     45      331.232     332.314      +5.65        +1.04
//     57      659.904     661.762      +4.87        +0.26
//     69      447.709     448.582      +3.37        -1.24
//     81      887.704     890.210      +4.88        +0.27
//
// The 37-cent gate is met with a factor of thirty in hand. The residual after
// the flat 4.61-cent R6 subtraction is not a defect either, and the cause is
// worth recording because it will recur in any waveguide in this port:
// kCorrectedSampleRate makes this engine's DELAY LINES 0.266% shorter than
// the module's, but part of this loop is NOT a delay line -- the two fixed
// samples, and the phase of the two filters -- so a 4.61-cent change in the
// line does not produce a 4.61-cent change in the pitch, and how much it does
// produce depends on the register the note is in. Compensating the delay
// instead of subtracting a constant separates the two. Rendering the engine
// at note - 0.0461 semitones, so its delay lines match the module's exactly,
// at half-integer notes (which keeps the body-filter index the same on both
// sides):
//
//     MIDI 33.5  -0.093 c    45.5  +0.018 c    57.5  -0.074 c
//     MIDI 69.5  -0.075 c    81.5  -0.074 c
//
// so the loop itself tracks the module to under a tenth of a cent, and the
// spread in the table above is the R6 correction propagating through a
// partly rate-fixed loop.
//
// Note what none of those numbers are: the module does not play the written
// note. FLUT overblows. At MIDI 45 (110 Hz nominal) it speaks 331 Hz, at
// MIDI 33 (55 Hz) it speaks 275 Hz, at MIDI 69 (440 Hz) 448 Hz. Which mode it
// picks is a property of the loop, and the engine reproduces the module's
// pitch, register jumps and all.
//
// -- WHICH BRAIDS PARAMETER DRIVES WHAT, with lines -------------------------
//
// RenderFluted uses no INTERPOLATE_PARAMETER macro, so the fold trap in the
// phase-2 guide's section 1 cannot arise here; the indices still have to be
// read the right way round, and they are:
//
//   parameter_[0] is TIMBRE, line 1403: `breath_intensity = 2100 -
//   (parameter_[0] >> 4)`, applied at 1421-1423 as a MULTIPLICATIVE noise on
//   the breath. It runs BACKWARDS -- TIMBRE up means less turbulence -- and
//   the depth spans 2100/4096 = 51.3% at TIMBRE 0 down to 53/4096 = 1.3% at
//   TIMBRE 1. Measured on the MODULE, held at MIDI 45, COLOR noon: TIMBRE 0.0
//   renders at -9.03 dBFS AC RMS with a 10561 Hz spectral centroid, TIMBRE
//   1.0 at -5.43 dBFS and 3590 Hz. The knob is as much a stability control as
//   a timbre one -- at full turbulence the strongest partial sits at 549 Hz
//   and wanders, at minimum it is a steady 331 Hz.
//
//   parameter_[1] is COLOR, line 1391: `jet_delay = (bore_delay >> 8) *
//   (48 + (parameter_[1] >> 10))`, then `bore_delay -= jet_delay` (1392). It
//   splits ONE fixed loop length between the jet and the bore -- 48/256 =
//   18.75% jet at COLOR 0 through 79/256 = 30.86% at COLOR 1 -- so it moves
//   the embouchure without moving the total. That is why COLOR is a timbre
//   control here and not a pitch control, and it is a different axis from
//   BLOWN's COLOR, which moves a filter that is not in the loop at all.
//
// -- THE OUT-OF-BOUNDS READ, which this engine fixes and declares -----------
//
// Line 1404 is `lut_flute_body_filter[pitch_ >> 7]`. That table has 128
// entries (LUT_FLUTE_BODY_FILTER_SIZE, braids/resources.h) and Render()
// clamps pitch_ only to kHighestNote = 140 * 128 (digital_oscillator.cc:44,
// 120-124), so any note above MIDI 127 reads up to 13 entries past the end.
// RenderBlown reads the SAME table off a pitch_ >> 7 of its own -- a different
// index (`(pitch_ - 8192 + (parameter_[1] >> 1)) >> 7`, so shifted down 64
// semitones and moved by COLOR) but the same arithmetic -- and CLAMPS it to
// 0..127 first (1325-1329), which is what proves the intent. This engine clamps.
//
// The fix cannot change the sound anywhere the module is defined: the table
// saturates at its min(0.7, ...) ceiling from entry 79 onward, so every
// in-range note at or above MIDI 79 already reads 2867 and so would every
// out-of-range one if the table continued. The clamp replaces undefined
// behaviour with the value the table would have had.
//
// -- HOW THIS DIFFERS FROM REED PIPE, AND FROM BLOWN ------------------------
//
// Plaits' reed-pipe and Braids' BLOWN are both ONE delay line closed by an
// inverting reflection and driven by a REED: a clipped straight line whose
// output MULTIPLIES the pressure difference, odd-symmetric about its
// operating point. Fluted is a different loop and a different excitation:
//
//   * TWO delay lines in series, not one, with COLOR moving the split. The
//     loop has an internal boundary the reed models do not have.
//   * A JET, not a reed. Lines 1436-1445 read a FIXED characteristic --
//     min(d^3 - d, 1), which dips to -0.385 at d = 0.577, crosses zero at
//     d = 1 and saturates flat above d = 1.33 -- directly, rather than using
//     it as a multiplier on a pressure difference. It is strongly asymmetric
//     and NON-MONOTONIC, which a clipped reed line is not.
//   * A warning about that jet, because it is easy to describe wrongly. The
//     clamp at 1438-1440 reads like a half-wave rectifier, and this engine's
//     first draft spent a macro on making it continuous -- which turned out to
//     change NOTHING: swept end to end at MIDI 45, TIMBRE 0.9, the AC RMS,
//     the spectral centroid and the strongest partial were identical to three
//     decimal places at every position. The reason is that at the module's own
//     operating point the jet flow is DC-biased well clear of zero and the
//     clamp almost never fires. Instrumented over 3 s at MIDI 45 it catches
//     0.000% of samples at TIMBRE 0.9, 0.040% at TIMBRE 0.5, 0.325% at TIMBRE
//     0.0, and 7.9% only at the extreme corner (COLOR 0, TIMBRE 0, both macros
//     at maximum). When it does fire it is mostly because the int8 jet line
//     has WRAPPED, not because the physical flow reversed -- the wrap count
//     tracks it at exactly the rate that implies. What shapes this model's
//     spectrum is the asymmetry of the cubic over a positive-biased range,
//     not rectification.
//   * MULTIPLICATIVE breath noise on an ENVELOPE. Lines 1419-1423: the noise
//     scales the breath rather than adding to it, so turbulence vanishes as
//     the breath does, and the breath has a 1.67 ms attack and a 3.33 ms
//     decay to 80% before it holds. BLOWN's breath is a constant with
//     additive noise and no envelope at all, which is why BLOWN sustains
//     identically forever and this does not.
//   * 8-BIT delay lines. Braids declares FLUT's bore and jet as int8
//     (digital_oscillator.h:371-372) where BLOWN's bore is int16 (369), so
//     the loop carries about 48 dB of quantisation noise as part of its
//     sound. This engine keeps the int8 lines.
//   * An in-loop DC blocker at 152.9 Hz (1428-1431), high enough to be a tone
//     control rather than hygiene, which no reed model here has.
//
// Measured against the landed blown engine, held at MIDI 45 with all four
// macros at noon, the two are not close: 12.75 dB of energy-weighted spectral
// difference, a spectral centroid of 9719 Hz against Blown's 2717 Hz, and
// they do not even speak in the same register -- Fluted's strongest partial
// is 333 Hz where Blown's is 110 Hz.
//
//   HARMONICS  Embouchure  Braids' COLOR: the jet/bore split of one fixed
//                          loop length.
//   TIMBRE     Air         Braids' TIMBRE: breath turbulence. Backwards on
//                          purpose -- it is Braids' axis.
//   MORPH      Blow        breath pressure, which Braids welds at 2x the
//                          excitation envelope (the `<< 1` at 1420).
//   MACRO      Body        an offset on the note index Braids uses to pick
//                          the reflection filter's corner (1404), which
//                          Braids gives no knob at all.
//
// WHY THOSE TWO. Both are welded constants in Braids, and neither is what the
// Plaits neighbour spends its knobs on: reed-pipe puts breath pressure on
// TIMBRE, reed stiffness on MORPH and the reflection coefficient on MACRO,
// and has no jet and no note-tracking body filter. Blow is the flute gesture
// the module cannot make -- blowing harder is what pushes a real flute into
// the next register, and it does that here. Body is the only way to reach a
// filter that is INSIDE this loop and that Braids ties rigidly to the note;
// note that BLOWN's body filter is an output filter, never fed back, so the
// same table does a different job there. Both ranges are measured, and both
// are narrower than they might be, because this is a self-oscillator held at
// one operating point and pushing it far off that point stops the
// oscillation rather than changing it -- see the constants below.
//
// -- OUT and AUX ------------------------------------------------------------
//
// OUT: the bore pressure at the far end, `bore_value >> 1` (1449-1451), which
// is what the module emits.
//
// AUX: a second tap HALFWAY ALONG THE SAME BORE, mono and stereo alike, so the
// pair is one standing wave heard at two points and the stereo render needs no
// separate path. It is level-matched to OUT -- within 0.01 dB at MIDI 33, 45
// and 69, at TIMBRE 0.5 and 0.9 -- and its peak tracks OUT's, worst measured
// 0.9318 against 0.9315 over notes 0..110 crossed with every corner of all
// four macros. The two are not the same signal, though: they correlate between
// -0.11 and +0.12 across MIDI 33/45/57/69/81.
//
// The midpoint was chosen by measurement, not taste. A fixed fraction of the
// bore sets a phase that scales with the mode number, so most fractions
// collapse toward mono or antiphase at some note: measured over those five
// notes, 0.35 of the bore gives +0.78 to +1.00, 0.42 gives -0.67 to +0.68,
// 0.55 gives -0.75 to +0.96, 0.68 gives -0.83 to -0.98. Only the midpoint
// stays decorrelated everywhere.
//
// WHAT AUX IS NOT, and why. The obvious choice for this model is the JET line
// -- the flow at the embouchure rather than at the end of the bore -- and it
// is a genuinely different sound: measured at MIDI 33/45/69, 6.2 to 7.7 dB
// quieter than OUT with a spectral centroid 870 to 2200 Hz higher, correlating
// with it at -0.89 to -0.94. It was rejected on PEAKS. The jet line carries a
// large DC bias, and the decimator's worst-case gain is 1.5, so over the same
// macro sweep its peak reached 2.03 against OUT's 0.93 -- and no fixed trim
// both bounds that and leaves AUX at a usable level. This engine takes the
// bounded, level-matched pickup instead.
//
// Because AUX is the same signal either way, Render() never reads
// parameters.stereo -- there is no separate stereo path to gate. The engine
// still reports stereo_capable() from PLAITS_STEREO_FLUTED, so the voice can
// route OUT/AUX to L/R; with the flag at 0 the same pickup is simply the mono
// aux, which is what it is anyway.

// -- Declared deviations from Braids ----------------------------------------
//
//   - the 96 -> 48 kHz decimator is a 31-tap Kaiser halfband. Braids has no
//     decimator at all -- it drives its DAC at 96 kHz -- and the reference
//     renderer uses a 127-tap Blackman-Harris halfband. R7 wants a number
//     rather than an inherited claim, and this model is brighter than blown,
//     so it was measured here: the coefficients read -6.02 dB at 24 kHz,
//     -15.5 dB at 26.4 kHz, -34.5 dB at 28.8 kHz and below -66 dB from 33 kHz,
//     and decimating THIS engine's own 96 kHz stream through both filters --
//     over MIDI 33/45/69/81 x COLOR 0/0.5/1 at TIMBRE 0, where up to 6.6% of
//     the native energy sits above 24 kHz -- puts the 31-tap 0.02 dB from the
//     127-tap in energy-weighted spectrum and 0.02 dB in AC RMS. The accepted
//     residue is not alias energy but band-edge droop: 1.3 dB in the 20.5-24
//     kHz band, from the wider transition. Numbers and method in the .cc.
//   - a one-pole DC blocker on both outputs, ~7.6 Hz at 48 kHz. The jet
//     characteristic is read over a positive-biased range, so the bore parks
//     well off zero: the reference measures +0.095 (MIDI 33) to +0.153 (MIDI
//     81) of DC. Braids emits that; a Plaits engine must not.
//   - the body-filter index is clamped to 0..127, where Braids reads out of
//     bounds above MIDI 127. It cannot change any note the module defines.
//   - the int8 stores WRAP, where the spec's uniform R8 asks for floor plus
//     SATURATE. R8's stated reason for saturating is that a wrap inside a
//     FLOAT feedback loop is a stability hazard with no analogue to Braids'
//     int32 accumulator bounds; this loop is not a float loop, it is Braids'
//     own int32 arithmetic, so the hazard does not arise and the wrap is
//     bounded exactly as it is on the module. It is also load-bearing: the
//     jet's bottom clamp fires mostly on wrapped jet-line samples (see above),
//     so saturating instead would change the sound. Declared here rather than
//     silently kept. It is also what lets the INNER LOOP alone -- fed Braids'
//     own delay_ and its own noise sequence -- come out sample-identical to
//     RenderFluted at every note and parameter tried. The ENGINE is not
//     bit-identical to the module and does not claim to be: the RNG
//     realisation, the decimator and the output DC blocker below are all real
//     deviations.
//   - the delay note is clamped to 0..128 semitones. The upper end is Braids'
//     own (ComputeDelay, 75-77); the lower end is not -- below MIDI 0 Braids
//     keeps octave-shifting inside ComputeDelay. It makes no audible
//     difference near the boundary (Braids' folded bore at MIDI -12 is within
//     0.1 of a sample of its MIDI 0 one) and only diverges below about MIDI
//     -12, where the fold has already taken over.
//   - the noise is an independent realisation of the same LCG. Braids reads
//     the global stmlib::Random; this engine carries its own state, so the
//     two sides of the A/B never see the same sequence. Measured cost:
//     rendering the MODULE from eight to twelve different seeds spreads its
//     own AC RMS by up to 0.36 dB and its own pairwise spectrum by up to
//     0.37 dB.
//   - the delay lines, the loop arithmetic and both tables are otherwise
//     integer for integer with Braids, INCLUDING the parts a float
//     transcription silently loses: the int8 lines and their `>> 9` floor and
//     two's-complement wrap, stmlib::Mix's `>> 16` (which floors the
//     fractional read back onto the int8 grid), the `>> 12` floors in the
//     reflection filter and the DC blocker, the `>> 1` floors on the
//     reflection, and the jet table's 128-step staircase. With both added
//     macros at noon the inner loop is Braids term for term.
//   - the excitation envelope and the jet characteristic are evaluated from
//     their generator formulae rather than stored, because both reproduce
//     braids/resources.cc at 0 LSB and neither needs a transcendental. The
//     body filter IS stored, because its closed form does.
//
// -- WHAT THE A/B ACTUALLY READS --------------------------------------------
//
// Ten cases, all inside tolerance: AC RMS -0.26 to +0.87 dB, spectrum 0.09 to
// 1.29 dB, pitch -0.8 to +0.7 cents. Eight of the ten sit at or under 0.40 dB
// of spectrum. The outlier is COLOR at its ceiling (0.87 dB / 1.29 dB), and it
// is not a port defect: the same case at MIDI 45.5 instead of 45 reads 0.16
// to 0.31 dB against a module-against-itself spread of 0.22 dB, and the
// MODULE compared against ITSELF two cents away at MIDI 45 reads 1.24 dB of
// spectrum and 0.76 dB of AC RMS. That corner of the model is simply that
// sensitive to bore length, and R6 moves the bore by 4.61 cents whatever the
// engine does. tests/ab.json carries the numbers and the reasoning.

#ifndef PLAITS_DSP_ENGINE2_FLUTED_ENGINE_H_
#define PLAITS_DSP_ENGINE2_FLUTED_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' kWGFBoreLength and kWGJetLength (digital_oscillator.h:44-45), and
// int8 exactly as Braids declares them (369-373). Keeping the lengths keeps
// the model's octave fold for bores too long to fit (1393-1397).
const size_t kFlutedBoreLength = 4096;
const size_t kFlutedJetLength = 1024;

// digital_oscillator.cc:1365 -- kDCBlockingPole = uint16_t(0.99 * 4096).
const int32_t kFlutedDcBlockingPole = 4055;

// The excitation envelope's plateau index. Braids clamps excitation_ptr to
// LUT_BLOWING_ENVELOPE_SIZE - 32 = 360 at the end of every block (1456-1458);
// entries 360..391 are all the same guard value, so clamping per sample reads
// exactly the same numbers.
const int kFlutedEnvelopePlateau = 360;
const int kFlutedEnvelopeAttack = 120;
const int kFlutedEnvelopeDecay = 240;

// Blow: Braids welds the breath at 2x the excitation envelope
// (`breath_pressure <<= 1`, line 1420); this is the multiplier on that, stock
// 1.0.
//
// The window is narrow and closed at BOTH ends, and it was measured rather
// than guessed. Held at MIDI 33/45/57/69, TIMBRE 0.9, Body at noon, AC RMS
// against the raw multiplier:
//
//     0.80  -38 to -41 dBFS   the pipe does not speak; only breath
//     0.90   -9.3 / -5.7 / -5.1 / -5.5 dBFS
//     1.00   -6.0 / -5.5 / -4.6 / -5.0     Braids' own point
//     1.20   -6.4 / -5.7 / -4.9 / -5.6
//     1.30   -8.3 / -6.1 / -6.0 / -6.4
//     1.40  -48 dBFS at every note; it chokes
//
// so 0.90 .. 1.30 is the contiguous live range and that is what the macro
// spans. It is a self-oscillator held at one operating point, and pushing the
// breath far off that point stops the oscillation rather than making it
// louder; a wider range would spend most of the knob on silence. Across the
// range the spectral centroid at MIDI 45 runs 6007 Hz at 0.90, 6734 Hz at
// noon and 7203 Hz at 1.30, and the strongest partial jumps register (551 Hz
// at 0.90, 332 Hz at noon, 551 Hz at 1.30) -- which is the flute gesture the
// module cannot make.
const float kFlutedBlowStock = 1.0f;
const float kFlutedBlowMin = 0.90f;
const float kFlutedBlowMax = 1.30f;

// Body: an offset, in semitones, on the note index Braids uses to pick the
// reflection filter's corner (line 1404). Braids gives that filter no knob at
// all -- unlike BLOWN, where COLOR moves the same table -- and here the filter
// is INSIDE the loop, so moving it changes the bore's damping and which mode
// it settles in, not just its EQ.
//
// The window was measured, not guessed. Held at MIDI 33/45/57/69/81, TIMBRE
// 0.9, other macros at noon: MIDI 33 is dead at -18 semitones (-31.9 dBFS,
// which is the breath alone) and speaking at -12 (-7.25 dBFS); MIDI 45 is dead
// at -30 and speaking at -24; MIDI 57, 69 and 81 speak across the whole span
// tested. -12 is therefore the floor, and the ceiling is where the table
// saturates -- entries 79 and up are all 2867 -- rather than where the tone
// dies.
//
// It does real work across that range, not just level. Measured on the engine
// as shipped, held at MIDI 33, TIMBRE 0.9, other macros at noon: MACRO 0.00
// (offset -12) -> -7.25 dBFS, 0.50 (offset 0) -> -5.98, 0.75 (+18) -> -5.26,
// 1.00 (+36) -> -4.85 dBFS, and the register moves with it -- the strongest
// partial is 276 Hz at noon and 497 Hz from +18 up.
const float kFlutedBodyStock = 0.0f;
const float kFlutedBodyMin = -12.0f;
const float kFlutedBodyMax = 36.0f;

// The second bore tap AUX reads, as a fraction of the bore delay. The
// midpoint, and chosen by measurement rather than taste -- see the OUT and AUX
// note above for the correlations that rule the alternatives out.
const float kFlutedAuxPickup = 0.5f;

// One-pole DC blocker, ~7.6 Hz at 48 kHz.
const float kFlutedDcCoefficient = 0.001f;

class FlutedEngine : public Engine {
 public:
  FlutedEngine() { }
  ~FlutedEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_FLUTED; }

 private:
  int8_t* bore_;
  int8_t* jet_;
  uint32_t delay_pointer_;

  // The reflection one-pole (digital_oscillator.cc:1425-1426).
  int32_t lp_state_;
  // The in-loop DC blocker (1428-1431).
  int32_t dc_blocking_x0_;
  int32_t dc_blocking_y0_;

  // The excitation envelope pointer, and the divider that reproduces Braids'
  // `if (size & 3) ++excitation_ptr` (1452-1454) -- three steps in four.
  int excitation_pointer_;
  // Exact BlowingEnvelope(excitation_pointer_), cached because the pointer
  // advances on only three internal samples in four and eventually stops.
  int blowing_envelope_;
  uint32_t excitation_divider_;

  float noise_depth_;
  float blow_;

  uint32_t rng_state_;

  // 96 -> 48 kHz halfband history, one per output channel.
  float history_[32];
  float history_aux_[32];
  uint32_t history_write_;

  float out_dc_input_;
  float out_dc_output_;
  float aux_dc_input_;
  float aux_dc_output_;

  DISALLOW_COPY_AND_ASSIGN(FlutedEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_FLUTED_ENGINE_H_
