// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' KICK: two decaying excitation pulses driving a resonant bandpass
// tuned to the played note, with a brief pitch-up "click" at the start of
// each hit and a self-brightening ("punch") dynamic filter modulation, then
// smoothed by an output one-pole. Self-enveloping -- there is no separate
// VCA stage, the sound decays entirely on its own once struck.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderKick
// (braids/digital_oscillator.cc:2303-2366).
//
// LINEAGE. Plaits' own analog-bass-drum (plaits/dsp/engine/bass_drum_engine.cc)
// is the module's refined descendant for this family, and it is a DIFFERENT
// synthesis mechanism, not a retuned copy. analog-bass-drum's click/punch is
// FM: an oscillator briefly self-modulates its own frequency
// (bass_drum_engine.cc:62-63, attack_fm_amount/self_fm_amount), and its "punch"
// macro (bass_drum_engine.cc:61, ApplyMacro(1,0.25,1.75,macro)) scales that FM
// depth. KICK's click/punch is filter-side: a Chamberlin state-variable
// bandpass (braids/svf.h) whose CENTRE FREQUENCY jumps 17 semitones above the
// note for ~4 ms (digital_oscillator.cc:2352, the pulse_[2] countdown) and
// whose own coefficients self-modulate from its recent output level
// (svf.h:86-90, "punch"), driven by two decaying excitation pulses of opposite
// polarity (digital_oscillator.cc:2328-2330) rather than an FM operator. So
// this port's click is a resonator being retuned out from under a fixed
// exciter, where analog-bass-drum's is an oscillator retuning itself -- an
// audibly different kind of pitch-drop, and the reason both are worth having.
//
// PARAMETER MAPPING, with the line it was read from. RenderKick reads both
// parameters directly, with no BEGIN_INTERPOLATE_PARAMETER_N macro at all
// (digital_oscillator.cc:2334,2340) -- so there is no INTERPOLATE_PARAMETER_1
// "reads parameter_[1], i.e. COLOR" trap to misread here (see fold_engine.h);
// the array index IS the read.
//   parameter_[0] (TIMBRE) sets the bandpass resonance/decay
//     (digital_oscillator.cc:2334-2338): a cubic-ish falloff from TIMBRE into
//     Svf::set_resonance, so TIMBRE climbing makes the resonance climb toward
//     Braids' near-self-oscillation ceiling -- TIMBRE UP is a LONGER boom, not
//     a shorter one, despite the local variable being named `decay`.
//   parameter_[1] (COLOR) sets the coefficient of the OUTPUT one-pole
//     (digital_oscillator.cc:2340-2343, `lp_coefficient`) that the resonator's
//     sum is smoothed through before it ever reaches the buffer -- COLOR is
//     brightness/attack sharpness of the FINAL mix, applied after the
//     resonator, not a property of the resonator itself.
//
// RATE. RenderKick ends in `size -= 2` (digital_oscillator.cc:2362): the
// excitation generator (both pulse_[] countdowns and the frequency/resonance
// pick) advances once per iteration, at a 48 kHz-equivalent cadence -- its
// own delay constants are computed against 48000 (digital_oscillator.cc:2313,
// 2317, `1.0e-3 * 48000` / `4.0e-3 * 48000`), so by SPEC R5's rule those
// transfer verbatim to a 48 kHz-native port.
//
// The bandpass itself does NOT share that rate. `svf_[0].Process()` is called
// TWICE per iteration (digital_oscillator.cc:2354-2361) with the SAME
// excitation sample held across both calls, but the filter's own recursion
// state genuinely advances on each call -- so the resonator runs at Braids'
// native 96 kHz, fed a zero-order-held 48 kHz excitation, not a duplicated
// 96 kHz write the way a naive read of "size -= 2" would suggest (contrast
// fold_engine.h's guards, which DO transfer verbatim because fold's internal
// rate is also 192 kHz on both sides). The coefficients driving that 96 kHz
// recursion, `lut_svf_cutoff`/`lut_svf_damp`
// (braids/resources/lookup_tables.py:98-107), are generated against
// `sample_rate = 96000` (lookup_tables.py:35, still in effect at line 98 --
// the file does not reassign it to 48000 until line 254, a section this
// engine's tables sit well before). So, unlike particle-burst's resonator
// table, which sits at lookup_tables.py:59-90 and divides by `sample_rate / 2`
// (line 63) -- i.e. a 48000 denominator already, from the SAME 96000
// assignment, no reassignment involved (see particle_burst_engine.h's RATE
// section) -- this table divides by `sample_rate` itself (line 99), so it is
// calibrated for the wrong rate for a single-pass 48 kHz port: transplanting
// it verbatim would tune the resonator an octave flat (this port's rate is HALF
// Braids' native rate, so Braids' own cutoff/96000 value used verbatim as a
// cutoff/48000-equivalent coefficient targets half the intended Hz).
//
// This port does not 2x-oversample to reach that native 96 kHz. Instead the
// coefficient is RE-DERIVED at Plaits' own rate: `f = 2*sin(pi*f_norm)` with
// `f_norm` read through `NoteToFrequency` (which carries kCorrectedSampleRate,
// SPEC R6) and clamped to 1/8 exactly as lookup_tables.py:100 does, the same
// closed form the table was generated from, just evaluated at the port's
// actual rate instead of Braids' 96 kHz. This is chosen over oversampling
// because the reference this port is measured against is ALREADY the 96 kHz
// signal decimated (band-limited) to 48 kHz (ab_engine.py always renders
// `--rate 48000`), and because the excitation feeding the resonator is a
// decaying pulse pair, not band-limited noise -- it carries negligible energy
// near the port's 24 kHz Nyquist for the zero-order hold Braids applies to
// matter. This mirrors particle_burst_engine.h's own choice to run its
// resonator recursion once per OUTPUT sample rather than oversample. Measured
// below (A/B NOTES).
//
// The OUTPUT one-pole (`lp_state`, digital_oscillator.cc:2357) sits in the
// SAME twice-per-iteration block the SVF does, so it is the SECOND place this
// applies, and it needs its own fix: a one-pole applied with coefficient k
// TWICE decays by (1-k)^2 per iteration, not (1-k) once, so transplanting k
// verbatim into a single 48 kHz-rate step halves its effective cutoff rather
// than reproducing it. KickToneCoefficient (kick_engine.cc) solves
// k' = 1 - (1-k)^2 for the port's single step instead. This was NOT caught by
// reasoning about it up front -- it was caught by A/B NOTES' first pass
// measuring tone-low at -3.7 dB AC RMS before this fix (COLOR=0 is where k is
// small enough for the missed factor of ~2 to matter; see A/B NOTES).
//
// There is a THIRD place, and it is the one this port originally missed:
// PUNCH's frequency term is an ADDEND to `f`, so it lives in the same
// rate-dependent coefficient domain `f` does and needs the same 2x -- see
// kKickPunchFreqScale below, and A/B NOTES for what leaving it out measured.
// The general shape to watch for: re-deriving a filter coefficient at a new
// rate is not finished until EVERY term that is summed into it has been
// re-derived too.
//
// TABLES. No table is embedded. `lut_svf_cutoff`/`lut_svf_damp` need re-
// deriving for the port's rate regardless (previous paragraph), so this is
// the "substitute the formula" half of SPEC's table rule, not the "embed and
// verify" half.
//
// Both ARE truncated uint16 tables, so the substitution does drop a real
// quantisation, and its size is worth stating rather than waving away.
// `lut_svf_cutoff[i] = floor(2*sin(pi*f)*32767)` loses up to 1 LSB out of an
// entry that is SMALL at the low indices a kick lives in -- 140.26 -> 140 at
// index 36, 70.13 -> 70 at index 24 -- so Braids' own resonator sits about
// 0.19% (~3.2 cents) FLAT of the nominal note down there, shrinking to ~0.3
// cents by index 60 as the entries grow. The port evaluates the closed form
// and so sits at nominal; that ~3 cents (alongside SPEC R6's +4.6) is the
// whole of the port's measured pitch offset from the reference once PUNCH is
// scaled correctly (A/B NOTES). It is left unreproduced deliberately: the
// port cannot read the table anyway (wrong rate), and re-quantising in
// Braids' 96 kHz coefficient domain would cost a rate round-trip whose own
// small-angle error at the top of the range is the same order as the LSB it
// is modelling. `lut_svf_damp`'s truncation is ~0.05% on the damping at
// TIMBRE 0.5, i.e. inaudible. Neither is a near-a-cusp read:
// `f = 2*sin(pi*f_norm)` and
// `damp = min(2*(1-r^0.25), min(2, 2/f - f*0.5))` (lookup_tables.py:100-104)
// are smooth, monotonic, transcendental-in-the-inner-loop formulas with no
// near-zero-derivative point the way particle-burst's `2*cos(theta)` pole has
// near theta=0 -- so evaluating them continuously in float, rather than
// through Braids' own int16 quantisation of TIMBRE into `resonance_`, changes
// nothing worth reproducing (KickResonanceDomain/KickToneCoefficient in the
// .cc do the same continuous substitution for TIMBRE's and COLOR's own
// int16-truncated polynomials, which are similarly smooth).
//
// PUNCH. Svf::set_punch (svf.h:70-72,86-90) self-modulates the filter's own
// coefficients from its recent low-pass output level, and Braids only ever
// calls it with ONE fixed argument, 24000, on every strike
// (digital_oscillator.cc:2331) -- the 32768 the Init block also sets
// (digital_oscillator.cc:2321) is overwritten by that SAME call on the very
// first Render, before Process() ever reads it, so it is dead and this port
// does not model it. `punch_ = (24000*24000) >> 24` truncates to 34
// (kKickPunchStockCoefficient below); reproduced as the exact truncated
// integer per SPEC's table rule, since unlike the SVF coefficients this IS a
// single hardcoded constant Braids computes once, not a smooth function worth
// substituting continuously.
//
// THE FOURTH MACRO.
//   MORPH  Exciter balance   Crossfades the two excitation pulses
//                             (digital_oscillator.cc:2328-2329, opposite
//                             polarity) against each other:
//                             0.5 leaves BOTH at Braids' native level (the
//                             module exactly), 0 fades out the negative
//                             "thump" pulse leaving only the sharp positive
//                             click, 1 fades out the click leaving only the
//                             thump. Braids hardwires both at unity; this is
//                             a balance the module cannot reach.
//   MACRO  Punch              Scales PUNCH's self-modulation strength around
//                              Braids' fixed level: 0.5 reproduces the
//                              module's one hardcoded punch amount exactly,
//                              0 disables the dynamic filter modulation
//                              entirely (a plain, undriven resonant filter),
//                              1 pushes it to 4x. Braids gives no control over
//                              this at all -- it is one hardcoded call. This
//                              scales BOTH terms svf.h:88-89 modulates
//                              together: the frequency-brightening one, which
//                              pushes the centre frequency UP while the
//                              filter is loud, and the damping one, which
//                              (note the sign -- `damp += (punch_signal -
//                              2048) >> 3`) INCREASES damping while it is
//                              loud, i.e. shortens the ring at the start of
//                              a hit and lets it lengthen as the hit decays;
//                              Braids' own formula only ever runs them at the
//                              one fixed ratio between them since punch_'s
//                              level never varies in the source, so "scale
//                              both by the same amount" is this port's own
//                              choice for extending a single fixed setting
//                              into a continuous control, not a Braids
//                              behaviour being reproduced.
//
// OUT: the self-enveloped kick (Braids' own output, `lp_state`,
// digital_oscillator.cc:2359). AUX (mono): the raw excitation pulses, before
// the resonator, soft-clipped for headroom -- the same "raw exciter on AUX"
// idiom bowed_engine.h and particle_burst_engine.h document, though KICK's
// excitation swings far past unity by design (it is meant to slam the filter
// to full scale on every strike, see A/B NOTES) so AUX needs the soft clip
// particle-burst's bounded noise burst does not.
//
// In stereo, OUT/AUX become a mid/side pair in the SAME idiom
// bass_drum_engine.cc already uses for this instrument family: the low/mid
// body (`sample`, mono-identical either way) stays centred, and a small
// soft-clipped slice of the raw excitation is spread as an L-R difference, so
// L+R sums back to twice the mono body and the low end stays fully
// mono-compatible.
//
// Declared deviations from Braids:
//   - the bandpass AND the output one-pole run once per OUTPUT sample at
//     Plaits' native rate with re-derived coefficients, not 2x-oversampled to
//     Braids' native 96 kHz (RATE above). Measured impact in A/B NOTES.
//   - TIMBRE's resonance polynomial and COLOR's output-coefficient polynomial
//     (digital_oscillator.cc:2334-2343) are evaluated continuously in float
//     rather than through Braids' int16 truncation of the knob position
//     itself (TABLES above) -- a sub-LSB difference in TIMBRE/COLOR's own
//     quantisation, not in any table read.
//   - the Chamberlin recursion (svf.h:91-96) runs in float, so it does not
//     carry Braids' per-step `>> 15` truncation, and the excitation pulses
//     (excitation.h:66) decay in float rather than through Braids' per-step
//     `>> 12` truncation -- both pulses are spent inside ~25 samples, so the
//     truncation never accumulates anywhere it matters.
//   - `lut_svf_cutoff`/`lut_svf_damp`'s uint16 truncation is not reproduced
//     (TABLES above): Braids' resonator sits ~3 cents flat of nominal at the
//     low table indices a kick uses, the port sits at nominal.
//   - MORPH and MACRO are this port's own additions (THE FOURTH MACRO above),
//     with no Braids behaviour to match at any position off 0.5.
//   - TRIGGER_UNPATCHED is not handled: with nothing patched to TRIGGER this
//     engine is silent, exactly as the module is with no gate. The sibling
//     Braids percussion ports (struck_drum_engine.cc:84-89,
//     struck_bell_engine.cc:65-70, snare_engine.cc:171-177) instead adopt
//     Plaits' own "unpatched fires once" convention. Left as-is here because
//     KICK has no drone/sustain state to hold open the way those do; revisit
//     if the wave settles on one rule.
//
// A/B NOTES. All five tests/ab.json cases (TIMBRE at 0, 0.5 and 0.85, COLOR
// at both ends and centre) measure AC RMS within 0.22 dB and full-band
// spectrum within 0.20 dB. Per-band, every band carrying more than ~1% of the
// energy is within ~0.3 dB; the larger per-band deltas (up to -7.9 dB at
// tone-low's 20.48-24 kHz bin, +6.5 dB at decay-low's) sit in bands each
// carrying ~0.1% of total energy and read as the usual decimator-vs-FIR
// difference (SPEC's "confined to the top octave" tell).
//
// Those five cases move ONE Braids axis at a time off centre, and the CORNER
// they therefore miss is the port's weakest point: at TIMBRE 0 AND COLOR 0
// together it measures +0.82 dB AC RMS / 0.74 dB spectrum, roughly 4x any
// declared case, falling back under 0.25 dB by COLOR 0.25. That is the
// residue of the RATE deviation, not a separate bug: at COLOR 0 the output
// one-pole's coefficient is its floor (128/32768), putting its cutoff around
// 60 Hz -- right on top of a note-36 kick's own fundamental -- and k' =
// 1-(1-k)^2 matches Braids' two-step POLE exactly but not the zero its
// intermediate 96 kHz sample contributes. It only bites where that cutoff
// coincides with the fundamental AND the hit is short enough for the
// transient to dominate the level; oversampling the one-pole is the fix if
// this corner ever matters more than the CPU does.
//
// Two rate bugs were caught here rather than in the RATE reasoning that was
// supposed to prevent them; both are what that section now describes:
//   - the output one-pole with `k` transplanted verbatim measured tone-low
//     (COLOR=0, the smoothing at its slowest) at -3.7 dB AC RMS; solving
//     k' = 1-(1-k)^2 took it to +0.56 dB.
//   - PUNCH's frequency addend transplanted verbatim (i.e. without the
//     kKickSvfRateRatio factor) left the resonator flat -- ~35 cents at note
//     36 with `lp_` at PUNCH's 2048 floor, ~90 cents at note 24, more during
//     the loud opening of a hit -- and the whole PUNCH pitch gesture at half
//     depth. It cost about 0.5 dB of full-band spectrum in every case
//     (0.42-0.87 dB before, 0.10-0.20 dB after) and 0.6 dB of AC RMS at
//     tone-low. It did NOT show up in ab_engine.py's `cents` figure, which
//     is why the omitted pitch tolerance mattered -- see below.
//
// PITCH. The reference and the port are Goertzel-tracked in 60 ms windows
// (outside ab_engine.py) on a single struck note. PUNCH does bend the
// resonator upward while the hit is loud, so there IS a glide -- at note 60,
// TIMBRE 0.85, the reference runs 271.3 Hz at 10 ms down to a settled
// 265.1 Hz by ~200 ms, about 40 cents, and the port tracks it (272.8 ->
// 265.8). After PUNCH's `lp_` falls to its 2048 floor the centre frequency
// is CONSTANT, so the sustained part of every hit does have a single well-
// defined f0, and the port sits ~10 cents above the reference there: SPEC
// R6's +4.6 plus the ~3 cents of lut_svf_cutoff truncation the port does not
// reproduce (TABLES).
//
// tests/ab.json still declares no `cents` tolerance, but for a narrower and
// checkable reason than "the pitch glides": with triggerHz 1-2 and 3-4 second
// renders, ab_engine.py's fixed mid-file 0.25 s window lands in a tail that
// is 50-90 dB down, where its autocorrelation locks onto something that is
// not the resonator at all -- it reported the identical figure for both sides
// in all five cases both BEFORE and AFTER the PUNCH fix above, i.e. while the
// port was genuinely tens of cents flat. The metric is inert on this model,
// not merely noisy, so declaring a tolerance on it would assert a fidelity
// the harness is not measuring. Pitch for this engine is checked out of band,
// by the Goertzel track described above.

