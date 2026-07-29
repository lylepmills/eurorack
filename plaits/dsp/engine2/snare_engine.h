// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SNAR: two decaying excitation pulses into two fixed-interval
// resonant bandpasses (the "tone"), plus a decaying noise burst into a
// third, high, fixed bandpass (the "snap"), all self-enveloping and summed.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderSnare
// (braids/digital_oscillator.cc:2368-2449).
//
// LINEAGE. Plaits' own analog-snare (plaits/dsp/engine/snare_drum_engine.cc,
// wrapping AnalogSnareDrum + SyntheticSnareDrum) is the module's refined
// descendant for this instrument family, and it is a different synthesis
// mechanism, not a retuned copy. analog-snare's AnalogSnareDrum runs a bank
// of `kNumModes` (analog_snare_drum.h) partials continuously spread by a
// `mode_spread` macro and an explicit, continuously live `decay` parameter
// (analog_snare_drum.h:73,78-81) -- decay and tone balance are independent,
// smoothly variable knobs with no coupling to the played note. SNAR instead
// runs exactly TWO tonal resonators at hardwired +12 and +24 semitone
// NOMINAL offsets (digital_oscillator.cc:2420-2421) plus one noise resonator
// at a nominal +60 (:2422) -- a fixed three-partial voicing, not a spread
// bank. Read WHERE THEY ACTUALLY LAND, those nominal offsets are all an
// octave high: lut_svf_cutoff is generated at sample_rate = 96000
// (lookup_tables.py:35,99) while RenderSnare steps each Svf once per TWO
// 96 kHz output samples (RATE below), so a coefficient written for f lands
// at f/2 and the three resonators sound at the NOTE ITSELF, an octave above
// it, and +48. Measured on the reference render at note 45 (A2, 110 Hz):
// peaks at 109 Hz and 221 Hz, not 220 and 440. That octave shift is Braids'
// own behaviour and is reproduced here verbatim (the port reads the same
// table constant at the same call cadence, so it lands in the same place);
// every "+12/+24/+60" in this file therefore names the SOURCE CONSTANT, and
// the audible relationships are note / +12 / +48. SNAR's decay, in turn, is
// not an independent control at all: it is DERIVED once per strike from the
// played pitch and from the top half of COLOR
// (:2400-2404, see PARAMETER MAPPING), then held fixed for the rest of the
// hit. A higher note strikes shorter, by construction, and nothing about
// that coupling can be dialled out while still resembling the module. So the
// two do not overlap: analog-snare is a continuously variable, decoupled
// modal drum; SNAR is a fixed-interval three-partial drum whose decay time
// is a side effect of pitch and color, snapshotted at the strike -- and that
// snapshot-at-strike behaviour, absent from analog-snare entirely, is what
// this port's MACRO spends its own axis extending (THE FOURTH MACRO, below).
//
// PARAMETER MAPPING, with the line it was read from. RenderSnare reads both
// parameters directly, with no BEGIN_INTERPOLATE_PARAMETER_N macro at all
// (digital_oscillator.cc:2412,2424-2425) -- so, as with kick_engine.h, there
// is no INTERPOLATE_PARAMETER_1 "reads parameter_[1], i.e. COLOR" trap to
// misread here (see fold_engine.h): the array index IS the read.
//
//   parameter_[0] (TIMBRE) sets the LIVE balance between the two tonal
//     resonators (:2424-2425): `g_1 = 22000 - (TIMBRE>>1); g_2 = 22000 +
//     (TIMBRE>>1)`, recomputed every Render() call from the CURRENT knob
//     position -- unlike everything COLOR touches (below), this one keeps
//     tracking TIMBRE live through a decaying hit. g_1 weights the +12
//     resonator, g_2 the +24 one; TIMBRE never reaches a pure +12-only mix
//     (g_1's floor is 5617/32768, not 0) but sweeps from equal (TIMBRE=0)
//     toward mostly-+24 (TIMBRE=1, g_1:g_2 about 1:6.8).
//
//   parameter_[1] (COLOR) is read in THREE places, all inside `if (strike_)`
//     (:2399-2418) -- meaning all three are SNAPSHOTTED at the trigger
//     instant and then held fixed for the rest of the hit, in contrast to
//     TIMBRE's live tracking above:
//       (a) `pulse_[3].Trigger(512 + (snappy << 1))` (:2416), snappy =
//           min(COLOR, 14336) (:2413-2415) -- the PEAK AMPLITUDE of the
//           noise burst that drives the snap resonator. This is COLOR's
//           whole effect over its BOTTOM half (COLOR < 0.5): louder snap,
//           nothing else.
//       (b) over COLOR's TOP half only (COLOR >= 0.5, the `>= 16384` branch,
//           :2401), COLOR ALSO adds `COLOR - 16384` on top of a decay value
//           driven primarily by pitch (:2400) -- see (c) and the paragraph
//           under it.
//       (c) that same `decay` value sets pulse_[3]'s own decay coefficient
//           (:2407) -- the length of the snap's own ring, not just its
//           level.
//     `decay = 49152 - pitch_ [+ COLOR's top-half contribution], clamped to
//     65535` (:2400-2404) additionally sets the TONAL resonators' resonance
//     (:2405-2406, `29000 + (decay>>5)` and `26500 + (decay>>5)`) -- so a
//     higher note (bigger `pitch_`, smaller `decay`) gives BOTH a shorter
//     snap ring AND slightly less resonant (shorter-ringing) tonal
//     partials, and the top half of COLOR stretches both back out again.
//     This coupling is real Braids behaviour, snapshotted once per hit, and
//     is reproduced as such -- not smoothed, not decoupled.
//
// RATE. RenderSnare ends in `size -= 2` (:2447), writing the SAME computed
// sample twice (:2445-2446) -- a literal zero-order-hold duplication, the
// same shape particle_burst_engine.h documents, NOT a linear interpolation
// (contrast bowed_engine.h) and NOT Kick's "duplicated write but the filter
// still advances every sample" case (kick_engine.h). Every stateful call in
// the loop body -- all four pulse_[].Process() calls AND all three
// svf_[].Process() calls -- happens exactly ONCE per iteration, so the
// WHOLE algorithm, resonators included, runs at Braids' 48 kHz-equivalent
// cadence (`size` decrements by 2 per iteration over a 96 kHz buffer, so
// `size/2` calls per second). That cadence already equals Plaits' own
// native render rate one-for-one -- unlike Kick, whose SVF calls twice per
// iteration to reach a genuinely separate, faster internal rate (see
// kick_engine.h's RATE section), SNAR's resonators need no oversampling and
// no rate re-derivation: this port calls each svf.Process() exactly once
// per OUTPUT sample, matching Braids' own call cadence exactly.
//
// Because that cadence already matches, the coefficient tables
// (lut_svf_cutoff/lut_svf_damp) transfer at their OWN generating constant,
// 96000 (braids/resources/lookup_tables.py:35, in effect at :99 -- NOT
// halved the way particle-burst's resonator table is at
// lookup_tables.py:63, and NOT re-derived at Plaits' own rate the way
// kick_engine.cc's KickResonanceDomain deliberately is: kick's SVF runs at
// Braids' native 96 kHz, a real rate this port doesn't reach without
// oversampling, so kick's coefficients are an accepted approximation; SNAR's
// SVF never left 48 kHz-equivalent on either side, so there is nothing to
// approximate). Verified against the actual generated tables in
// braids/resources.cc (see TABLES). Note the audible consequence, spelled
// out in LINEAGE: a table generated for 96 kHz driving a filter stepped at
// 48 kHz puts every resonance an octave BELOW its nominal offset. That is
// Braids', not a port artefact -- transferring the constant verbatim is what
// reproduces it.
//
// TABLES. No table is embedded. lut_svf_cutoff/lut_svf_damp
// (braids/resources/lookup_tables.py:98-112) are re-derived from their
// closed forms and evaluated continuously in float rather than through
// Braids' own int16 quantisation of the two Interpolate824 index reads
// (svf.h:80-81) -- the same "smooth formula, not a table with a cusp"
// substitution kick_engine.h's TABLES note makes for the identical table.
// Reproduced programmatically against every one of the 257 entries in
// braids/resources.cc:
//   lut_svf_cutoff[i]: f = 2*sin(pi*min(440*2^((i-69)/12)/96000, 1/8)),
//     value = f*32767 -- max deviation 0.99 (< 1 LSB) across all 257.
//   lut_svf_damp[i]: value = 2*(1 - (i/260)^0.25)*32767 -- max deviation
//     1.00 (< 1 LSB) across all 257, using ONLY the resonance term.
//     lookup_tables.py:104's frequency-ceiling clamp,
//     `min(2, 2/f[i] - f[i]*0.5)`, is part of the generator formula but
//     NEVER binds anywhere in the actual table: `f[i]` saturates to the 1/8
//     ceiling for every i above ~127 (the same saturation the cutoff table
//     shows), and at that ceiling the clamp term evaluates to ~2.230, which
//     the generator's own `min(2, ...)` immediately caps to exactly 2.0 --
//     a value the resonance term `2*(1-r^0.25)` never reaches for r < 1.
//     Below i=127 the clamp term is even larger. So SvfDamp() below omits
//     it entirely; this is verified against the real table, not assumed.
//     This matters here specifically because SNAR always drives the tonal
//     resonators' resonance codes into the 27000-31000+ range (see PARAMETER
//     MAPPING) -- an operating point where, had the clamp ever bound, it
//     would have bound hardest.
//
// SELF-STRIKE. DigitalOscillator::Init() sets strike_ = true
// (digital_oscillator.h:256), and DigitalOscillator::Render() calls Init()
// on every shape change (digital_oscillator.cc:111-115) -- so a freshly
// selected SNAR fires once immediately,
// with no external trigger, using whatever pitch/COLOR are current at that
// moment. This port follows struck_drum_engine.cc's established idiom for
// that behaviour: `self_strike = unpatched && !ever_struck_`, so an
// unpatched TRIGGER input fires exactly one hit on load and stays silent
// after, rather than free-running.
//
// THE FOURTH MACRO. Braids' two knobs map straight across --
// Braids-TIMBRE-to-Plaits-TIMBRE, Braids-COLOR-to-Plaits-HARMONICS -- the
// same convention fold_engine.h and kick_engine.h both use. MORPH and MACRO
// are this port's own additions, chosen to extend exactly what SNAR does
// that analog-snare cannot (LINEAGE, above) rather than duplicate
// analog-snare's own MORPH=decay / HARMONICS=snappy axes:
//
//   MORPH   Snap balance   Crossfades the tonal sum (both resonators, at
//                          their TIMBRE-set balance) against the snap
//                          resonator, kick_engine.h's own MORPH idiom: 0.5
//                          leaves both at Braids' native unweighted sum (the
//                          module exactly, since Braids adds the snap
//                          resonator's output into the mix with no gain or
//                          dry-mix term of its own), 0 fades the snap out
//                          entirely (a pure two-partial tone Braids cannot
//                          isolate on its own), 1 fades the tone out,
//                          leaving only the snap component -- which AUX
//                          already exposes on its own (below), so this is a
//                          second way to reach the same material through
//                          OUT alone when AUX is not patched.
//   MACRO   Spread          Scales the two tonal resonators' FIXED +12/+24
//                          semitone source offsets around Braids' own
//                          hardwired 1x (:2420-2421): 0.5 reproduces the
//                          module's own voicing exactly (audibly the note
//                          and an octave above it -- LINEAGE), 0 collapses
//                          both resonators into UNISON an octave below the
//                          played note (not reachable on the module), 1
//                          doubles the source offsets to +24/+48, i.e. two
//                          octaves apart. The noise resonator's own fixed
//                          +60 source offset (:2422) is NOT touched by
//                          this control -- Spread is specifically about the
//                          two-partial "chord" SNAR's tone is built from,
//                          the one piece of this model with no equivalent
//                          on analog-snare at any position (LINEAGE). Read
//                          at BLOCK rate, not smoothed per sample like the
//                          other three macros -- see the .cc's Render(),
//                          which explains the CPU reasoning (three sinf
//                          calls per sample, one per resonator, were the
//                          most expensive thing in the loop).
//
// OUT: the full self-enveloping mix (Braids' own `sd`, digital_oscillator.
// cc:2439-2443). AUX (mono): the snap resonator's own output alone,
// soft-clipped -- a signal Braids sums into `sd` unconditionally and never
// exposes on its own (:2442, `sd += svf_[2].Process(noise_sample);`, no gain
// or dry-mix term unlike the two tonal terms above it).
//
// In stereo, OUT/AUX become a mid/side pair -- kick_engine.h's own idiom for
// this reason: AUX here is a COMPONENT already summed into OUT (like Kick's
// raw-excitation AUX), not an independently complete second voice, which is
// what would justify the native SnareDrumEngine's equal-pan-two-full-voices
// approach instead (it panels two COMPLETE snares, analog and synthetic,
// against each other -- a different relationship). So: the full mix stays
// centred and a small soft-clipped slice of the snap component spreads as
// an L-R difference.
//
// A/B NOTES (measured; tests/ab.json has the exact cases and numbers).
// All six cases pass at the guide's standard targets: AC RMS +0.01 to
// +0.82 dB, spectrum 0.19 to 0.67 dB. On the five note-45 cases EVERY band
// below 10 kHz now sits within 0.3 dB, and what remains is a single residual
// with a closed form: the top-octave tilt particle_burst_engine.h documents.
// Braids' reference is the SAME sample written twice at 96 kHz (RATE above)
// and then decimated, so the reference carries the duplicate-write's own
// in-band response |cos(pi*f/96000)| where this port computes v[n] directly.
// That predicts the port reading HIGH by 1.09 dB at 15 kHz and 3.0 dB at
// 22 kHz; measured, across all six cases, +1.03 to +1.12 dB in the
// 10.24-20.48 kHz band and +3.04 to +3.10 dB in 20.48-24 kHz (the note-78
// case, whose resonators sit higher, reads lower still: +0.34 and +2.35).
// The residual is therefore fully accounted for -- there is no unexplained
// term left, and no case needs a widened tolerance.
//
// This was NOT the situation before the excitation truncation went in.
// Reproducing Braids' `state_ = state_ * decay_ >> 12` floor (see the .cc's
// SnarePulse::Process) moved every case at once: stock-mid 1.12 -> 0.39 dB
// spectrum, snap-low 0.55 -> 0.19, snap-high 1.72 -> 0.42, balance-low
// 1.10 -> 0.39, balance-high 1.15 -> 0.40, high-note-shorter-decay 0.94 ->
// 0.67 with its AC RMS 1.06 -> 0.82 dB. Roughly half of what an earlier
// draft of this file attributed to the zero-order-hold tilt was in fact
// that missing quantisation ringing the snap envelope on ~29 dB past where
// Braids stops it -- the "used the mathematically correct value where
// Braids uses a quantised one" trap, caught by the fact that the measured
// tilt did not match the |cos| prediction until the floor was added. The
// tilt figures above are the check that nothing else is hiding under it.
//
// `high-note-shorter-decay` (note 78) is the loosest case at +0.82 dB AC
// RMS, and the residual grows smoothly with note (+0.07 dB at note 45).
// What is left there is the per-step `>> 15` truncation inside Braids' own
// Svf, which this port does not reproduce (deviations below): a higher
// note's resonators run at proportionally larger `f` coefficients, so the
// same per-step precision loss integrates over fewer, faster-changing
// samples and shortens Braids' hit slightly more than this port's. It is
// not a spectral-SHAPE error (0.67 dB spectrum on that same case).
//
// Declared deviations from Braids:
//   - lut_svf_cutoff/lut_svf_damp are re-derived from their closed forms in
//     continuous float rather than read through Braids' own int16
//     quantisation (TABLES above); reproduces to < 1 LSB.
//   - the Chamberlin recursion (svf.h:91-96) runs in float, so it does not
//     carry Braids' per-step `>> 15` truncation of the two state updates
//     (A/B NOTES above). The EXCITATION's own `>> 12` truncation IS
//     reproduced -- it is not a sub-LSB detail there, see the .cc.
//   - TIMBRE's balance formula (:2424-2425) is evaluated continuously in
//     float rather than through Braids' own int16 truncation of the knob
//     position itself -- a sub-LSB difference in the knob's own
//     quantisation, not in any table read. COLOR's own Q15 code IS floored
//     (matching parameter_[1]'s true int16 nature), and the `decay >> 5` /
//     `decay >> 14` right shifts feeding resonance and pulse 3's decay
//     coefficient ARE reproduced as exact floor/truncation, not smoothed --
//     unlike TIMBRE's shift, `decay >> 14` sits within a few parts in 4096
//     of 1.0 (a near-unity, very slowly decaying one-pole), so continuous
//     substitution there measurably lengthens the decay; see the .cc.
//   - the SVF duplicate-write-then-decimate top-octave tilt and the
//     high-note active-region RMS growth (A/B NOTES above).
//   - MORPH and MACRO are this port's own additions (THE FOURTH MACRO
//     above), with no Braids behaviour to match at any position off 0.5.

