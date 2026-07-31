// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' MORPH model: one knob walking a single oscillator from triangle
// through saw to a narrowing pulse, the whole thing then run into a
// pitch-tracking one-pole low-pass and a tanh(8x) fuzz whose amount is the
// other knob.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderMorph
// (braids/macro_oscillator.cc:66-125) over AnalogOscillator::RenderTriangle
// (:424), RenderSaw (:274) and RenderSquare (:188).
//
// WHICH BRAIDS PARAMETER DRIVES WHAT, with lines. RenderMorph uses NO
// INTERPOLATE_PARAMETER macro at all -- it reads both parameters directly,
// once per block -- so the suffix trap that cost `fold` a rewrite cannot
// arise here. Both readings are still written out, because the model couples
// two behaviours onto one knob and that coupling is the whole design:
//
//   parameter_[0], TIMBRE, at :74/:80/:87 -- and nowhere else. It picks one of
//   three regions and the crossfade inside it, and in the third region it also
//   sets the square's pulse width.
//   parameter_[1], COLOR, at :99 and :107 -- and nowhere else. It does TWO
//   jobs at once: it pulls the low-pass cutoff down from 128 semitones above
//   the note to the note itself (:99), and it raises the fuzz mix from dry to
//   fully wet (:107). One knob, welded.
//
// THE THIRD REGION DOES NOT REACH THE SINE. :90 sets the second oscillator to
// OSC_SHAPE_SINE and :91 then sets `balance = 0`, and Mix(a, b, 0) returns a.
// The sine is rendered into temp_buffer_ and never mixed in -- the top third
// of TIMBRE is a variable-width pulse alone. The port does not render a sine,
// and the header of this engine is the only place that fact is written down.
//
// The three regions, from the integer boundaries at :74/:80/:87:
//   TIMBRE 0    -> 0.3333   triangle crossfading to saw   (balance = p0 * 6)
//   TIMBRE 0.3333 -> 0.6666 saw crossfading to square     (65535 - (p0-10923)*6)
//   TIMBRE 0.6666 -> 1      square narrowing to a pulse   (pw 0.5 -> 0.0117)
// The crossfade weights are continuous across both boundaries (0.99995 -> 1.0
// at each), so the sweep is smooth even though the branch is not.
//
// LINEAGE. Plaits' virtual-analog engine is the neighbour: a variable-shape
// oscillator against a sync square, MORPH sweeping the shape. It shares the
// triangle/saw/square/pulse vocabulary and nothing else. It has no fuzz stage,
// no pitch-tracking filter, and its two waveforms are a detuned PAIR rather
// than a crossfade through a single-oscillator morph. What makes MORPH sound
// like MORPH is the tanh(8x) violent overdrive sitting after a filter that
// closes onto the fundamental, and virtual-analog cannot reach it at all.
//
//   HARMONICS  Fuzz    Braids' COLOR. Filter cutoff falls from note+128 to the
//                      note while the fuzz mix rises from dry to wet, welded
//                      together exactly as the module welds them.
//   TIMBRE     Shape   Braids' TIMBRE: triangle -> saw -> square -> pulse.
//   MORPH      Tone    offsets the cutoff +/-60 semitones from where Fuzz put
//                      it, which is the one thing the module cannot do: it is
//                      the coupling at :99 taken apart. Zero at noon.
//   MACRO      Drive   gain into the overdrive, 0.125x to 8x around Braids'
//                      1.0. The fuzz mix crossfades dry against wet; drive
//                      changes how hard the tanh is hit, which is a different
//                      axis and one Braids welds shut at unity.
//
// RATE. RenderMorph does NOT end in `size -= 2` -- its loop at :114 writes one
// sample per iteration -- so it is a 96 kHz algorithm and its one rate-bearing
// constant, the one-pole coefficient read from lut_svf_cutoff at :105, is
// expressed against 96 kHz (braids/resources/lookup_tables.py:35 sets
// sample_rate = 96000 for that table). The port therefore runs its whole inner
// chain 2x oversampled, at that same 96 kHz, and the coefficient transfers
// verbatim rather than being re-derived. RenderTriangle is additionally 2x
// oversampled inside (:441 and :447 each advance by phase_increment >> 1 and
// the two are box-averaged at :445/:451), so the port's triangle is evaluated
// twice per 96 kHz sub-sample and averaged, exactly as Braids does. Saw and
// square are not oversampled in Braids and are not here.
//
// The fuzz roll-off at :108-113 is a pitch guard, not a rate constant: it
// subtracts (pitch - 80*128) << 4 from the fuzz amount, which is 2048 of 65535
// per semitone, so the fuzz is gone 32 semitones above MIDI 80 at full COLOR
// and 16 above it at noon. Expressed in MIDI notes it transfers unchanged.
//
// TABLES, both verified against their generators by regenerating them and
// differencing every entry:
//   ws_violent_overdrive reproduces at 0 LSB across all 257 entries from
//   scale(tanh(8x)) in braids/resources/waveshapers.py:38,63. It is EMBEDDED
//   because it is read once per sub-sample and a tanh in that loop is not
//   affordable. 86 of its 257 entries sit within 1 LSB of full scale -- every
//   one at |x| >= 0.671875 -- so past that point the curve is a hard clip, not
//   a soft one, and that flattening is part of the sound.
//   lut_svf_cutoff reproduces at 0 LSB (truncating, 1 LSB rounding) from
//   2*sin(pi*min(f/96000, 1/8))*32767 in lookup_tables.py:99-107. It is NOT
//   embedded: it is read once per BLOCK, so the closed form is cheap, and the
//   port evaluates it exactly rather than through Braids' per-semitone linear
//   interpolation. Differenced against Braids' interpolated read at all 32768
//   reachable indices, the closed form is within 0.98% of the coefficient
//   (0.17 semitones of cutoff) at or above a cutoff of MIDI 48, 2.2%
//   (0.38 semitones) at or above MIDI 24 and 4.3% (0.72 semitones) at or above
//   MIDI 12, rising to 9.7% only BELOW MIDI 12, where the stored coefficient is
//   a two-digit integer and its own quantisation dominates the difference. The
//   error is the table's, not the closed form's: it grows as the stored value
//   shrinks toward single digits and the per-semitone linear interpolation
//   spans a wider exponential step.
//
// WHAT THE A/B MEASURES. Thirteen cases in tests/ab.json. Twelve of them hold
// AC RMS to 1.0 dB, pitch to 1.0 cent and spectrum to 1.5 dB, and measure
// within 0.71 dB, 0.6 cents and 1.29 dB of that. The thirteenth, pulse-high,
// is the narrowest pulse two octaves up, where 1.2% of a cycle is under one
// 96 kHz sample and BOTH sides alias: AC RMS +1.05 dB and spectrum 2.22 dB. It
// declares no cents tolerance not because the f0 estimator returns nothing --
// it returns a number on both sides -- but because on that aliased waveform
// each side locks onto a DIFFERENT sub-harmonic of the played 1046.5 Hz: the
// reference reports 149.5 Hz (f0/7) and the port 262.3 Hz (f0/4), a 969-cent
// gap that measures the estimator, not the oscillator. The case is carried
// because it is where the port stops tracking, not because it passes easily.
//
// ONE CASE, guard-band, CARRIES A WIDENED 2.5 dB AC RMS / 1.8 dB SPECTRUM
// TOLERANCE, and the reason is a real Braids behaviour that this port
// deliberately does not reproduce.
//
// AnalogOscillator::Render calls Init() when the shape changes
// (analog_oscillator.cc:75-78), and Init() resets pitch_ to 60 << 7
// (analog_oscillator.h:77) AFTER RenderMorph has already called set_pitch
// (macro_oscillator.cc:70-71). So whichever of the two oscillators changed
// shape renders its first 24-sample block at MIDI 60 rather than the played
// note, and the phase error that leaves never comes back. previous_shape_ is
// zero-initialised to OSC_SHAPE_SAW on the module's file-scope MacroOscillator
// (braids.cc:59), so on a MORPH note the square or the triangle re-Inits and
// the saw does not, and the offset is exactly 24 * (f60 - fnote) / 96000
// cycles -- ZERO at MIDI 60, growing either side of it, and opposite in sign
// above and below. Measured first-hand in the reference renderer, by DFT-ing
// the fundamental of a saw-alone render against a square-alone render at the
// same note and referencing both to MIDI 60: +0.01039 cycles at MIDI 57
// against +0.01041 predicted, +0.03785 at 45 against +0.03791, +0.05159 at 33
// against +0.05166, -0.06534 at 72 against -0.06541, -0.19632 at 84 against
// -0.19622, -0.35043 at 92 against -0.34990 and -0.45839 at 96 against
// -0.45784. Agreement to 1e-4 cycles across four octaves.
//
// The cost, derived then measured. For a 50/50 saw-square mix the level
// follows RMS^2 = 1/12 + 1/4 + R(delta)/2 with R(delta) = 0.5 - 2*delta over
// delta in [0, 0.5], predicting the port reading +0.00 dB at MIDI 60, +0.29 at
// 45, +0.40 at 33, +1.78 at 84 and +3.98 at 92; the dry mix measures +0.01,
// +0.26, +0.33, +1.77 and +4.10. Under a note it is a fraction of a dB, which
// is why twelve cases hold at 1.0 dB. Only guard-band sits far enough from
// MIDI 60 for it to bite, at +2.36 dB -- the fuzz at COLOR 0.8 compressing
// that dry +4.10. Two control cases keep the widening honest: mix-at-60 is the
// same mix and fuzz where the module's offset is zero (-0.01 dB) and
// rolloff-92 is a single shape at guard-band's own note and fuzz (-0.01 dB),
// so only the mix AND the distant note together cost anything.
//
// (Until 2026-07 render_braids_model.cc declared its MacroOscillator as a
// stack LOCAL rather than at file scope, leaving previous_shape_ and
// previous_phase_increment_ indeterminate and producing a note-INDEPENDENT
// ~0.166-cycle offset of its own. Every morph figure measured against that
// renderer is void, including the "1.4 to 1.7 dB on every mixed-shape case"
// reading and the 2.0 dB tolerance it justified. Nothing above rests on it.)
//
// OUT: the fuzzed voice. AUX (mono): the same morph oscillator DRY -- no
// filter, no fuzz -- which is the analog waveform the model is built on and
// the half a Plaits user would otherwise reach for virtual-analog to get. At
// the bottom of Fuzz the two are the same signal, because that is what the
// model is there: COLOR at zero mixes in none of the shaper, so OUT IS the dry
// waveform. In stereo OUT/AUX become L/R and the two sides take opposing
// offsets along the welded Fuzz axis, so one side is cleaner and brighter and
// the other dirtier and darker; measured with a fixed knob, the two channels
// differ at every position of Fuzz including both ends, so the image never
// collapses.
//
// Declared deviations from Braids:
//   - all three shapes run off ONE phase accumulator, so a crossfade mixes two
//     shapes that are exactly in phase. Braids runs two AnalogOscillators and
//     re-Inits whichever one changed shape, which leaves them
//     24 * (f60 - fnote) / 96000 cycles apart (see above). The port does not
//     reproduce that: there is no canonical value to copy, since Braids' own
//     depends on the played note AND on where TIMBRE has been -- crossing a
//     region boundary re-Inits an oscillator and re-rolls it. This is the one
//     deviation the A/B pays for, and only far from MIDI 60: guard-band, at
//     MIDI 92, is the single case whose tolerance is widened for it.
//   - the decimator is a 31-tap Kaiser halfband, not Braids' native 96 kHz
//     output. Measured against the reference renderer's own 127-tap decimator
//     it is within 0.1 dB to 18 kHz, -0.65 dB at 20 kHz and -2.3 dB at 22 kHz,
//     with a stopband under -78 dB above 32 kHz (R7).
//   - saw and square are polyBLEP band-limited; Braids uses its own integer
//     BLEP table. The triangle is naive in both, plus the 2x box average.
//   - a DC blocker sits at the output of each channel. Braids has none, and
//     at the top of TIMBRE its pulse carries up to +0.98 of DC. R1 then
//     applies: an engine with a DC blocker cannot pin its post-blocker peak,
//     so this engine registers negative gains and takes the limiter path.
//   - the pulse width is floored at half a sub-sample increment so it can
//     never reach zero. Braids has no floor at all; this one does not bite
//     until about MIDI 97, where its own 1.2% pulse is already under a 96 kHz
//     sample wide. Floors large enough to make an `else if` edge chain
//     well-posed were measured and rejected: two sub-samples costs 10.9 dB of
//     level against the module at MIDI 96, so the two edge branches are
//     chained instead and both fire in the same sub-sample when they have to.
//   - Braids clips the filter state to int16 and quantises the shaper output
//     to int16 every sample. The port clips the state to +/-1 but holds the
//     shaper output as float, so it does not reproduce that quantisation.
//   - a TRIGGER resets the phase accumulator. Braids does NOT do this for an
//     analog model: MacroOscillator::Strike (macro_oscillator.h:80-82) only
//     sets the DIGITAL oscillator's strike_ flag, and nothing in the MORPH path
//     reads it, so a trigger leaves the analog phase running. The port resets
//     it to match csaw and fold, whose Render does the same. Every A/B case
//     holds the trigger low, so this is unmeasured by tests/ab.json.