#ifndef PLAITS_DSP_ENGINE2_KICK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_KICK_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Excitation pulse timing, braids/digital_oscillator.cc:2309-2314 -- delays
// in ticks at the 48 kHz-equivalent excitation-generator cadence (RATE
// above), decays as Braids' own decay/4096 fraction.
const int32_t kKickPulse0DelayTicks = 0;
const float kKickPulse0Decay = 3340.0f / 4096.0f;
const int32_t kKickPulse1DelayTicks = 48;  // 1.0e-3 * 48000, line 2313
const float kKickPulse1Decay = 3072.0f / 4096.0f;

// Strike levels, braids/digital_oscillator.cc:2328-2329 -- `int32_t` from a
// double expression, so C++ TRUNCATES toward zero on the implicit
// double->int32 conversion: 12*32768*0.7 = 275251.2 -> 275251;
// -19662*0.7 = -13763.4 -> -13763. Reproduced at the truncated integer, then
// normalized by 32768 into this port's float sample scale. Deliberately far
// past unity (pulse 0 alone is >8x full scale) -- Braids drives the filter
// hard enough to slam it to Svf's own clip on every strike regardless of the
// decay curve shape; see A/B NOTES for what happens if this is "corrected"
// to a sane amplitude instead of reproduced.
const float kKickPulse0Level = 275251.0f / 32768.0f;
const float kKickPulse1Level = -13763.0f / 32768.0f;

