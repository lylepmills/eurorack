// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' BELL: eleven fixed inharmonic partials, additively synthesised and
// struck once per trigger, all sharing one TIMBRE-controlled decay curve and
// one COLOR-controlled odd/even detune.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderStruckBell
// (braids/digital_oscillator.cc:857-920). The partial data
// (kBellPartials/kBellPartialAmplitudes/kBellPartialDecayLong/Short,
// digital_oscillator.cc:825-839) is hand-authored directly in that file --
// unlike most Braids tables it has no generator in braids/resources/*.py, so
// it is embedded verbatim rather than "reproduced" (nothing to check it
// against but itself).
//
// LINEAGE. Plaits' modal-resonator (plaits/dsp/physical_modelling/
// resonator.cc) is a real filter bank: white noise or a click through a
// state-variable resonator per mode, with STRUCTURE bending a stretched-
// harmonic series continuously (NthHarmonicCompensation, resonator.cc:60-70),
// BRIGHTNESS and DAMPING shaping a per-mode Q that falls off geometrically
// with mode index (`q *= q_loss` every iteration, resonator.cc:133), and up
// to 24 modes whose amplitudes come from a cosine window over a continuous
// `position` (resonator.cc:48-52). None of that is BELL. BELL is not a
// resonator at all -- it is eleven independent sine partials with NO
// feedback, struck open-loop from a FIXED 11-point table of ratios,
// amplitudes and decay times that was hand-tuned to sound like a specific
// bell, not derived from a stretched-harmonic formula. modal-resonator's
// continuous STRUCTURE control can approximate a family of stiff-string/
// bell-like spectra, but it cannot reach this exact fixed voicing (try:
// there is no `structure` that reproduces partial 6 landing at exactly
// +12.000 st while partial 7 lands at +17.445 st and partial 9 at
// +22.922 st -- resonator.cc's stretch factor is a single monotonic curve,
// not eleven independently chosen points). Conversely modal-resonator can
// detune its exciter continuously and reach true inharmonic clusters BELL's
// fixed table never visits. The two are genuinely different instruments
// that happen to sit in the same "struck resonant metal" territory.
//
// PARAMETER MAPPING, with the line it was read from.
//   parameter_[0] (TIMBRE) sets the decay. Lines 894-904: when
//   parameter_[0] < 32000 (32000/32767 = 0.9766 of full scale), EVERY
//   partial's amplitude is multiplied, once per call, by a decay quantised
//   from a quadratic blend of two fixed per-partial tables:
//     `balance = ((32767 - parameter_[0]) >> 8)`, then `balance*balance >> 7`
//     `decay = decay_long - ((decay_long - decay_short) * balance >> 7)`
//     `partial_amplitude[i] = partial_amplitude[i] * decay >> 16`
//   Above that threshold the update is skipped outright -- a "droning" bell
//   that never loses energy. The port's `timbre` macro is the direct map;
//   the engine's Decay label is literal, not renamed.
//   parameter_[1] (COLOR) detunes odd and even partials apart. Lines
//   881-887: `partial_pitch = pitch_ + kBellPartials[i]`, then
//   `if (i & 1) partial_pitch += parameter_[1] >> 7; else -= ...`. The
//   port's `harmonics` macro is the direct map (matches the fold/particle-
//   burst convention: parameter_[1] = COLOR = HARMONICS).
//   Strike: `strike_` (set by DigitalOscillator::Strike(), called on the
//   module's own trigger rising edge) resets every partial's amplitude to
//   kBellPartialAmplitudes and phase to a quarter cycle (line 874,
//   `(1L << 30)` of a 32-bit accumulator = 0.25 of a cycle -- NOT phase
//   zero). The port checks `parameters.trigger & TRIGGER_RISING_EDGE` and
//   resets to phase 0.25 for the same reason: Interpolate824(wav_sine,
//   1<<30) reads -cos(pi/2) = 0, so every partial starts silent and ramps
//   in over the first sample rather than stepping to a value discontinuity.
//   `current_partial` (lines 865-869, 881-890): Braids only RE-TUNES three
//   of the eleven partials per call, round-robin, to save CPU on the
//   original hardware -- except on a strike, when all eleven are retuned at
//   once. This port retunes all eleven every call unconditionally (a
//   declared deviation, below); Plaits has no comparable per-sample CPU
//   pressure and the eleven-partial recompute is a handful of adds and one
//   NoteToFrequency-equivalent call each, done once per BLOCK, not per
//   sample.
//
// RATE. RenderStruckBell's sample loop (:906-919) is
//   `while (size--) { ...one partial-sum...; *buf++ = (out+prev)>>1;
//    *buf++ = out; size--; prev = out; }`
// -- an explicit second `size--` inside the body on top of the `while`
// condition's own decrement, so it consumes TWO units of `size` and writes
// TWO buffer samples per ONE partial-sum computation. That is SPEC R5's
// `size -= 2` pattern spelled with two separate decrements instead of one
// statement: the additive recursion (all eleven `partial_phase[i] +=
// partial_phase_increment[i]`, the sine reads, the sum) runs at 48 kHz, and
// the missing 96 kHz sample is reconstructed by averaging the current and
// previous 48 kHz-computed sample -- a 2-tap linear interpolant, not a
// repeat (contrast particle_burst_engine.h, whose `size -= 2` duplicates
// the SAME sample instead). Confirmed independently by the increment
// itself: `partial_phase_increment[i] = ComputePhaseIncrement(partial_pitch)
// << 1` (:889). `ComputePhaseIncrement(pitch)` alone, with no shift, is the
// increment DigitalOscillator::Render uses for `phase_increment_` in every
// UN-oversampled shape (:117, `phase_increment_ = ComputePhaseIncrement(
// pitch_)`, consumed once per 96 kHz output sample by those shapes' own
// `while(size--)` loops) -- so it is calibrated to ONE 96 kHz sample. The
// bell's `<< 1` doubles that to cover the elapsed time of TWO 96 kHz
// samples, i.e. one 48 kHz-equivalent sample, which is exactly R5's 48 kHz
// inner rate. Consequence: this port runs the partial-phase recursion once
// per OUTPUT sample at Plaits' native 48 kHz using `plaits::NoteToFrequency`
// on the SAME note arithmetic Braids does in its 1/128-semitone pitch
// space (`parameters.note + kBellPartials[i] / 128.0f [+-] detune`) --
// R6's correction comes along for free through NoteToFrequency, the way
// every direct-oscillator engine in this tree gets it (fold, csaw,
// dual-sync, ...), unlike particle-burst's table-index case which needed a
// manual shift. The 2-tap reconstruction itself is NOT reproduced: at a
// native 48 kHz there is a genuine computed sample every sample, nothing to
// interpolate between (same non-reproduction fold and particle-burst both
// declare for their own 48 kHz-inner functions).
//
// The per-call decay multiplier is a SEPARATE rate question from the
// per-sample recursion above: Braids applies it once per call to
// RenderStruckBell, and on real hardware that call is ALWAYS exactly
// kBlockSize = 24 samples at 96 kHz (braids.cc:57,276) = 250 us, a fixed
// wall-clock period regardless of how the recursion above is rated. Plaits'
// own kBlockSize is 12 samples at 48 kHz (plaits/dsp/dsp.h:51) -- the SAME
// 250 us -- so at the common call size this port's "once per Render() call"
// update lands on an identical cadence to Braids'. An atypical call size
// (the host CPU/flash tooling sometimes renders kMaxBlockSize = 24) takes
// `round(size / 12)` whole STEPS of the same update, never a fractional
// power of it: the update truncates (below), so it is not a smooth
// exponential and `powf(decay, size/12)` would not land on Braids' integers.
// See kBellBlockSamples below.
//
// AMPLITUDE STATE -- the quantisation that IS audible. Braids keeps each
// partial's amplitude in an int32 (digital_oscillator.h:134) and decays it
// with `partial_amplitude[i] = partial_amplitude[i] * decay >> 16`
// (digital_oscillator.cc:901-902). `>> 16` is an arithmetic shift, so the
// state truncates DOWN by up to 1 LSB every block on top of the
// multiplicative loss -- and since `decay` is 65519..65533 for the long
// table, the 1-LSB floor DOMINATES: once `amplitude * (65536 - decay) /
// 65536 < 1` the decay is effectively linear at one count per 250 us, and
// the partial reaches EXACT zero in finite time. Braids' bell always stops
// ringing. A float `amplitude *= decay / 65536.0f` does not: it decays
// smoothly and asymptotically, so the port would ring for tens of seconds
// where the module falls silent in two. Measured on the A/B harness before
// this was reproduced: +2.39 dB AC RMS and 3.91 dB spectrum over a single
// 10 s strike at TIMBRE 0.93, where balance quantises to 0 and the decay is
// exactly kBellPartialDecayLong -- and, more quietly, essentially ALL of
// the +0.05..+0.37 dB the six shorter A/B cases used to read. So the port
// carries Braids' int32 amplitude counter verbatim (amplitude_ below is
// that counter, Q15, not a normalised float) and converts to a float gain
// once per block at readout.
//
// OUTPUT SCALE. `out += partial * partial_amplitude[i] >> 17`, both Q15
// (32768 = unity): `partial_f * 32768 * amplitude_f * 32768 / 2^17 =
// partial_f * amplitude_f * 2^13`. The buffer sample IS that value
// (post-CLIP to +-32767), so as a normalised float, `/32768` gives
// `partial_f * amplitude_f * 0.25` -- kBellOutputScale below. Verified
// against the table data: the eleven kBellPartialAmplitudes sum to 122958,
// *0.25/32768 = 0.938 -- just under the CLIP ceiling if every partial's
// sine happened to peak in phase together at the strike instant, which is
// exactly the design headroom a hard-clipped additive strike wants.
//
// wav_sine is Braids' -cos(2*pi*phase), not Plaits' sin(2*pi*phase) lut_sine.
// BraidsSine() evaluates the equivalent -sin(phase + 0.25) form so the
// already-normalized phase can use SineNoWrap without a redundant wrap.
// Verified by extracting the 257-entry table from
// braids/resources.cc and fitting it programmatically: it runs -32512 at
// phase 0 to +32766 at phase 0.5, i.e. wav_sine[i] = 32638*-cos(2*pi*i/256)
// + 127 to within 1 LSB (the residual is the table's own dither noise --
// braids/resources/waveforms.py's scale() runs an order-2 noise-shaped
// quantizer on top of the continuous formula, not a plain round()) --
// rather than a full-scale, zero-mean cosine. This EARLIER version of this
// note assumed the resulting +127 DC pedestal was "removed by the A/B
// before comparison" and that the 32638/32768 = 0.034 dB amplitude
// shortfall alone explained the whole residual. That assumption was
// UNTESTED and WRONG: the pedestal is added inside BraidsSine() and then
// multiplied by each partial's own decaying amplitude
// (`BraidsSine(phase)*partial_gain[i]`, struck_bell_engine.cc), so it is not
// a fixed DC term at all -- it is a slowly-decaying quasi-DC that tracks the
// bell's own envelope, and it DOES show up in both the A/B's AC-RMS window
// and its spectrum metric. Measured directly (A/B with vs. without the
// pedestal + exact 32638 scale, all seven ab.json cases): AC RMS residual
// drops from +0.01..+0.06 dB (every case reading hot) to -0.00..+0.03 dB,
// and spectrum error drops in all seven cases too (stock-mid 0.07->0.04 dB,
// decay-short 0.04->0.02 dB, decay-long 0.10->0.03 dB, decay-drone
// 0.03->0.03 dB, detune-none/-max 0.06->0.04 dB, high-register
// 0.09->0.05 dB). So BOTH the amplitude scale AND the DC pedestal are
// reproduced (kBellWavSineScale, kBellWavSinePedestal in the .cc) -- this is
// not a declared deviation, it is the exact table form, kept because it
// measurably tightens the fit. The residual +0.03 dB on high-register alone
// is NOT this pedestal (removing it moved every case, including
// high-register, in the same direction) -- it is the continuous-Sine()-vs-
// 257-entry-LUT substitution declared below, which this port does NOT
// reproduce.
//
// THE FOURTH MACRO.
//   MACRO  Decay spread   Braids' two decay tables already make high
//                         partials die faster than low ones (compare
//                         kBellPartialDecayLong[0] = 65533 against [10] =
//                         65519), but TIMBRE can only slide ALL eleven
//                         partials together between the two fixed tables --
//                         it cannot change how MUCH they diverge from each
//                         other. This macro can: each partial's per-call
//                         decay multiplier is pulled toward (0) or pushed
//                         away from (1) the ensemble's mean, stock (0.5)
//                         exactly reproducing Braids' own spread. At 0
//                         every partial decays at one uniform rate (an
//                         additive bell with no spectral "fizz" at all); at
//                         1 the spread is 4x Braids' own, so the high
//                         partials flash out almost immediately and the
//                         fundamental rings on far past where the module
//                         ever lets it. Neither the module (no such
//                         control exists) nor modal-resonator (whose
//                         `q_loss` is one fixed geometric curve set by
//                         BRIGHTNESS/DAMPING, not a variable spread around
//                         an existing per-mode voicing) can do this.
//   MORPH  Brightness     Braids' eleven initial strike amplitudes
//                         (kBellPartialAmplitudes) are a hardcoded table --
//                         no parameter ever touches them; a soft mallet and
//                         a hard mallet sound identical. This macro tilts
//                         the INITIAL amplitude of each partial, at strike
//                         only, by a linear function of its position in the
//                         table (kBellBrightnessDepth below): darker/softer
//                         toward 0, Braids' own fixed amplitudes exactly at
//                         0.5, brighter/harder toward 1. Applied once per
//                         strike, not continuously, so sweeping MORPH while
//                         a droning (TIMBRE near 1) note sustains does not
//                         retroactively relight a bell that has already
//                         been struck -- the same way turning a mallet
//                         knob does not change a note already ringing.
//
// TRIGGER_UNPATCHED. Braids' bell has no concept of "unpatched" -- the
// hardware TRIG jack is either struck or it is not. Plaits' own struck
// engines (modal-resonator, the analog drums) all treat an unpatched
// trigger as SUSTAIN rather than silence (modal_engine.cc:65, hi_hat_engine
// .cc:61,75 pass `trigger & TRIGGER_UNPATCHED` down as a `sustain` flag),
// so an unplugged preview or hosted-builder audition still makes sound
// instead of a bell that can never be struck. This port follows that house
// convention rather than Braids literally: on TRIGGER_UNPATCHED the engine
// self-strikes exactly once (the first Render() call after Reset(), so a
// preview always hears the strike's transient) and then holds -- decay is
// suppressed for as long as the trigger stays unpatched, the same way
// TIMBRE >= the drone threshold suppresses it. This is a design choice, not
// a Braids behaviour, and is not covered by tests/ab.json (every case there
// drives triggerHz > 0, which never sets TRIGGER_UNPATCHED -- see
// render_model.cc:91-99).
//
// alreadyEnveloped is declared true in plaits-engine.json's
// postProcessing: the amplitude decay above IS the note's whole loudness
// envelope, generated internally exactly like Braids' module (which has no
// separate VCA stage either). Per voice.cc's `lpg_bypass = already_enveloped
// || ...` (voice.cc:329), leaving it false would let Plaits' own LPG gate
// this decay a second time and choke it early on a short external gate --
// the same reasoning modal-resonator and the analog drums' own metadata
// follows.
//
// OUT: the eleven-partial sum (mono, or the left channel in stereo). It
// tracks Braids' own output closely -- see tests/ab.json for the measured
// figures -- but is NOT sample-identical: the declared deviations below
// (continuous sine for the 257-entry LUT, no 2-tap 96 kHz reconstruction,
// all eleven partials retuned per block) each move it by a little.
//
// AUX (mono): the
// five highest partials (indices 6-10, +12 st and up) alone -- a bright,
// fast-decaying companion voice OUT does not foreground, cheap because it
// reuses the same per-sample partial values OUT's loop already computed.
//
// In stereo, AUX drops the upper-partial subset for a second, fully
// independent eleven-partial render (its own phase and pitch state, sharing
// the SAME amplitude envelope as the left channel -- decay does not need a
// second copy, only detuned pitch does): every partial's pitch is offset by
// -+kBellStereoDetuneSemitones/2 between channels (left gets the negative
// half, matching fold_engine.cc's `blend_main`/`blend_aux` split, which
// shifts ITS main output too, not just aux), a fixed, ungrounded (no Braids
// equivalent -- the module has no stereo output at all) chorus-width
// choice, not derived from anything. The offset on both sides is exactly
// zero whenever `stereo` is false, so a MONO render (parameters.stereo
// unset, which is what tests/ab.json exercises) matches Braids' tuning
// precisely; enabling stereo shifts OUT's own tuning by -kBellStereo
// DetuneSemitones/2 as part of widening it, verified by direct comparison
// (a standalone host build, not part of this SDK's tooling): first-block
// L/R differ by up to 0.00196 and mono-OUT differs from stereo-run OUT by
// up to 0.00103 at note 57, HARMONICS 0.6, full 4000-block run -- small,
// expected, and exactly the size a +-0.04 semitone-ish pitch offset over a
// held partial predicts, not a bug.
//
// Declared deviations from Braids:
//   - all eleven partials retune every block; Braids staggers three per
//     call except on a strike. Both converge to the same increments within
//     ~1 ms of a static note (four blocks), which is what every ab.json
//     case holds; the two differ only during a fast pitch glide, which
//     none of them exercise.
//   - the 2-tap 96 kHz reconstruction (average of the current and previous
//     48 kHz-computed sample) is not reproduced; this port computes a
//     genuine sample every output sample at its native 48 kHz instead (see
//     RATE above).
//   - partial reads use plaits::Sine() through BraidsSine(), a continuous
//     evaluation, rather than Braids' 257-entry wav_sine table read through
//     Interpolate824. BraidsSine() DOES reproduce wav_sine's amplitude scale
//     and DC pedestal exactly (kBellWavSineScale/kBellWavSinePedestal --
//     see the wav_sine note above); what remains a deviation is only the
//     continuous-vs-257-entry-LUT evaluation itself. Both are effectively
//     bandlimited sine lookups; the residual this leaves is small (the
//     +0.03 dB high-register AC RMS reading after the pedestal fix above)
//     and inaudible on a pure additive sum with no nonlinearity to fold it
//     through (contrast fold_engine.h, where the same substitution would
//     NOT be safe).
//   - NOT a deviation, called out because it looks like one: the TIMBRE
//     decay-balance and COLOR detune arithmetic, and the per-block
//     amplitude update, all keep Braids' exact integer truncation (`>>8`,
//     `>>7` reproduced as `floorf(x / 256.0f)` / `floorf(x / 128.0f)` on
//     non-negative operands, and the amplitude state kept as the int32 it
//     is in Braids). All three are once-per-BLOCK, so reproducing them
//     costs nothing; the amplitude one is not optional at all (see
//     AMPLITUDE STATE above).
//   - TRIGGER_UNPATCHED sustain (see above) has no Braids equivalent at
//     all; it follows the sibling stock engines' convention instead.

