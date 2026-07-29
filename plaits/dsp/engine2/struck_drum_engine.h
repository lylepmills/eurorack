// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' DRUM: six fixed partials -- two of them sharing the fundamental --
// struck once per trigger, blending an always-present pure tone against a
// pair of partials that are ring-modulated by lowpass-filtered noise rather
// than played straight.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderStruckDrum
// (braids/digital_oscillator.cc:989-1085). The partial data
// (kDrumPartials/kDrumPartialAmplitude/kDrumPartialDecayLong/Short,
// digital_oscillator.cc:841-855) is hand-authored directly in that file, the
// same as its sibling kBellPartials -- no generator in braids/resources/*.py,
// so it is embedded verbatim rather than "reproduced".
//
// LINEAGE. Plaits' modal-resonator (plaits/dsp/physical_modelling/
// resonator.cc) excites a bank of state-variable resonators with noise or a
// click; STRUCTURE bends a continuous stretched-harmonic series, BRIGHTNESS/
// DAMPING shape one geometric per-mode Q falloff, and the noise excitation
// passes THROUGH the resonators before it is heard at all. DRUM is not a
// resonator and its noise never passes through anything resonant: two of its
// six partials are plain sine oscillators that get MULTIPLIED (ring
// modulated) by a noise signal lowpass-filtered on its OWN separate three-
// stage one-pole chain (:1030-1052), so the noise's spectral colour and the
// partials' pitch are two independent knobs on the same product, not one
// signal exciting the other. modal-resonator cannot produce that -- its
// noise IS the excitation, there is no way to route noise around the
// resonator bank and multiply it back in afterward. Conversely modal-
// resonator's continuous mode count, stretch and per-mode Q are all
// unreachable from DRUM's fixed six-partial table. Struck bell's header
// documents the same "genuinely different instruments in the same
// territory" relationship against the same Plaits neighbour; DRUM's version
// of the difference is the ring-modulated-noise mechanism, not the fixed
// voicing (DRUM's voicing is not even the point -- see PARAMETER MAPPING,
// nothing detunes it).
//
// PARAMETER MAPPING, with the line it was read from.
//   parameter_[0] (TIMBRE) sets the decay, and is IDENTICAL in structure to
//   struck bell's: lines 1004-1014, `if (parameter_[0] < 32000)` (32000/32767
//   = 0.9766 of full scale) computes `balance = (32767-parameter_[0])>>8`,
//   `balance*balance>>7`, then blends kDrumPartialDecayLong/Short by it and
//   multiplies each partial's amplitude by the result once per call. Above
//   the threshold the update is skipped outright: a droning drum that never
//   loses energy. The port's `timbre` macro is the direct map.
//   parameter_[1] (COLOR) drives THREE things at once, none of them the
//   partials' pitch (contrast bell, where COLOR detunes):
//     (a) the noise lowpass's cutoff, line 1023:
//         `cutoff = (pitch_ - 12*128) + (parameter_[1] >> 2)`, table-read at
//         line 1029 via `Interpolate824(lut_svf_cutoff, cutoff << 16)`.
//     (b) `harmonics_gain`, line 1033: `parameter_[1] < 12888 ?
//         parameter_[1]+4096 : 16384`, a Q14 gain (0.25 floor, 1.0 ceiling)
//         on the SIX-partial sum mixed into the output at line 1070.
//     (c) `noise_mode_gain`, lines 1034-1035: zero below COLOR's midpoint,
//         then `(parameter_[1]-16384)*12888>>14` above it -- the crossfade
//         between two DIFFERENT noise-ring-modulated partials, mixed at
//         lines 1068-1069 against the constant 12288 (not 12888 -- the two
//         numbers are different and both appear in the source; see the
//         MIX WEIGHTS note below).
//   The port's `harmonics` macro is the direct map (matches the fold /
//   particle-burst / struck-bell convention: parameter_[1] = COLOR =
//   HARMONICS), driving all three exactly as Braids couples them -- COLOR is
//   one knob doing three jobs in the source, and staying coupled here is
//   what makes an ab.json case at COLOR 0 and 1 actually test the module.
//   Neither knob touches kDrumPartials (the six ratios) or
//   kDrumPartialAmplitude (the six strike levels) at all -- both are fixed,
//   unlike bell's amplitudes, which are also fixed but at least gets a
//   COLOR-driven detune. DRUM has no parameter-driven pitch motion
//   whatsoever; see THE FOURTH MACRO for what that leaves MORPH free to do.
//   Strike: `strike_` (set by DigitalOscillator::Strike() on the module's
//   trigger rising edge) sets every partial's TARGET amplitude to
//   kDrumPartialAmplitude (:997) -- but, unlike bell, only resets phase
//   CONDITIONALLY: `bool reset_phase = partial_amplitude[0] < 1024`
//   (:995, 1024/65536 = 0.0156 of full scale). A drum re-struck while
//   partial 0 (the loudest, longest-ringing partial) is still audible keeps
//   its current phase; only a genuinely silent drum gets phase reset to a
//   quarter cycle (:998-999, `1L<<30`). This is a real difference from
//   bell, which resets phase unconditionally on every strike (digital_
//   oscillator.cc:874) -- copying bell's unconditional reset here would be
//   the kind of "looks like the sibling, isn't" mistake this port exists to
//   avoid; DRUM's own conditional is reproduced as written.
//
// MIX WEIGHTS. Lines 1068-1069 use two DIFFERENT constants that are one
// digit apart -- `12288` in the mix (`noise_mode_1 * (12288 -
// noise_mode_gain) >> 14`) against `12888` in the gain's OWN scale factor
// (line 1035, `noise_mode_gain * 12888 >> 14`). They are not a typo for each
// other: `noise_mode_gain` maxes at `(32767-16384)*12888>>14 = 12887`
// (verified programmatically), so `12288 - noise_mode_gain` at COLOR's top
// end is `12288 - 12887 = -599` -- the first noise-mode voice's mix weight
// goes slightly NEGATIVE at the very top of COLOR's travel. Both constants
// are carried exactly as written rather than "fixed" to match.
//
// RATE. The sample loop (:1039-1076) is the same idiom as bell's:
// `while (size--) { ...one partial-sum + noise-chain step...;
//  *buf++ = (out+prev)>>1; *buf++ = out; size--; prev = out; }` -- SPEC R5's
// `size -= 2` pattern, confirmed independently by
// `partial_phase_increment[i] = ComputePhaseIncrement(partial_pitch) << 1`
// (:1019, unconditional, every partial, every call -- DRUM has no analogue
// of bell's 3-per-call retune stagger to begin with, so there is no
// "retunes every block" deviation to declare here). The additive recursion
// and the noise-lowpass recursion both run at 48 kHz; this port runs both at
// Plaits' native 48 kHz using `plaits::NoteToFrequency`, picking up R6's
// correction the same way every direct-oscillator engine in this tree does.
//
// The per-call TIMBRE/COLOR-derived values (decay multiplier, noise cutoff
// coefficient, harmonics/noise-mode gains) are a separate rate question,
// exactly as struck-bell's header explains: on real hardware one call is
// ALWAYS kBlockSize = 24 samples at 96 kHz (braids.cc:57,276) = 250 us of
// wall-clock time, and Plaits' own kBlockSize is 12 samples at 48 kHz
// (plaits/dsp/dsp.h:51) -- the SAME 250 us. `size == 12` therefore needs no
// correction; a larger caller block takes the corresponding number of whole
// integer-truncating decay steps, copying bell's kBellBlockSamples pattern as
// kStruckDrumBlockSamples below.
//
// TABLE VERIFICATION. `lut_svf_cutoff` (braids/resources.cc:114-224, 257
// uint16 entries) reproduces from braids/resources/lookup_tables.py:98-101 --
// `f = 2*sin(pi*min(440*2^((i-69)/12)/96000, 1/8))`, truncated toward zero
// (Python's `int()`; all values here are positive so this is floor) -- at
// 0 LSB across the full table (verified programmatically). Only entries
// 0-128 are ever addressed: `Interpolate824` reads `cutoff<<16` where
// `cutoff` is clamped to [0,32767], so the top-8-bit index it extracts
// (`(cutoff<<16)>>24 = cutoff>>8`) tops out at 127, needing index 128 for
// its final interpolation step. This port embeds exactly that reachable
// slice (129 entries, kStruckDrumSvfCutoffTable below) rather than the full
// 257, and reads it with the SAME integer index/fraction split
// `Interpolate824` uses (`cutoff>>8` / `(cutoff&0xff)/256`), including that
// function's own truncation of the interpolated result back to an integer,
// rather than evaluating `2*sin(pi*f)` in float: the table is read once per
// call, not once per sample, so embedding costs nothing, and per the
// particle-burst precedent (see its header, TABLE VERIFICATION), a table
// whose own generator truncates should be READ truncated rather than
// re-derived exact. Here the coefficient is not merely a brightness control
// -- it sets the size of the noise chain's DC offset (see NOISE-CHAIN
// TRUNCATION below), so it is kept at Braids' raw Q15 integer end to end.
//
// Everything ELSE Braids reads from parameter_[0]/parameter_[1] in this
// function (the TIMBRE decay balance, COLOR's harmonics/noise-mode gains) is
// likewise kept at Braids' exact integer truncation, `floorf(x / 2^n)` on
// non-negative operands standing in for the arithmetic right shift -- the
// same choice struck-bell's header explains and for the same reason: these
// are computed once per BLOCK, so truncating costs nothing and evaluating
// the closed form in float would only add drift no one asked for.
//
// THE AMPLITUDE STATE ITSELF IS ALSO INTEGER-TRUNCATED, and this one is not
// a fraction-of-an-LSB nicety -- it changes when a partial goes silent.
// `partial_amplitude[i] = partial_amplitude[i] * decay >> 16`
// (digital_oscillator.cc:1011-1012) truncates the STATE, not just the decay
// coefficient: once a partial's raw amplitude (kDrumPartialAmplitude scale,
// i.e. up to ~16986, NOT normalised) gets small, `amp * decay16 >> 16`
// rounds DOWN by roughly one whole unit per block regardless of how close
// `decay` is to 65536, so a small partial hits an abrupt, exact zero within
// a couple hundred milliseconds rather than fading asymptotically forever.
// A first version of this port applied the decay multiplier in continuous
// float (`amplitude_[i] *= multiplier`), which never truncates and so never
// zeroes out -- partials 1-5 (all far smaller than partial 0's 16986) rang
// on in this port long after Braids' own integer truncation had silenced
// them. Measured at COLOR 0 (where the noise-mode and harmonics terms these
// partials feed are weighted most heavily relative to the direct tone): AC
// RMS +2.45 dB overall, and the per-10 ms envelope showed the gap GROWING
// through the decay -- +0.06 dB at the strike, +1.4 dB by 90 ms, +4.2 dB by
// 480 ms -- exactly the shape of "my copy keeps something audible that
// Braids has already zeroed", not a level or rate error. The fix keeps
// `amplitude_` in Braids' raw (un-normalised) integer scale and truncates it
// every block, `floorf(amplitude_[i] * multiplier)`, dividing by 65536.0f
// only at the point of use in the per-sample partial read; see Render().
// That fix brought every case inside 1 dB; the rest of the gap belonged to a
// SECOND truncated state, in the noise filter -- see the next note, which
// closed it to 0.04 dB.
//
// NOISE-CHAIN TRUNCATION: THE FILTER STATE IS INTEGER TOO, AND ITS FLOOR IS
// A DC OFFSET, NOT A ROUNDING NICETY. Each stage of the noise lowpass is
// `state += (input - state) * f >> 15` (:1050-1052). The arithmetic right
// shift FLOORS, and unlike the amplitude update above the operand here is
// signed and zero-mean, so the increment is biased down by half a unit on
// average and the stage does not settle at its input's mean -- it settles
// about `16384 / f` raw units BELOW it, three stages compounding. At COLOR 0
// and note 45 the coefficient is only ~45/32768, so lp2 sits at a measured
// mean of roughly -1095 of +-32768 full scale (standard deviation ~136): at
// that setting Braids' "filtered noise" is overwhelmingly a DC term, and the
// ring modulation of partials 1 and 3 by it is mostly a phase-INVERTED copy
// of those partials rather than noise. Partial 1 shares the fundamental
// (kDrumPartials[1] = 0), so that inverted copy partially CANCELS the drum's
// fundamental -- Braids is quieter than the same algorithm run in continuous
// float, at every COLOR setting where `12288 - noise_mode_gain` is still
// large.
//
// Running the chain in float, which never floors, leaves lp2 zero-mean and
// so drops the cancellation entirely. Measured against the reference with a
// float chain: AC RMS +0.04 to +0.86 dB, every one of the six A/B cases
// reading HOT, and the COLOR-0 case's excess GREW with window length
// (+1.61 dB over two hits, +2.32 dB over eight) because the offset takes
// ~45 ms to establish and then persists, unreset, across every later hit.
// That growth is easy to misread as two independent RNG streams drifting
// apart -- it is not; it is deterministic, and reproducing the floor in
// Braids' own raw integer scale (NoisePoleStep in the .cc, the state carried
// un-normalised exactly as `amplitude_` is) collapses the whole suite to
// +0.01 to +0.04 dB, the eight-hit COLOR-0 window included. This is the
// same lesson as THE AMPLITUDE STATE, arrived at from the opposite
// direction: there the floor forced an exact zero, here it forces an exact
// offset.
//
// wav_sine is NOT a unit-amplitude, zero-mean -cos(2*pi*phase) -- fitted
// programmatically against braids/resources.cc's 256-entry table (:1461 on),
// wav_sine[i] = 32638 * -cos(2*pi*i/256) + 127 (max residual 1 LSB, e.g.
// entry 0 = 32638*-1+127 = -32511 against the table's -32512). Two real
// deviations from unity: the table runs 20*log10(32638/32768) = -0.0343 dB
// quiet, and carries a +127/32768 = +0.00388 DC pedestal on every sample.
// Both are reproduced in BraidsSine() (kBraidsSineAmplitude/
// kBraidsSineDcOffset in the .cc), on top of the quarter-cycle offset that
// helper already carried (copied from fold_engine.cc). struck_bell_engine.cc
// carries the identical scale/pedestal on its own BraidsSine() (its header
// records the same fit and a comparable measured win in its own, purely
// linear, six-partial sum), which is corroborating evidence that the win
// below is the amplitude term doing the work in both engines, not something
// specific to drum's ring-modulated path. Only the table's own 1-LSB
// read-vs-formula residual is not reproduced, the same class of deviation
// already declared below for the per-sample output-mix truncations.
//
// This partial feeds TWO different paths here, unlike bell's purely linear
// one, so the fix could not be assumed inert and had to be measured:
// partial[0] and the six-partial harmonics_sum are LINEAR (summed directly
// into the output, Render():291,295), but partial[1] and partial[3] are
// MULTIPLIED by the filtered-noise state lp2_ (ring modulation,
// Render():291-293) -- exactly the kind of nonlinear interaction where a DC
// pedestal in one factor can leak a scaled copy of the OTHER factor's own
// spectrum into the output. Measured effect of adding both the amplitude
// scale and the DC pedestal together: full-band AC RMS improved by a
// consistent 0.03-0.04 dB in every one of the six A/B cases -- moving the
// port from a uniform "reads hot" bias (+0.01 to +0.04 dB) to within +-0.02
// dB of the reference (see A/B NOTES) -- which matches the amplitude term's
// predicted -0.0345 dB alone to within the harness's 0.01 dB rounding.
// Energy-weighted spectrum and the tracked pitch case moved by <=0.01 dB /
// 0 cents, so the ring-modulated DC-pedestal leak this port worried about is
// not separately measurable at this harness's precision. Net: KEPT, on the
// evidence of the amplitude term alone -- the DC pedestal rides along
// because it costs nothing to also get right, not because it moved a
// number by itself.
//
// THE FOURTH MACRO.
//   MORPH  Spread        DRUM's six partial ratios (kDrumPartials, in
//                         semitones: 0, 0, 8.133, 13.648, 14.422, 24.0) are
//                         completely fixed -- no Braids parameter reaches
//                         them, the mirror image of bell's COLOR-driven
//                         detune. This macro scales all six ratios by
//                         `2 * MORPH`: 0 collapses every partial onto the
//                         fundamental (all six partials in unison -- the two
//                         that already share the fundamental do not move,
//                         since their own ratio is already zero), 0.5 is
//                         Braids' own voicing exactly, 1.0 doubles the
//                         spread. Same idiom as particle-burst's Chord width
//                         (its three filter offsets are equally fixed in the
//                         source), applied to DRUM's six because that is the
//                         one axis the module hardwires and the one thing
//                         modal-resonator's continuous, single-curve
//                         STRUCTURE stretch cannot reach either -- STRUCTURE
//                         reshapes ONE formula's stretch factor, not six
//                         independently-fixed points scaled together.
//   MACRO  Decay spread  Copied directly from struck-bell's own MACRO (see
//                         its header, THE FOURTH MACRO): TIMBRE can only
//                         slide all six partials together between
//                         kDrumPartialDecayLong and kDrumPartialDecayShort;
//                         it cannot change how far they diverge from each
//                         other, and that divergence is real -- partial 5
//                         (the +24 st partial) decays far faster than 0-4 in
//                         BOTH tables (decayShort[5] = 62312 against
//                         64715..65083 for partials 0-4, and decayLong[5] =
//                         65516 against 65531..65533). At stock (0.5) every
//                         partial's per-call multiplier is pulled toward or
//                         away from the six-partial mean by exactly 1x --
//                         Braids' own spread, untouched. At 0 every partial
//                         decays at one uniform rate; at 1 the spread is 4x
//                         Braids' own. Neither the module nor modal-
//                         resonator's `q_loss` (one fixed geometric curve
//                         set by BRIGHTNESS/DAMPING, not a variable spread
//                         around an existing fixed voicing) can do this.
//                         Reused rather than reinvented because it is
//                         exactly the same shape of gap in DRUM's own
//                         parameter space that it was in bell's.
//
// TRIGGER_UNPATCHED. Braids' drum, like bell, has no concept of "unpatched"
// -- the hardware jack is struck or it is not. This port follows the same
// house convention struck-bell's header documents (matching modal_engine.cc
// and hi_hat_engine.cc's `sustain` treatment too): on TRIGGER_UNPATCHED the
// engine self-strikes exactly once, on the first Render() call after
// Reset(), and decay is suppressed for as long as the trigger stays
// unpatched -- the same suppression TIMBRE's own drone threshold applies.
// Not covered by tests/ab.json (every case there drives triggerHz > 0,
// which never sets TRIGGER_UNPATCHED; see render_model.cc:91-99).
//
// alreadyEnveloped is declared true in plaits-engine.json's postProcessing:
// the amplitude decay above is the note's whole loudness envelope, the same
// reasoning struck-bell's and modal-resonator's own metadata follow.
//
// OUT: the six-partial mix (partials[0] direct + the two noise-ring-
// modulated partials, crossfaded by COLOR + the six-partial sum at
// `harmonics_gain`), one computed sample per output sample at the native
// 48 kHz rate. It is NOT bit-identical to Braids -- the noise stream is an
// independent draw (see Declared deviations) -- but it tracks the module's
// level and spectrum to within the figures in A/B NOTES below. Mono, or the
// left channel in stereo. AUX (mono): the noise-ring-
// modulated component ALONE (the two mixed terms, without partials[0] or
// the harmonics sum) -- the part of this model a plain resonator-bank
// neighbour cannot make this way (see LINEAGE), cheap because it reuses the
// same per-sample values OUT's loop already computed.
//
// In stereo, AUX drops the noise-alone tap for a second, independently-
// filtered right channel: unlike bell (which has no noise to decorrelate
// and so detunes pitch for stereo width instead), DRUM's noise chain is its
// own R14 opportunity -- the tonal partials (deterministic sines) stay
// shared/centred between channels, and the RIGHT channel draws its own
// noise samples through its own three-stage lowpass (its own lp0/lp1/lp2
// state), the same "genuinely decorrelated, not a panned copy"
// independence particle-burst's header documents. The right channel's
// draws happen only inside the stereo branch, so turning stereo on never
// perturbs the mono/left channel's noise SEQUENCE (R14). A small,
// ungrounded (Braids has no stereo output to derive it from) opposing
// offset on the noise-mode crossfade point additionally separates the two
// channels' COLOR balance slightly, the same idiom fold_engine.h's
// kFoldStereoBlend uses -- being opposing, it shifts the LEFT channel's
// crossfade by -kStruckDrumStereoBlend as well, so the left channel in
// stereo is the Braids-faithful mono render with that one offset applied,
// not a byte-for-byte copy of it. The mono render (the one tests/ab.json
// compares) carries no offset at all.
//
// A/B NOTES. Full-band AC RMS is -0.02 to +0.01 dB across all six cases
// (before the wav_sine amplitude/pedestal fix above: +0.01 to +0.04 dB,
// every case reading uniformly hot), energy-weighted spectrum 0.05-0.17 dB.
// The band carrying the fundamental matches within 0.2 dB in every case
// (80-160 Hz at note 45, where it holds 64-90% of the energy; 320-640 Hz at
// note 72, where it holds 75%). What is left is confined to the sparse top
// bands, where the two renders disagree in BOTH directions -- decay-fast and
// high-register read up to +4.3 and +9.2 dB there, stock-mid and
// color-bright up to -3.7 and -3.6 dB. Neither direction is a level error in
// the algorithm: those bands hold 0.0-0.3% of the energy (one exception:
// high-register's 1280-2560 Hz holds 4.3% and still reads -1.37 dB, present
// before this fix too and unchanged by it), and they are where a native-
// 48 kHz render and a 96 kHz render halfband-decimated to 48 kHz
// (render_braids_model.cc's 127-tap windowed-sinc) are least alike, since
// the reference's content up there is decimator residue and the port's is
// the sine bank's own numerical floor. The one band above 640 Hz that
// carries real energy is high-register's 640-1280 Hz (16.6%), and it
// matches to -0.13 dB. The same pattern is in fold's, particle-burst's and
// struck-bell's own A/B tables. See tests/ab.json for the cents-tolerance
// note.
//
// Declared deviations from Braids:
//   - the intra-block linear "fade" ramp (:1037-1038,1059-1060, smoothing
//     each partial's amplitude from its old value to the new target across
//     the CURRENT call rather than stepping instantly) is not reproduced --
//     struck-bell's port makes the identical simplification for the
//     identical reason: one call is 250 us, an instant step at a block
//     boundary that short is not an audible click, and reproducing the ramp
//     would need six more per-call state variables for no measurable
//     benefit.
//   - the 2-tap 96 kHz reconstruction (average of the current and previous
//     48 kHz-computed sample, :1073) is not reproduced; at a native 48 kHz
//     there is a genuine computed sample every sample (same non-
//     reproduction fold, particle-burst and struck-bell all declare for
//     their own 48 kHz-inner functions).
//   - partial reads use `plaits::Sine()` through `BraidsSine()` rather than
//     Braids' own 256-entry `wav_sine` table read through `Interpolate824`;
//     BraidsSine() DOES reproduce the table's amplitude scale and DC
//     pedestal exactly (see the wav_sine note above), so what remains
//     declared here is only the table's own 1-LSB fit residual.
//   - the noise source is `stmlib::Random::GetSample()`, the SAME generator
//     Braids' own `Random::GetSample()` wraps (both are stmlib), but an
//     independent stream -- no attempt is made to reproduce Braids' exact
//     noise SEQUENCE, only its statistics through the same lowpass chain.
//     The chain's own integer truncation IS reproduced (see NOISE-CHAIN
//     TRUNCATION), which is what makes the statistics match: the DC offset
//     that truncation produces is deterministic, not part of the sequence.
//   - the per-SAMPLE integer truncations in the output mix (`partial *
//     amplitude >> 16`, `partials[1] * lp_state_2 >> 8`, the two `>> 14`
//     mix shifts, :1058-1070) are not reproduced. Unlike the amplitude and
//     noise-chain floors these truncate a value that is consumed and
//     discarded within the sample, never fed back into state, so they can
//     only add a sub-LSB dither rather than accumulate into an offset.

