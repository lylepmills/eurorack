// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' PRTC: three fixed-interval resonant filters, each re-tuned and
// re-triggered by a shared decaying noise burst that fires at a random,
// density-controlled rate.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderParticleNoise
// (braids/digital_oscillator.cc:2080-2179).
//
// LINEAGE. Plaits' own particle-noise engine (plaits/dsp/engine/
// particle_engine.cc) is the refined descendant of this model, and the two
// are structurally different, not just differently tuned. particle-noise
// runs SIX particles (kNumParticles, particle_engine.h:45) whose pitches are
// each independently RANDOMIZED around the played note by a `spread` in
// semitones (particle_engine.cc:82, `48.0f * harmonics^2`) -- a scattered
// cluster centred on one pitch. PRTC runs exactly THREE filters at FIXED
// intervals above the note -- +12, +15.125 and +19 semitones
// (digital_oscillator.cc:2108,2114,2119, the constants 0x600/0x980/0x790)
// -- which is a fixed chord voicing, not a cloud; COLOR only jitters each
// filter a little around ITS OWN fixed interval, it never lets them collide
// or reorder. particle-noise also carries a diffuser (a decorrelating
// all-pass network, plaits/dsp/fx/diffuser.h) and a post low-pass that PRTC
// has neither of, and exposes an explicit Q control (particle_engine.cc:78,
// MORPH's top half) where PRTC's resonance is a hardwired constant
// (kResonanceFactor, digital_oscillator.cc:2078) the module never exposed.
// So the two do not overlap: particle-noise is a controllable cloud around
// one pitch with variable resonance and a diffuser; PRTC is a fixed chord of
// three hard resonators with a burst-driven decay, and that chord shape is
// what this port's MORPH spends its axis on (below) precisely because
// particle-noise has no equivalent -- it cannot produce a fixed intervallic
// voicing at all, only a spread cluster.
//
// PARAMETER MAPPING, with the line it was read from.
//   parameter_[0] (TIMBRE) sets DENSITY, the trigger probability per sample:
//     `uint32_t density = 1024 + parameter_[0];` (digital_oscillator.cc:2085)
//     tested against 23 bits of the same RNG draw every sample:
//     `if ((noise & 0x7fffff) < density)` (digital_oscillator.cc:2104).
//   parameter_[1] (COLOR) sets the per-filter JITTER around each filter's
//     fixed interval, drawn fresh on every trigger from the SAME noise word
//     that fired it (digital_oscillator.cc:2106-2119):
//     `p1 = pitch_ + (3 * noise_a * parameter_[1] >> 17) + 0x600`
//     `p2 = pitch_ + (noise_a * parameter_[1] >> 15) + 0x980`
//     `p3 = pitch_ + (noise_b * parameter_[1] >> 16) + 0x790`
//     Each filter's jitter formula is different (a different shift and a
//     different noise slice for filter 3), so at max COLOR the three
//     filters spread by different amounts -- filter 1 the least (+-12
//     semitones), filters 2 and 3 the most (+-16 semitones) -- and that
//     asymmetry is carried over unchanged rather than symmetrised.
//   Reading the suffix as "the first/second parameter" rather than the
//   parameter INDEX still gives density-from-TIMBRE and jitter-from-COLOR
//   here (both READS happen to land on the correct array slot for this
//   model), so this engine does not carry fold's exact trap -- but the
//   convention is the same one that cost fold a rewrite and is documented
//   here for the next model that reads it the wrong way.
//
// RATE. RenderParticleNoise ends in `size -= 2` (digital_oscillator.cc:2163)
// writing the SAME sample twice, not linear-interpolating between two
// (contrast bowed_engine.h, whose 2x upsampler averages current and
// previous) -- so the inner algorithm, including the RNG draw, the trigger
// test, the retuning and the two-pole recursion, runs once per TWO 96 kHz
// output samples, i.e. at 48 kHz. Braids' resonator table generator
// (braids/resources/lookup_tables.py:65-90) computes its cutoff as
// `cutoff / (sample_rate / 2)` with the file's global `sample_rate = 96000`
// (lookup_tables.py:35) -- that denominator is 48000, so the table is
// ALREADY normalised for a 48 kHz filter rate, not the nominal-96-kHz rate
// its own comment might suggest. Both facts point the same way (R5): the
// port runs the filter recursion once per OUTPUT sample at Plaits' native
// 48 kHz, no oversampling, and the table's closed form transfers with a
// plain 48000.0f denominator, verified below.
//
// The one thing the 48 kHz render does NOT reproduce is the duplicate write
// itself. Repeating each computed sample twice at 96 kHz is a 2x zero-order
// hold, `H(z) = 1 + z^-1` at 96 kHz, and render_braids_model.cc's halfband
// decimator only removes the IMAGES that creates -- it does not undo the
// hold's in-band tilt, `|cos(pi*f/96000)|`, which is -0.26 dB at 7 kHz,
// -1.0 dB at 14.5 kHz and -3.0 dB at 24 kHz. So the port reads BRIGHTER than
// the decimated reference in the top two octaves, and does so by exactly
// that amount: every A/B case measures +1.0 dB in 10.24-20.48 kHz and
// +3.1 dB in 20.48-24 kHz, against <=0.3 dB everywhere below. That residual
// is a declared deviation (below), not a decimator artefact, and it is left
// unmodelled deliberately: the two bands carry 0.6% of total energy between
// them, and the exact correction is a fractional-delay filter at 48 kHz
// whose own approximation error would be the same order as the tilt.
//
// TABLE VERIFICATION. `lut_resonator_coefficient` and `lut_resonator_scale`
// (braids/resources.cc:44-115, 129 entries each, one per MIDI note 0-128)
// are reproduced from braids/resources/lookup_tables.py:59-90 -- coefficient
// from the closed form `2*cos(2*pi*f)*32768` truncated toward zero (Python's
// `int()`), scale from the 2000-sample analytic impulse-response simulation
// at a CALIBRATION resonance of 0.99985 (higher than the 0.996 the filter
// actually runs at -- a deliberate Braids headroom choice this port keeps).
// Both reproduce at 0 LSB across all 129 entries against the embedded ints
// (verified programmatically, not retyped).
//
// BOTH ARE EMBEDDED AT BRAIDS' uint16 WIDTH and read back through Braids'
// own integer Interpolate824, NOT re-derived in float. That is the opposite
// of fold's "skip the quantisation step" practice, and deliberately so,
// because here the quantisation is not a rounding detail -- it is the
// model's tuning and level:
//   - `lut_resonator_coefficient` stores `2*cos(theta)` in Q1.15, so near
//     theta = 0 it sits just below 65536 and ONE integer LSB is a large step
//     in pole ANGLE. Braids' own table therefore mistunes its low resonators
//     badly, and audibly: at MIDI 57 (the +12 filter over a note-45 root)
//     the truncated table puts the pole at 227.5 Hz rather than the nominal
//     220 Hz (+74 cents), at MIDI 64 +24 cents, at MIDI 33 +623 cents. Above
//     ~MIDI 90 it converges to under 1.5 cents. Evaluating the closed form
//     in float "fixes" a mistuning that IS the model: doing so measured the
//     port's three note-45 resonators 75 / 58 / 24 cents FLAT of Braids'.
//   - `lut_resonator_scale` is a per-filter GAIN, and truncation there is
//     large where the values are small: 1.96 -> 1 at MIDI 20 (6 dB), 5.83 ->
//     5 at MIDI 33. Holding it in float made the port read a consistent
//     +0.6 dB louder than Braids in every A/B case except high-register --
//     where the table saturates at exactly 256 and there is no truncation to
//     miss, which is why that one case was the only one already correct.
// `c * kResonanceFactor >> 15` (digital_oscillator.cc:2123-2125) is likewise
// carried at Braids' truncating integer width. All of this runs on a trigger
// only, at most a few hundred times a second, so R12's per-sample cost
// concern does not apply -- and two table reads are in fact cheaper than the
// `Sine()` call the float closed form needed.
//
// PITCH CORRECTION. Braids has no clock-deviation compensation to carry
// (braids.cc's 96 kHz timer has no analogue of Plaits' kCorrectedSampleRate
// quirk); this port's resonator centres take it anyway, the same way every
// pitched engine's oscillator does (SPEC R6; precedent: z_filter_engine.cc:
// 117,127 derives its cutoff through `NoteToFrequency`, which carries the
// correction). There is no frequency to scale here -- the resonator centre
// is a TABLE INDEX in Braids' 1/128-semitone pitch space -- so the
// correction is applied as a single upfront shift on the note,
// kParticlePitchCorrection = `12 * log2(48000/47872.34)` = +0.0461
// semitones (+4.61 cents), which is the same ratio `NoteToFrequency` folds
// into its `a0` and keeps every offset and jitter term below it in the ONE
// note-space Braids used.
//
// This IS measured, not just asserted, but not by `ab_engine.py`'s cents
// figure (see A/B NOTES for why that number is unusable here). Comparing
// the spectral CENTROID of the chord's octave between reference and port,
// on the unjittered COLOR=0 chord: +5.1 cents at note 45 and +5.3 cents at
// note 82 with the correction in, -2.8 and +0.8 cents with
// kParticlePitchCorrection forced to 0. The correction moves the port by
// the +4.6 cents it is supposed to, in the direction it is supposed to.
//
// Caveat worth knowing: below ~MIDI 40 the coefficient table's integer step
// is worth far more than 4.6 cents (see TABLE VERIFICATION), so at very low
// resonator centres this shift can cross an LSB boundary and land tens of
// cents away rather than 4.6. That is Braids' quantisation amplifying a
// legitimate shift, not a second error, and the alternative -- skipping R6
// for this engine -- does not make the low register any closer.
//
// THE FOURTH MACRO.
//   MORPH  Chord width   Scales the three fixed offsets (+12, +15.125, +19
//                         semitones) from unison at 0 through Braids' own
//                         voicing at 0.5 to double the spread at 1 --
//                         reshaping the chord itself, which neither the
//                         module (the offsets are hardwired) nor Plaits'
//                         particle-noise (which has no fixed intervals to
//                         begin with, only a randomised cluster) can do.
//   MACRO  Decay          Braids' burst envelope always decays at the same
//                         fixed rate, kParticleNoiseDecay = 64763/65536 per
//                         48 kHz sample (digital_oscillator.cc:2076,2129) --
//                         about a 12 ms T60. This macro reshapes that
//                         directly (ApplyMacro on the raw per-sample decay
//                         coefficient, the same quantity Braids hardcodes,
//                         not a derived time constant), from a ~4 ms tick at
//                         0 through the stock ~12 ms at 0.5 to a
//                         ~100 ms bloom at 1. particle-noise has no
//                         comparable per-particle decay control either --
//                         its envelope shape is fixed inside its own
//                         Particle class.
//
// OUT: the three filters' sum, hard-clamped to +-1 exactly where Braids
// clips (digital_oscillator.cc:2160) -- a bounded expression of
// individually-clamped terms (R1/R2), so this engine uses a positive gain.
// AUX (mono): the raw decaying-noise burst BEFORE the three filters --
// unpitched, matching the in-tree idiom bowed_engine.h documents for a
// physical model's exciter, and the same role Plaits' own particle-noise
// gives its AUX ("raw random pulses", particle_engine.h:30).
//
// In stereo, AUX drops the raw exciter for a second, fully independent
// filter bank and RNG stream (its own amplitude, its own three filters, its
// own draws from Random::GetWord()) driven by the SAME density/jitter/width/
// decay settings -- genuinely decorrelated left and right burst patterns,
// not a panned copy of one. The right-channel draws happen only inside the
// stereo branch, so turning stereo on or off never changes the mono/left
// render's random sequence (R14). This matches the idiom bowed_engine.h
// names as shared across the in-tree physical models: raw exciter on AUX in
// mono, dropped for true stereo.
//
// Declared deviations from Braids:
//   - the 2x zero-order hold on the output is not reproduced (see RATE
//     above): the port reads about +1.0 dB high over 10.24-20.48 kHz and
//     +3.1 dB over 20.48-24 kHz, two bands that carry 0.6% of the energy
//     between them, and is within 0.3 dB everywhere below.
//   - the jitter and offset arithmetic is done in float and only the final
//     14-bit pitch `p` is floored back onto Braids' integer grid, so it
//     divides exactly where Braids' `>>17`/`>>15`/`>>16` on a value that CAN
//     be negative (noise_a, noise_b) floors toward negative infinity. `p`
//     lands within one 1/128-semitone count of Braids'.
//   - the two-pole recursion runs in float, so it does not carry Braids'
//     per-step `>> 15` truncation. That is inaudible where the scale table
//     puts real signal in the resonators, but at very low resonator centres
//     (below ~MIDI 40, i.e. a low root with MORPH near unison) Braids drives
//     them at only a few integer LSB and the port reads a few dB louder and
//     cleaner there. Measured at note 21 with MORPH at noon: +3.6 dB over
//     the chord's octave. Not corrected -- reproducing it would mean
//     rebuilding the whole recursion in fixed point.
//   - RenderParticleNoise never reads `sync`/TRIGGER at all -- PRTC free-
//     runs regardless of gate state on the module -- so this port ignores
//     `parameters.trigger` the same way, rather than adding a reset the
//     source has no equivalent of.
//
// A/B NOTES. Full-band spectrum is 0.06-0.11 dB in five of the six cases
// (density-low is the exception, next paragraph) and AC RMS is +0.13 to
// +0.37 dB. Per-band, everything below 5 kHz is within 0.3 dB; the only
// systematic residual is the unmodelled zero-order hold in the top two
// bands (+1.0 / +3.1 dB, 0.6% of energy -- see RATE).
//
// `cents` is NOT declared in tests/ab.json, and the reason is stronger than
// a hunch. `ab_engine.py`'s single-lag autocorrelation f0 tracker does not
// track this source AT ALL at moderate registers: fixing the resonator
// coefficient quantisation moved this port's three note-45 resonators by
// +75 / +58 / +24 cents, a change plainly visible in the band table and in
// the chord's spectral centroid, and the tool's reported cents figure for
// every moderate-note case did not move by so much as 0.1 cent (-4.6
// before, -4.6 after). Only high-register responded (-2.5 -> -1.3). Three
// inharmonic, closely-registered resonant peaks excited by independently-
// seeded noise bursts give a single-lag autocorrelator no reliable
// fundamental to lock onto (SPEC's granular-cloud caveat, carried over
// here). Pitch on this engine has to be checked by spectral centroid on the
// COLOR=0 chord instead -- see PITCH CORRECTION for that measurement.
//
// density-low (TIMBRE 0, the sparsest setting) is the one case with a
// widened spectrum tolerance (2.5 dB; it measures 1.41 dB). The excess is
// confined to the 20-40 and 40-80 Hz bands, well below any resonator's
// tuned frequency: at this density a burst fires about six times a second,
// so those bands are not tone at all, they are the shot noise of the burst
// TIMING, and this port's RNG stream and Braids' are independent. That is
// variance, not a defect, and it behaves like variance -- changing only the
// played note by a semitone swings the 20-40 Hz figure between -5.4 and
// -10.0 dB (measured at notes 44/45/46), and a longer window does not
// converge it either (25 s: RMS improved 0.59 -> 0.15 dB while that bin got
// WORSE, -9.1 -> -11.0 dB). The band that actually carries the fundamental
// (160-320 Hz, ~40% of total energy) matches to within 0.3 dB in every
// case, including density-low.