#ifndef PLAITS_DSP_ENGINE2_MORPH_ENGINE_H_
#define PLAITS_DSP_ENGINE2_MORPH_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// The port runs its inner chain at Braids' own 96 kHz so the one-pole
// coefficient and the fuzz roll-off transfer verbatim (see RATE above).
const int kMorphOversampling = 2;

// macro_oscillator.cc:74 and :80. The region boundaries are on the raw 15-bit
// parameter, so they are kept there rather than being renormalised.
const float kMorphRegion1End = 10922.0f;
const float kMorphRegion2Start = 10923.0f;
const float kMorphRegion2End = 21845.0f;
const float kMorphRegion3Start = 21846.0f;

// `balance = parameter_[0] * 6` (:79) and `65535 - (parameter_[0] - 10923) * 6`
// (:85), read through Mix()'s 65535 scale.
const float kMorphBalanceSlope = 6.0f / 65535.0f;

// :87 scales the third region by 3 into RenderSquare, which clamps at 32000
// (analog_oscillator.cc:194) and forms `pw = (32768 - parameter_) << 16`
// (:206) -- so the low half of the cycle shrinks from 0.5 to 768/65536.
const float kMorphSquareSlope = 3.0f;
const float kMorphSquareMax = 32000.0f;

// :99 -- `lp_cutoff = pitch_ - (parameter_[1] >> 1) + 128 * 128`, clamped to
// 0..32767, read as a MIDI note by lut_svf_cutoff's index. In notes: the
// cutoff starts 128 semitones above the played note and COLOR pulls it down
// by 32767/256 semitones at the top of the knob, landing on the note itself.
const float kMorphCutoffCeiling = 128.0f;
const float kMorphCutoffSpan = 32767.0f / 256.0f;
const float kMorphCutoffMax = 32767.0f / 128.0f;