#ifndef PLAITS_DSP_ENGINE2_STRUCK_DRUM_ENGINE_H_
#define PLAITS_DSP_ENGINE2_STRUCK_DRUM_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const size_t kNumStruckDrumPartials = 6;

// digital_oscillator.cc:841-843. Partial ratios, in Braids' 1/128-semitone
// pitch units -- converted to semitones with /128.0f below. Partials 0 and 1
// share the fundamental (both 0); MORPH's Spread (THE FOURTH MACRO) scales
// all six together and leaves those two pinned to the fundamental regardless.
const int16_t kDrumPartials[kNumStruckDrumPartials] = {
  0, 0, 1041, 1747, 1846, 3072
};

// digital_oscillator.cc:845-846. Initial per-partial strike amplitude, in
// the SAME units `partial_amplitude[i]` is carried in at runtime -- NOT
// normalized to a Q15/Q16 full scale; see PARAMETER MAPPING and the >>16
// scale-down at digital_oscillator.cc:1061 for the derivation of the
// /65536.0f used at point of use below.
const int16_t kDrumPartialAmplitude[kNumStruckDrumPartials] = {
  16986, 2654, 3981, 5308, 3981, 2985
};

// digital_oscillator.cc:849-850 and :853-854. Per-partial per-BLOCK decay
// multiplier, Q16 (65536 = no decay at all). TIMBRE quadratically blends
// between these two tables; MACRO's Decay spread (THE FOURTH MACRO) pulls
// each partial's multiplier toward or away from their six-partial mean.
const uint16_t kDrumPartialDecayLong[kNumStruckDrumPartials] = {
  65533, 65531, 65531, 65531, 65531, 65516
};
const uint16_t kDrumPartialDecayShort[kNumStruckDrumPartials] = {
  65083, 64715, 64715, 64715, 64715, 62312
};