#ifndef PLAITS_DSP_ENGINE2_STRUCK_BELL_ENGINE_H_
#define PLAITS_DSP_ENGINE2_STRUCK_BELL_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const size_t kNumBellPartials = 11;

// digital_oscillator.cc:825-826. Partial ratios, in Braids' 1/128-semitone
// pitch units (kOctave = 12 * 128, digital_oscillator.cc:46) -- converted to
// semitones with /128.0f below, added directly to parameters.note.
const int16_t kBellPartials[kNumBellPartials] = {
  -1284, -1283, -184, -183, 385, 1175, 1536, 2233, 2434, 2934, 3110
};

// digital_oscillator.cc:829-830. Initial per-partial strike amplitude, Q15
// (32768 = unity); see OUTPUT SCALE above for how this becomes a float gain.
const int16_t kBellPartialAmplitudes[kNumBellPartials] = {
  8192, 5488, 8192, 14745, 21872, 13680, 11960, 10895, 10895, 6144, 10895
};

// digital_oscillator.cc:833-834 and :837-838. Per-partial per-BLOCK decay
// multiplier, Q16 (65536 = no decay at all). TIMBRE quadratically blends
// between these two tables; see PARAMETER MAPPING above for the exact
// integer arithmetic reproduced verbatim.
const uint16_t kBellPartialDecayLong[kNumBellPartials] = {
  65533, 65533, 65533, 65532, 65531, 65531, 65530, 65529, 65527, 65523, 65519
};
const uint16_t kBellPartialDecayShort[kNumBellPartials] = {
  65308, 65283, 65186, 65123, 64839, 64889, 64632, 64409, 64038, 63302, 62575
};