// lookup_tables.py:101-103 clamps the table's cutoff at 1/8 of the sample
// rate before taking 2*sin(pi*f). The port shares the 96 kHz rate, so it
// shares the clamp.
const float kMorphCutoffLimit = 0.125f;

// :107-113 -- `fuzz_amount = parameter_[1] << 1`, then above MIDI 80 it loses
// (pitch - 80*128) << 4, which is 128*16 = 2048 of Mix()'s 65535 per semitone.
const float kMorphFuzzGuardNote = 80.0f;
const float kMorphFuzzRollOff = 2048.0f / 65535.0f;

// MORPH takes the cutoff five octaves either side of where Braids' coupling
// put it. Wide on purpose: at the top of Fuzz the module's filter sits ON the
// fundamental, and anything narrower cannot get back out to a bright fuzz.
const float kMorphMaxToneOffset = 60.0f;

// MACRO's drive range around Braids' welded 1.0. Six octaves of gain, because
// the shaper is already near-clipping at unity and the interesting half of
// this control is downward, where the tanh becomes a soft saturation.
const float kMorphMinDrive = 0.125f;
const float kMorphMaxDrive = 8.0f;

// The two stereo channels sit this far either side of the Fuzz knob. The knob
// is remapped into [d, 1-d] first so neither side ever clamps: a clamp would
// make the two channels identical at the ends of the knob and collapse the
// image there, the fixed-point failure csaw documents at kCSawStereoBend.
const float kMorphStereoFuzz = 0.14f;