// The constant Braids adds on top of pulse 1 for as long as it is not yet
// done (digital_oscillator.cc:2349), and pulse 2's pure-timer delay for how
// long the resonator's centre frequency sits 17 semitones up
// (digital_oscillator.cc:2317,2352 -- pulse_[2]'s own state/level are unused,
// only its countdown is read).
const float kKickPulse1Hold = 16384.0f / 32768.0f;
const int32_t kKickClickDelayTicks = 192;  // 4.0e-3 * 48000, line 2317
const float kKickClickSemitones = 17.0f;  // 17 << 7, line 2352

// The Chamberlin SVF's own frequency ceiling (lookup_tables.py:100,
// `f[f > 1/8] = 1/8`) and Braids' int16 sample clip (stmlib/stmlib.h:39,
// applied to the SVF's internal states AND the output one-pole).
const float kKickSvfMaxNormalizedFrequency = 0.125f;
const float kKickClip = 32767.0f / 32768.0f;

// PUNCH, svf.h:70-72,86-90. `punch_ = (24000*24000) >> 24` truncates to 34
// (PUNCH above); the frequency term divides by 8192 (>>4 then >>9, i.e.
// /16/512) and the damping term by 8 (>>3), both after normalizing Braids'
// int16 `lp_` into this port's float scale (hence the extra /32768 folded
// into the floor/threshold below).
//
// The frequency term is a THIRD place RATE matters (see RATE above, and
// KickToneCoefficient for the second). It is an ADDEND to `f`, in the same
// Chamberlin-coefficient domain `f` itself lives in, and `f = 2*sin(pi*fc/fs)
// ~= 2*pi*fc/fs` scales with 1/fs -- so this port's `f`, re-derived at half
// Braids' resonator rate, is ~2x Braids' for the same fc, and an addend
// transplanted verbatim buys only HALF the pitch shift in Hz that Braids
// gets. Hence the 2.0 rate factor: without it the resonator sits flat by
// however much PUNCH is currently bending it (about 35 cents at note 36 with
// `lp_` at PUNCH's own 2048 floor, ~90 cents at note 24, and hundreds of
// cents during the loud opening of a hit -- the whole PUNCH gesture came out
// at half depth). The damping term needs NO such factor: the resonator's
// per-step loss is f*damp/2, so doubling f already doubles the loss per
// sample, and `damp` (including this addend) transfers verbatim.
const float kKickPunchStockCoefficient = 34.0f;
const float kKickSvfRateRatio = 2.0f;  // Braids' 96 kHz SVF / this port's rate
const float kKickPunchFreqScale = kKickSvfRateRatio / 8192.0f;
const float kKickPunchDampScale = 1.0f / 8.0f;
const float kKickPunchFloor = 2048.0f / 32768.0f;
const float kKickPunchThreshold = 4096.0f / 32768.0f;