#ifndef PLAITS_DSP_ENGINE2_PARTICLE_BURST_ENGINE_H_
#define PLAITS_DSP_ENGINE2_PARTICLE_BURST_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// The three filters' fixed intervals above the note, in semitones --
// digital_oscillator.cc:2108 (0x600 = 1536 = 12*128), :2114 (0x980 = 2432 =
// 19*128), :2119 (0x790 = 1936 = 15.125*128). MORPH's Chord width scales all
// three together; 1.0 (MORPH at 0.5) is the module exactly.
const float kParticleFilter1Interval = 12.0f;
const float kParticleFilter2Interval = 19.0f;
const float kParticleFilter3Interval = 15.125f;

// Per-filter jitter coefficients, converted from Braids' pitch-128 shift
// amounts (>>17, >>15, >>16) to a divisor on `noise_a`/`noise_b * COLOR-as-
// parameter_[1]` in the SAME pitch-128 units (digital_oscillator.cc:
// 2108,2114,2119). Filter 3 reads `noise_b` (a wider 13-bit slice) where 1
// and 2 read `noise_a` (12 bits) -- kept distinct, not symmetrised.
const float kParticleJitter1Divisor = 131072.0f;  // >> 17, filter 1 (x3 first)
const float kParticleJitter2Divisor = 32768.0f;   // >> 15, filter 2
const float kParticleJitter3Divisor = 65536.0f;   // >> 16, filter 3