#ifndef PLAITS_DSP_ENGINE2_SNARE_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SNARE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// lut_svf_cutoff/lut_svf_damp's own generating constant and ceiling,
// braids/resources/lookup_tables.py:35,99,100 -- in effect for BOTH tables
// (:98-112 sit before the file reassigns sample_rate at :254). See RATE:
// this transfers verbatim because SNAR's resonators never leave Braids'
// own 48 kHz-equivalent call cadence.
const float kSnareSvfLutSampleRate = 96000.0f;
const float kSnareSvfMaxNormalizedFrequency = 0.125f;

// Braids' int16 sample clip (stmlib/stmlib.h:39, CLIP), applied to the
// output sum (digital_oscillator.cc:2443) and, inside each Svf, to its own
// lp_/bp_ states (svf.h's CLIP(lp_)/CLIP(bp_)).
const float kSnareClip = 32767.0f / 32768.0f;

// Excitation pulse timing, digital_oscillator.cc:2373-2386 -- delays in
// ticks at the 48 kHz-equivalent cadence (RATE above), decays as Braids'
// own decay/4096 fraction. Pulse 3's decay is NOT fixed here -- it is
// snapshotted at every strike (PARAMETER MAPPING) -- so only its delay
// (always 0, :2386) is a compile-time constant.
const int32_t kSnarePulse0DelayTicks = 0;   // :2374
const float kSnarePulse0Decay = 1536.0f / 4096.0f;   // :2375
const int32_t kSnarePulse1DelayTicks = 48;  // 1e-3 * 48000, :2378
const float kSnarePulse1Decay = 3072.0f / 4096.0f;   // :2379
const int32_t kSnarePulse2DelayTicks = 48;  // 1e-3 * 48000, :2382
const float kSnarePulse2Decay = 1200.0f / 4096.0f;   // :2383
const int32_t kSnarePulse3DelayTicks = 0;   // :2386