// digital_oscillator.cc:1004, the literal threshold on Braids' 0..32767
// parameter_[0] range -- above it the decay update is skipped outright
// ("droning" drum). 32000.0f / 32767.0f = 0.9766 of full TIMBRE travel.
const float kStruckDrumDroneThreshold = 32000.0f;

// digital_oscillator.cc:995, `partial_amplitude[0] < 1024` -- the threshold
// below which a re-strike resets phase (see PARAMETER MAPPING, Strike), in
// the SAME raw (un-normalised) units as kDrumPartialAmplitude and the
// `amplitude_` state below -- see THE AMPLITUDE STATE IS ALSO INTEGER-
// TRUNCATED above.
const float kStruckDrumResetPhaseThreshold = 1024.0f;

// digital_oscillator.cc:998-999, `(1L << 30)` of a 32-bit phase accumulator
// -- a quarter cycle, not phase zero (BraidsSine(0.25) = 0, so a reset
// partial starts silent rather than at a value discontinuity).
const float kStruckDrumStrikePhase = 0.25f;

// 12 * log2(kSampleRate / kCorrectedSampleRate) -- +4.61 cents, the same
// offset every table-indexed pitch computation in this alt firmware carries
// (SPEC R6; see particle_burst_engine.h's identical kParticlePitchCorrection
// for the derivation). Applied ONLY to the noise-cutoff table lookup below,
// which does not go through `NoteToFrequency` and so does not pick the
// correction up automatically the way the oscillator partials do.
const float kStruckDrumPitchCorrection = 0.046105f;