// digital_oscillator.cc:894, the literal threshold on Braids' 0..32767
// parameter_[0] range -- above it the decay update is skipped outright
// ("droning" bell). 32000.0f / 32767.0f = 0.9766 of full TIMBRE travel.
const float kBellDroneThreshold = 32000.0f;

// One Braids call to RenderStruckBell is ALWAYS kBlockSize = 24 samples at
// 96 kHz (braids.cc:57,276) = 250 us of wall-clock time, which the decay
// multiplier above is calibrated to. Plaits' own kBlockSize is 12 samples
// at 48 kHz (plaits/dsp/dsp.h:51) -- the SAME 250 us -- so `size == 12` is
// the common case that needs no correction at all; see RATE above for the
// general `round(size / kBellBlockSamples)` step count used otherwise.
const float kBellBlockSamples = 12.0f;

// digital_oscillator.cc:874, `(1L << 30)` of a 32-bit phase accumulator.
const float kBellStrikePhase = 0.25f;

// See OUTPUT SCALE above: partial_f * amplitude_f * 8192, normalised to a
// +-32767 sample by /32768.
const float kBellOutputScale = 0.25f;

// Braids' COLOR detune, in semitones: parameter_[1] (0..32767) >> 7, /128 to
// convert Braids' pitch-128 units to semitones. Maximum is (32767 >> 7) /
// 128.0f = 1.9922 st, verified programmatically from the same arithmetic
// PARAMETER MAPPING reproduces at runtime.
const float kBellMaxDetuneSemitones = 1.9921875f;