// Strike-time trigger levels and holds, digital_oscillator.cc:2409-2416,
// normalized into this port's unity-full-scale float domain (raw/32768).
// Pulse 0/1 levels are FAR past unity by construction -- Braids slams the
// tonal resonators to their own clip on every hit regardless of decay
// shape, the same deliberate-overdrive idiom kick_engine.h documents for
// its own pulse 0.
const float kSnarePulse0Level = 15.0f * 32768.0f / 32768.0f;   // :2409
const float kSnarePulse1Level = -1.0f * 32768.0f / 32768.0f;   // :2410
const float kSnarePulse1Hold = 2621.0f / 32768.0f;             // :2431
const float kSnarePulse2Level = 13107.0f / 32768.0f;           // :2411
const float kSnarePulse2Hold = 13107.0f / 32768.0f;            // :2435,
    // the SAME literal as pulse 2's own trigger level (:2411) -- not a
    // shared constant in the source, just the same number appearing twice.
const float kSnareSnappyCeiling = 14336.0f;   // :2413-2415, clamps COLOR's
    // Q15 code (0..32767) before it becomes pulse 3's level, below.
const float kSnarePulse3LevelBase = 512.0f / 32768.0f;   // :2416
const float kSnarePulse3LevelSlope = 2.0f / 32768.0f;    // :2416, `<< 1`

