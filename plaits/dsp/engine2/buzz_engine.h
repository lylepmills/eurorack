// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' BUZZ model: two band-limited pulse-train oscillators, unison-tuned
// and summed, whose shared harmonic content is chosen by reading a table
// instead of computing it.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderBuzz
// (macro_oscillator.cc:329) driving AnalogOscillator::RenderBuzz
// (analog_oscillator.cc:563) twice.
//
// PARAMETER MAPPING, with line numbers, because this model's trap is not the
// fold kind (there is no BEGIN_INTERPOLATE_PARAMETER_1 here to misread) but a
// quieter one: it is very easy to assume the second oscillator gets its own
// independent harmonic axis, when the source says otherwise twice.
//
//   macro_oscillator.cc:333  analog_oscillator_[0].set_parameter(parameter_[0]);
//   macro_oscillator.cc:335  analog_oscillator_[0].set_pitch(pitch_);
//   macro_oscillator.cc:337  analog_oscillator_[1].set_parameter(parameter_[0]);
//   macro_oscillator.cc:339  analog_oscillator_[1].set_pitch(pitch_ + (parameter_[1] >> 8));
//
// Line 337 passes parameter_[0] again -- TIMBRE, not COLOR. Both oscillators
// read the identical harmonic-content parameter; only their PITCH differs.
// parameter_[1] (COLOR) appears exactly once in the whole model, at line 339,
// purely as a pitch offset for oscillator 2, clamped by the `>> 8` to under
// one semitone (32767 >> 8 = 127, /128 = 0.992 st). COLOR never touches
// harmonic content. A plausible-sounding wrong port -- "oscillator 2 gets its
// own COLOR-driven harmonic axis, like a two-band version of FOLD" -- renders
// fine in isolation and is not what the module does; it is closer to what
// this port's MORPH macro adds deliberately (see below) than to what TIMBRE
// and COLOR do on the module.
//
// Inside AnalogOscillator::RenderBuzz (analog_oscillator.cc:563), TIMBRE
// does not touch amplitude or a filter -- it drives which pair of
// WAV_BANDLIMITED_COMB_* wavetable zones the oscillator crossfades between:
//
//   analog_oscillator.cc:568  shifted_pitch = pitch_ + ((32767 - parameter_) >> 1);
//   analog_oscillator.cc:569  crossfade = shifted_pitch << 6;
//   analog_oscillator.cc:570  index = shifted_pitch >> 10;
//   analog_oscillator.cc:574,579  wave_1 = zone[index], wave_2 = zone[index + 1]
//   analog_oscillator.cc:585  *buffer++ = Crossfade(wave_1, wave_2, phase_, crossfade);
//
// Each of the 15 zones (braids/resources/waveforms.py:119-134) is a
// band-limited impulse train -- a periodic Dirichlet kernel,
// sin(pi*i*m)/(m*sin(pi*i)), which is DC plus (m - 1) / 2 cosine harmonics
// at equal weight and a hard cutoff above them -- NOT m harmonics: the
// kernel's m equally-weighted exponentials run from -(m-1)/2 to +(m-1)/2.
// Measured by FFT of the 15 checked-in tables: 128, 128, 128, 128, 128, 128,
// 128, 81, 51, 32, 20, 12, 8, 5, 1 significant harmonics for zones 0..14 --
// matching (m-1)/2 from zone 7 down, and capped at 128 below it by the
// 256-point table's own Nyquist. Each zone is generated for a
// reference pitch 8 semitones apart (`f0 = 440 * 2^((18 + 8*zone - 69)/12)`,
// waveforms.py:120) with m chosen (waveforms.py:126) so that the
// ((m-1)/2)-th harmonic sits just under that zone's own Nyquist (zone 7:
// 81 * 587.3 Hz = 47.6 kHz).
//
// The zone INDEX tracks the played note (`index = shifted_pitch >> 10`, i.e.
// one zone per 8 semitones), so the harmonic count falls as the note rises.
// The zone's reference pitch runs 10-18 semitones ABOVE the note that selects
// it (ref = 18 + 8*zone; played note in [8*zone, 8*zone+8)), so at TIMBRE =
// max the top harmonic lands around 17-27 kHz rather than at Nyquist: the
// fullest stack the zone system gives that pitch, comfortably band-limited
// rather than pushed to the limit. TIMBRE = 0 shifts the lookup up
// by 32767/256 ~= 128 semitones (over ten octaves) regardless of the note
// actually played, which clamps to the top zone (m = 3 -- one harmonic, an
// exact sine) independent of pitch. So TIMBRE sweeps harmonic count, not
// amplitude, and -- unlike a normal wavetable scan -- does so by pretending
// the note is higher than it is, not by picking a different waveform outright.
//
// RELATIONSHIP TO PLAITS' "HARMONIC" ENGINE. No lineage claim here -- unlike
// FOLD, nothing says Harmonic descends from BUZZ -- but it is the closest
// neighbour in the palette, and reaches a related place by a different
// route. Harmonic (family Additive, additive_engine.cc) runs 24 live sine
// partials and shapes each one's amplitude with a continuous
// centroid/slope/bump envelope (additive_engine.cc:67-112): a smoothly
// weighted spectrum. BUZZ's spectrum is a boxcar: every harmonic up to the
// zone's cutoff at equal weight, then silence -- the classic "buzz" timbre,
// not reachable by tilting Harmonic's envelope. This is a difference in the
// generating law, not only the implementation, so BUZZ is not a re-skin of
// its neighbour.
//
// RATE (SPEC R5). analog_oscillator.cc:563-587 contains no `size -= 2` and no
// internal oversampling of its own -- one Crossfade read per loop iteration
// (:585) -- so by R5 it is a 96 kHz algorithm and its rate-dependent constant
// must be re-derived, not transplanted. That constant is the wavetable
// itself: waveforms.py:38 fixes `SAMPLE_RATE = 96000`, and each zone's
// harmonic count is chosen so its top harmonic sits just under a 96 kHz
// Nyquist (48 kHz) AT THAT ZONE'S OWN reference pitch.
//
// What that costs at 48 kHz is NOT the zones' own harmonics. Because the zone
// index tracks the played note, the stack the module actually reads tops out
// around 17-27 kHz whatever you play (see the zone discussion above), and at
// a high note it is well clear of a 24 kHz Nyquist: measured on the reference
// renderer at its native 96 kHz, note 84 / TIMBRE 1 (zones 10 and 11 -- 20
// and 12 harmonics, top partial 20.9 kHz) carries 0.0003% of its energy above
// 24 kHz. The content that does sit up there is the IMAGE of the 256-point
// table read -- the interpolation error around 256 * f0, with its null
// exactly at 256 * f0 -- so the risk is worst at a LOW note, where 256 * f0
// falls between the two Nyquists: at note 48 / TIMBRE 1, 256 * 130.8 Hz =
// 33.5 kHz and 0.49% of the render's energy sits above 24 kHz (0.22% in
// 24-30 kHz, 0.22% in 42-48 kHz, a null at 30-36 kHz). Running the table read
// at 48 kHz would fold that whole image band into the audible range;
// tests/ab.json's `timbre-high` is that case, and it matches to 0.78 dB.
//
// The port runs both oscillators' zone lookup and phase accumulation at 2x
// -- matching the tables' native 96 kHz -- and decimates through the same
// [0.25, 0.5, 0.25] 3-tap halfband `z-filter` uses for the identical
// no-size-minus-2 situation (SPEC R7: -1.4 dB @
// 12 kHz, -6.0 dB @ 24 kHz, null at 48 kHz).
//
// TABLES. All 15 WAV_BANDLIMITED_COMB_* zones (257 entries each) reproduce at
// 0 LSB of deviation from braids/resources.cc against the closed form in
// waveforms.py:119-134 (checked programmatically against every entry, not
// sampled). Embedded rather than evaluated at render time: the closed form is
// a ratio of two sines with a removable singularity at phase 0 needing a
// guard, and R12 -- table reads are the cheapest thing this hardware does,
// evaluating that ratio in the inner loop is not -- argues the same way fold
// did for its shaper curves.
//
// Both tables read per oscillator are bounded to +/-32767/32768 by
// construction (waveforms.py's `scale()` centers each zone to zero mean and
// normalizes its peak into that range; the widest entry across all 15 zones,
// checked programmatically, is exactly 32767). A linear read, a crossfade
// between two such tables, the [0.25, 0.5, 0.25] decimation, and the final
// two-oscillator sum or difference are all convex combinations, so the
// output is bounded to that same peak with no DC blocker and no feedback:
// R1 permits a positive gain.
//
// OUT/AUX. Mono: OUT is Braids' exact 0.5*(oscillator 1 + oscillator 2) sum;
// AUX is the complementary 0.5*(oscillator 1 - oscillator 2) difference,
// silent at HARMONICS = 0 WITH MORPH at noon (no detune and no zone spread,
// so the two voices are identical and there is nothing to difference) exactly
// as SPEC's `sub-oscillator` AUX is silent at its own detent -- declared, not
// a defect. Off noon, MORPH alone separates the voices and AUX carries the
// zone difference at any HARMONICS setting. Stereo (Pattern B,
// PLAITS_STEREO_BUZZ already gated in stereo_config.h): OUT is oscillator 1
// alone and AUX is oscillator 2 alone, so engaging stereo turns the mono
// sum's partial cancellation into a real two-voice width instead of
// discarding it.
//
// THE FOURTH MACRO, and the trap made useful on purpose. MACRO widens
// HARMONICS' detune reach past the module's own sub-semitone ceiling, with a
// detent at noon that reproduces the ceiling to within a third of a cent
// (kBuzzStockDetuneSemitones = 32767/32768 st: the DE-QUANTIZED form of
// `(parameter_[1] >> 8) / 128`, whose integer shift actually lands on
// 127/128 = 0.9922 st at parameter_[1] = 32767 -- see the declared deviation
// below). MORPH gives oscillator 2 the independent
// harmonic-content axis the parameter-index trap above could have been
// mistaken for -- zero at noon (both oscillators read the same TIMBRE-chosen
// zone, exactly Braids), a bipolar zone offset away from it. Braids' one
// TIMBRE knob cannot separate the two voices' harmonic content at all; MORPH
// is that missing control made real instead of accidental.
//
// Declared deviations from Braids:
//   - the decimator is the [0.25, 0.5, 0.25] 3-tap halfband. Braids has none
//     at all: RenderBuzz already runs AT its 96 kHz output rate and never
//     itself decimates anything; the decimator exists only because this port
//     reaches that same internal rate by running 2x from a 48 kHz output.
//   - the zone crossfade and both oscillators' pitch are interpolated per
//     sample; Braids recomputes shifted_pitch, the zone pair and the
//     crossfade weight once per 24-sample block and steps pitch the same
//     way. SPEC's `sub-oscillator` write-up calls the equivalent block-rate
//     zipper "a Braids bug, not character"; the same reasoning applies here.
//   - MORPH (zone spread) and MACRO's detune ceiling above the module's own
//     ~1 semitone have no Braids equivalent at all; both are declared new
//     axes, not reproductions, and are not verified against hardware.
//   - the detune is de-quantized. Braids' offset is the INTEGER
//     `parameter_[1] >> 8` in 1/128-semitone units, so its travel is a
//     staircase of 128 steps ending at 127/128 = 99.22 cents; the port's is
//     continuous and ends at 32767/32768 = 100.00 cents. Measured on the
//     `color-full-detune` renders (single-sinusoid fit of both partials):
//     Braids 99.32 cents, port 99.65 cents -- 0.33 cents wide, below the
//     A/B's pitch metric and inaudible, and the same de-zippering argument as
//     the per-sample interpolation above.
//   - the trigger resets both phases to zero (Plaits convention for
//     predictable onsets). Braids never resets BUZZ's phase: its sync input
//     is unused here and MacroOscillator::Strike (macro_oscillator.h:80)
//     reaches only the digital oscillator bank. Invisible to the A/B, which
//     never triggers.