// 31-tap Kaiser(beta = 7.5) halfband, normalised to unity DC gain. Every even
// tap is zero by construction, so only the centre and the eight odd pairs are
// stored. R9 keeps decimator coefficients per engine rather than shared.
const float kMorphHalfbandCentre = 0.499977286f;
const float kMorphHalfband1 = 0.313391108f;
const float kMorphHalfband3 = -0.092152672f;
const float kMorphHalfband5 = 0.042744897f;
const float kMorphHalfband7 = -0.020375131f;
const float kMorphHalfband9 = 0.008867136f;
const float kMorphHalfband11 = -0.003211545f;
const float kMorphHalfband13 = 0.000826695f;
const float kMorphHalfband15 = -0.000079130f;

// Braids caps its pitch at kHighestNote = 128 * 128 in ComputePhaseIncrement
// (analog_oscillator.cc:47-49), which is 13.29 kHz. That is Braids' own
// ceiling and it is tighter than plaits::kMaxFrequency, so R4 takes it.
const float kMorphMaxNote = 128.0f;

class MorphEngine : public Engine {
 public:
  MorphEngine() { }
  ~MorphEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_MORPH; }

 private:
  inline float Decimate(const float* history) const;

  float phase_;
  float frequency_;

  // The crossfade weights, the pulse width and the two fuzz mixes, all
  // interpolated across the block. Braids steps them once per block instead;
  // at Plaits' block size the two update rates are the same 4 kHz.
  float saw_;
  float square_;
  float pw_;
  float fuzz_;
  float fuzz_aux_;

  // One band-limited oscillator feeds both channels; only the post-oscillator
  // chain differs between them, which is what keeps the stereo branch cheap.
  bool high_;
  float previous_pw_;
  float next_sample_;

  float lp_state_;
  float lp_state_aux_;

  float dc_input_;
  float dc_input_aux_;

  // Two mirrored copies turn each 31-tap decimator window into one contiguous
  // span, avoiding a wrap mask at every tap without changing sample order.
  float history_[64];
  float history_aux_[64];
  int history_position_;

  DISALLOW_COPY_AND_ASSIGN(MorphEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_MORPH_ENGINE_H_