// The strike-time `decay` computation, digital_oscillator.cc:2400-2407,
// kept in Braids' own Q7-pitch-scaled integer domain (pitch_ = note * 128)
// so every constant below transfers at its literal source value.
const float kSnareDecayPitchBase = 49152.0f;        // :2400
const float kSnareColorDecayThreshold = 16384.0f;   // :2401
const float kSnareDecayMax = 65535.0f;              // :2402-2404
const float kSnareTone0ResonanceBase = 29000.0f;    // :2405
const float kSnareTone1ResonanceBase = 26500.0f;    // :2406
const float kSnareResonanceDecayScale = 1.0f / 32.0f;   // `>> 5`, :2405-2406
const float kSnarePulse3DecayBase = 4092.0f;        // :2407
const float kSnarePulse3DecayScale = 1.0f / 16384.0f;   // `>> 14`, :2407
const float kSnarePulse3DecayNormalize = 1.0f / 4096.0f;   // Excitation's
    // own decay/4096 fraction (excitation.h:66), applied on top of the
    // strike-time coefficient above.

// The noise resonator's own resonance -- fixed once at Init and never
// touched again (digital_oscillator.cc:2393), unlike the two tonal
// resonators' resonance, which the strike-time decay above sets fresh
// every hit.
const float kSnareNoiseResonanceCode = 2000.0f;   // :2393