// One Braids call to RenderStruckDrum is ALWAYS kBlockSize = 24 samples at
// 96 kHz (braids.cc:57,276) = 250 us of wall-clock time, which the decay
// multiplier above is calibrated to. Plaits' own kBlockSize is 12 samples at
// 48 kHz (plaits/dsp/dsp.h:51) -- the SAME 250 us -- so `size == 12` needs
// no correction; larger calls take one integer-truncating decay step per
// elapsed 12 samples. Copied from struck_bell_engine.h's identical
// kBellBlockSamples treatment.
const float kStruckDrumBlockSamples = 12.0f;

// braids/resources.cc:114-224, entries 0-128 of lut_svf_cutoff (see TABLE
// VERIFICATION above for why only this slice is reachable and why it is
// embedded rather than re-derived; the generator is
// braids/resources/lookup_tables.py:98-101). Q15 one-pole coefficient
// (32768 = unity gain, i.e. no filtering at all).
const uint16_t kStruckDrumSvfCutoffTable[129] = {
     17,    18,    19,    20,    22,    23,    24,    26,
     27,    29,    31,    33,    35,    37,    39,    41,
     44,    46,    49,    52,    55,    58,    62,    66,
     70,    74,    78,    83,    88,    93,    99,   105,
    111,   117,   124,   132,   140,   148,   157,   166,
    176,   187,   198,   210,   222,   235,   249,   264,
    280,   297,   314,   333,   353,   374,   396,   420,
    445,   471,   499,   529,   561,   594,   629,   667,
    706,   748,   793,   840,   890,   943,   999,  1059,
   1122,  1188,  1259,  1334,  1413,  1497,  1586,  1681,
   1781,  1886,  1999,  2117,  2243,  2377,  2518,  2668,
   2826,  2994,  3172,  3361,  3560,  3772,  3996,  4233,
   4485,  4751,  5033,  5332,  5648,  5983,  6337,  6713,
   7111,  7532,  7978,  8449,  8949,  9477, 10037, 10628,
  11254, 11916, 12616, 13356, 14138, 14964, 15837, 16758,
  17730, 18756, 19837, 20975, 22174, 23435, 24761, 25078,
  25078
};