// MACRO's Decay spread range (stock = Braids' own spread exactly, see THE
// FOURTH MACRO above). Chosen to taste, not derived.
const float kBellMinDecaySpread = 0.0f;
const float kBellStockDecaySpread = 1.0f;
const float kBellMaxDecaySpread = 4.0f;

// MORPH's Brightness tilt (see THE FOURTH MACRO above). Chosen to taste.
const float kBellBrightnessDepth = 1.5f;

// Stereo chorus width (see the OUT/AUX note above) -- ungrounded in Braids,
// which has no stereo output. Small enough to read as one wide bell, not
// two different ones.
const float kBellStereoDetuneSemitones = 0.08f;

class StruckBellEngine : public Engine {
 public:
  StruckBellEngine() { }
  ~StruckBellEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_STRUCK_BELL; }

 private:
  // Braids' own int32 Q15 amplitude counter (digital_oscillator.h:134), not
  // a normalised float -- see AMPLITUDE STATE above. Shared between channels;
  // only pitch/phase differ L/R (see the stereo note above).
  int32_t amplitude_[kNumBellPartials];
  // [0] = mono/left, [1] = right (stereo only).
  float phase_[2][kNumBellPartials];

  bool ever_struck_;

  DISALLOW_COPY_AND_ASSIGN(StruckBellEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_STRUCK_BELL_ENGINE_H_