// The three resonators' fixed SOURCE semitone offsets, digital_oscillator.
// cc:2420-2422. These are the constants Braids writes; because the
// coefficient table is generated at 96 kHz and stepped at 48 kHz-equivalent,
// each resonator SOUNDS an octave below its offset here -- at the note
// itself, +12, and +48 (LINEAGE). Braids' own pitch clamp
// (:44,120-124, kHighestNote = 140*128) applies to the BASE note before
// these offsets are added, exactly as Braids clamps pitch_ before using it.
const float kSnareTone0SemitoneOffset = 12.0f;
const float kSnareTone1SemitoneOffset = 24.0f;
const float kSnareNoiseSemitoneOffset = 60.0f;
const float kSnareMinNote = 0.0f;
const float kSnareMaxNote = 140.0f;

// TIMBRE's live balance formula, digital_oscillator.cc:2424-2425, in this
// port's unity-scale gain domain (raw/32768).
const float kSnareBalanceBase = 22000.0f / 32768.0f;
const float kSnareBalanceSpan = 16383.5f / 32768.0f;   // TIMBRE(Q15) >> 1,
    // continuous in float (kick_engine.h's TABLES note: a plain halving,
    // no cusp to reproduce) -- max TIMBRE(1.0) gives 16383.5, a half-LSB
    // above Braids' own integer floor(32767/2) = 16383.