// digital_oscillator.cc:1033. Q14 gain floor/ceiling on the six-partial
// harmonics sum.
const float kStruckDrumHarmonicsGainThreshold = 12888.0f;
const float kStruckDrumHarmonicsGainFloor = 4096.0f;

// digital_oscillator.cc:1034-1035,1068-1069. See the header's MIX WEIGHTS
// note for why 12288 and 12888 are both correct and both different.
const float kStruckDrumNoiseModeGainScale = 12888.0f;
const float kStruckDrumNoiseModeMixConstant = 12288.0f;

// MACRO's Decay spread range -- copied verbatim from struck_bell_engine.h's
// kBellMinDecaySpread/kBellStockDecaySpread/kBellMaxDecaySpread (THE FOURTH
// MACRO above explains why the same shape of control applies here).
const float kStruckDrumMinDecaySpread = 0.0f;
const float kStruckDrumStockDecaySpread = 1.0f;
const float kStruckDrumMaxDecaySpread = 4.0f;

// Stereo noise-mode crossfade offset (see the OUT/AUX note above) --
// ungrounded in Braids, which has no stereo output. Chosen to taste, small
// enough that both channels still read as the same drum.
const float kStruckDrumStereoBlend = 0.15f;

class StruckDrumEngine : public Engine {
 public:
  StruckDrumEngine() { }
  ~StruckDrumEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_STRUCK_DRUM; }

 private:
  // Shared between channels -- only the noise-lowpass chain differs L/R
  // (see the OUT/AUX stereo note above). Braids' RAW (un-normalised) scale,
  // matching kDrumPartialAmplitude and truncated every block the same way
  // Braids' own integer state is -- see THE AMPLITUDE STATE IS ALSO
  // INTEGER-TRUNCATED above. Divided by 65536.0f only at the point of use
  // in Render()'s per-sample partial read.
  float amplitude_[kNumStruckDrumPartials];
  float phase_[kNumStruckDrumPartials];

  // [0] = mono/left (always runs), [1] = right (stereo only, its own
  // independent noise draws -- R14, see particle_burst_engine.h). Carried in
  // Braids' RAW (un-normalised, +-32768) integer scale and floored every
  // stage, every sample, exactly as its `>> 15` does -- see NOISE-CHAIN
  // TRUNCATION above; divided by 32768.0f only at the point of use.
  int32_t lp0_[2];
  int32_t lp1_[2];
  int32_t lp2_[2];

  bool ever_struck_;

  DISALLOW_COPY_AND_ASSIGN(StruckDrumEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_STRUCK_DRUM_ENGINE_H_