#ifndef PLAITS_DSP_ENGINE2_BUZZ_ENGINE_H_
#define PLAITS_DSP_ENGINE2_BUZZ_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kBuzzNumZones = 15;

// waveforms.py:120: `f0 = 440 * 2^((18 + 8*zone - 69) / 12)` -- 8 semitones
// of reference pitch per zone.
const float kBuzzZoneSemitones = 8.0f;

// analog_oscillator.cc:568: `(32767 - parameter_) >> 1`, continuous and
// converted from 1/128-semitone pitch units to semitones: 32767 / (2 * 128).
const float kBuzzShiftSemitones = 32767.0f / 256.0f;

// analog_oscillator.h's kHighestNote (128 * 128, analog_oscillator.cc:42) and
// the pitch_ >= 0 clamp (analog_oscillator.cc:82-86) both apply to pitch_
// BEFORE RenderBuzz reads it for zone selection -- but not to the phase
// increment, which analog_oscillator.cc:80 already computed from the
// unclamped value. The port mirrors this: clamp only the zone-selection
// input, never the playback pitch (that clamping is NoteToFrequency's own,
// R6).
const float kBuzzZoneSelectMaxNote = 128.0f;

// analog_oscillator.cc:339: `parameter_[1] >> 8`, continuous and converted to
// semitones: 32767 / (256 * 128) = 32767 / 32768. The module's own detune
// ceiling -- just under one semitone -- and the value MACRO's detent must
// reproduce exactly.
const float kBuzzStockDetuneSemitones = 32767.0f / 32768.0f;

// MACRO's new ceiling: a full octave apart at MACRO = 1, unison at MACRO = 0.
const float kBuzzMacroMaxDetuneSemitones = 12.0f;

// MORPH's new axis: how far oscillator 2's zone can diverge from
// oscillator 1's, in zones (8 semitones of reference pitch each).
const float kBuzzMaxSpreadZones = 4.0f;

class BuzzEngine : public Engine {
 public:
  BuzzEngine() { }
  ~BuzzEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_BUZZ; }

 private:
  float phase0_;
  float phase1_;
  float frequency0_;
  float frequency1_;
  float zone_position0_;
  float zone_position1_;

  // [0.25, 0.5, 0.25] decimator history: the last sub-sample of the previous
  // output period, per oscillator.
  float osc0_decimator_;
  float osc1_decimator_;

  DISALLOW_COPY_AND_ASSIGN(BuzzEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_BUZZ_ENGINE_H_