// The dry-mix term each tonal resonator's output is summed with before the
// TIMBRE gain, digital_oscillator.cc:2440-2441 (`excitation >> 4`).
const float kSnareDryMix = 1.0f / 16.0f;

// THE FOURTH MACRO (header prose above). Spread's range around Braids'
// own 1x; Snap balance reuses kick_engine.h's own 0..2 crossfade shape.
const float kSnareMaxPartialSpread = 2.0f;

// Stereo mid/side spread (kick_engine.h's own idiom for this "AUX is a
// component of OUT" relationship, LINEAGE above).
const float kSnareStereoSpread = 0.18f;

class SnareEngine : public Engine {
 public:
  SnareEngine() { }
  ~SnareEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_SNARE; }

 private:
  bool ever_struck_;

  // Excitation pulses 0-3 (digital_oscillator.cc's pulse_[0..3]). Pulse 3's
  // decay coefficient is a per-strike snapshot (pulse3_decay_ below), not a
  // compile-time constant like the other three.
  float pulse0_state_;
  int32_t pulse0_counter_;
  float pulse0_level_;
  float pulse1_state_;
  int32_t pulse1_counter_;
  float pulse1_level_;
  float pulse2_state_;
  int32_t pulse2_counter_;
  float pulse2_level_;
  float pulse3_state_;
  int32_t pulse3_counter_;
  float pulse3_level_;
  float pulse3_decay_;

  // The three Chamberlin SVFs' own states (svf.h's lp_/bp_), all BP mode.
  float svf0_lp_;
  float svf0_bp_;
  float svf1_lp_;
  float svf1_bp_;
  float svf2_lp_;
  float svf2_bp_;

  // Strike-time snapshots (PARAMETER MAPPING) -- resonance domains for the
  // two tonal resonators, held fixed between strikes.
  float tone0_resonance_domain_;
  float tone1_resonance_domain_;

  // ParameterInterpolator targets for the LIVE knob-derived quantities.
  float gain0_;
  float gain1_;
  float tone_mix_gain_;
  float noise_mix_gain_;
  float partial_spread_;

  DISALLOW_COPY_AND_ASSIGN(SnareEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SNARE_ENGINE_H_