// MACRO's Punch range (THE FOURTH MACRO above); 1.0 at macro=0.5 reproduces
// Braids' one fixed punch level exactly.
const float kKickMaxPunchScale = 4.0f;

// excitation >> 4, the raw exciter blended dry alongside the filtered signal
// (digital_oscillator.cc:2356).
const float kKickExcitationDryMix = 1.0f / 16.0f;

// Stereo mid/side spread (bass_drum_engine.cc's own idiom for this
// instrument family) -- kept small so the low end stays mono-compatible.
const float kKickStereoSpread = 0.2f;

class KickEngine : public Engine {
 public:
  KickEngine() { }
  ~KickEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_KICK; }

 private:
  // Excitation pulses 0 and 1 (digital_oscillator.cc's pulse_[0]/pulse_[1]).
  float pulse0_state_;
  int32_t pulse0_counter_;
  float pulse1_state_;
  int32_t pulse1_counter_;

  // Pulse 2's pure countdown -- its state/level are never read by Braids
  // (only .done()), so only the counter is carried.
  int32_t click_counter_;

  // The Chamberlin SVF's own two states (svf.h's lp_/bp_ -- lp_ is also what
  // PUNCH reads back), and the output one-pole's state.
  float svf_lp_;
  float svf_bp_;
  float lp_state_;

  // ParameterInterpolator targets for the knob-derived quantities.
  float resonance_domain_;
  float tone_coefficient_;
  float punch_scale_;
  float balance_gain0_;
  float balance_gain1_;

  DISALLOW_COPY_AND_ASSIGN(KickEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_KICK_ENGINE_H_