// Braids' hardwired resonance (digital_oscillator.cc:2077-2078) -- the module
// gives no control over this at all. Both constants are carried at the value
// Braids' own `static const int32_t` initialisers TRUNCATE to, not at the
// nominal 0.996: `32768 * 0.996` = 32636.928 -> 32636 (that one is folded
// into the coefficient by ResonatorCoefficient in the .cc) and
// `32768 * 0.996 * 0.996` = 32506.35 -> 32506, the feedback loop's other pole
// term.
const float kParticleResonanceSquared = 32506.0f / 32768.0f;

// kParticleNoiseDecay/65536 (digital_oscillator.cc:2076,2129), the module's
// fixed per-48kHz-sample decay of the burst envelope -- about a 12 ms T60.
// MACRO's Decay axis is ApplyMacro on this SAME raw coefficient, stock at
// 0.5; the min/max bounds are a ~4 ms tick and a ~100 ms bloom.
const float kParticleStockDecay = 64763.0f / 65536.0f;
const float kParticleMinDecay = 0.90f;
const float kParticleMaxDecay = 0.9995f;

// 12 * log2(kSampleRate / kCorrectedSampleRate) -- +4.61 cents, the same
// offset every pitched engine in this alt firmware carries (SPEC R6). See
// the PITCH CORRECTION header note for the derivation that folds it into a
// single upfront semitone shift rather than a second frequency domain.
const float kParticlePitchCorrection = 0.046105f;

// Braids' 23-bit trigger test denominator (digital_oscillator.cc:2104,
// `noise & 0x7fffff`) and the 1024 floor added to TIMBRE-as-parameter_[0]
// (digital_oscillator.cc:2085).
const uint32_t kParticleDensityMask = 0x7fffffu;
const float kParticleDensityFloor = 1024.0f;
const float kParticleDensityRange = 32767.0f;

class ParticleBurstEngine : public Engine {
 public:
  ParticleBurstEngine() { }
  ~ParticleBurstEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool fast_fm_capable() const { return true; }
  virtual bool stereo_capable() const { return PLAITS_STEREO_PARTICLE_BURST; }
  virtual void HardSync() { Reset(); }

 private:
  // Per-channel resonator bank state: 0 = mono/left (always runs), 1 = right
  // (stereo only, its own independent RNG draws -- R14).
  float amplitude_[2];
  float y1_state_[2][2];  // [channel][y[n-1], y[n-2]]
  float y2_state_[2][2];
  float y3_state_[2][2];
  float c1_[2];
  float c2_[2];
  float c3_[2];
  float s1_[2];
  float s2_[2];
  float s3_[2];

  DISALLOW_COPY_AND_ASSIGN(ParticleBurstEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_PARTICLE_BURST_ENGINE_H_
