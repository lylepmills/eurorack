// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' CYMBAL: six comparator-summed square oscillators through a
// resonant bandpass (the "metallic" side) crossfaded against a sample-and-held
// noise word through a resonant highpass (the "raw" side). Free-running --
// there is no envelope or trigger response anywhere in the algorithm.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderCymbal
// (braids/digital_oscillator.cc:2451-2513).
//
// NOT SELF-ENVELOPING, despite how the model is normally heard. RenderCymbal
// never reads `sync` and never checks `strike_` (contrast its two neighbours
// in the same dispatch table: RenderKick re-triggers its excitation pulses
// and its resonator punch on every strike, digital_oscillator.cc:2326-2332,
// and RenderSnare goes further and RETUNES off the played note on each strike
// too, :2399-2418).
// The percussive "hit" character Braids CYMB is known for on the module comes
// entirely from the HARDWARE's own external AD envelope, applied uniformly to
// every macro-oscillator shape outside this function -- render_braids_model.cc
// calls `osc.Strike()` on `--trigger-hz`, and Strike() reaches RenderCymbal's
// `strike_` flag (digital_oscillator.h:284-286) but RenderCymbal never reads
// it, so triggering has NO effect on the rendered audio: CYMBAL is a
// continuous texture, exactly like PARTICLE_NOISE (see particle_burst_engine.h,
// which documents the same free-running behaviour). This port matches that:
// `already_enveloped = false`, so Plaits' own voice-level DECAY/TRIGGER
// envelope is what gives a Cymbal PATCH its triggered, decaying character --
// the same mechanism Fold and Particle Burst already rely on, not something
// unique to this engine.
//
// PARAMETER MAPPING, with the line it was read from.
//   parameter_[0] (TIMBRE) sets the cutoff of BOTH filters, identically
//     (digital_oscillator.cc:2480-2481):
//     `svf_[0].set_frequency(parameter_[0] >> 1); svf_[1].set_frequency(...)`
//     -- a single tone control for the whole instrument, not split per side.
//   parameter_[1] (COLOR) is the crossfade ratio between the two filtered
//     sides (digital_oscillator.cc:2479,2511):
//     `xfade = parameter_[1]; ... hat_noise + ((noise - hat_noise) * xfade >> 15)`
//   Read directly off the array index, same as kick_engine.h notes for its
//   own model -- RenderCymbal has no BEGIN_INTERPOLATE_PARAMETER_N macro at
//   all, so there is no "the suffix is COLOR, not the second parameter" trap
//   here (see fold_engine.h) to misread.
//
// THE METALLIC SIDE. Six phase accumulators (digital_oscillator.cc:2488-2493),
// one per detuned square oscillator, comparator-summed every sample
// (:2495-2503): each contributes 1 if its phase is past the half-cycle point,
// 0 otherwise, the six are summed (0..6), re-centred by -3, and scaled by
// 5461/32768 -- HALF the full int16 range as headroom before the resonant
// filter (Plaits' own SquareNoise, hi_hat.h:47-104, uses the SAME six-square
// comparator idea at FULL range instead; see LINEAGE). The root oscillator's
// frequency is `NoteToFrequency(40 + note/2)` (below); the other five track
// it at FIXED ratios (digital_oscillator.cc:2472-2476, `root * C_k >> 4` with
// `root = increments[0] >> 10`, i.e. ratio_k = C_k / 16384 to a fraction of a
// cent -- verified against the exact two-shift integer arithmetic at
// representative pitches, max deviation under 0.03 cents anywhere a root note
// in the instrument's practical range reaches):
//   ratio_1 = 24273/16384 = 1.481506   ratio_4 = 22452/16384 = 1.370361
//   ratio_2 = 12561/16384 = 0.766663   ratio_5 = 31858/16384 = 1.944458
//   ratio_3 = 18417/16384 = 1.124084
// The sum feeds a bandpass Chamberlin SVF (svf_[0], SVF_MODE_BP,
// digital_oscillator.cc:2457) hardwired to resonance 12000
// (digital_oscillator.cc:2458) -- a value TIMBRE never touches and the module
// exposes no control over at all.
//
// THE RAW SIDE is NOT full-rate white noise. A second phase accumulator
// (`phase_`, distinct from the six above) wraps 24x per cycle of the root
// note (digital_oscillator.cc:2477,2484-2487, `increments[6] = increments[0]
// * 24`); each wrap draws a fresh 32-bit LCG word (`rng_state = rng_state *
// 1664525 + 1013904223`, the Numerical-Recipes constants) and its TOP 16 bits
// become the held sample (digital_oscillator.cc:2507, `(rng_state >> 16) -
// 32768`) until the next wrap -- clocked, sample-and-held noise, not a fresh
// draw every sample. That held sample, halved for headroom, feeds a highpass
// Chamberlin SVF (svf_[1], SVF_MODE_HP) hardwired to resonance 2000
// (digital_oscillator.cc:2461) -- again a constant TIMBRE never touches.
//
// RATE. RenderCymbal ends in a plain `while (size--)` (digital_oscillator.cc:
// 2483), not `size -= 2`: a genuinely 96 kHz-native algorithm (SPEC R5), with
// no further internal oversampling on top (one SVF::Process call per side per
// output sample, no 2x/4x loop the way fold's folders have). The two
// coefficient tables it reads, `lut_svf_cutoff`/`lut_svf_damp`
// (resources.cc:114,181), are generated at `sample_rate = 96000`
// (lookup_tables.py:35,98-104) -- the SAME tables kick_engine.h documents
// needing full re-derivation for, not particle-burst's lucky already-48kHz
// case.
//
// The FIRST version of this port followed kick_engine.h's resolution for
// those same two tables: re-derive the cutoff coefficient at Plaits' own
// 48 kHz rate, `f = 2*sin(pi*f_norm)` with `f_norm = min(cutoff_hz /
// kSampleRate, 1/8)` -- the SAME closed form and the SAME 1/8 clamp fraction
// lookup_tables.py:100 uses, just evaluated at the port's actual rate. That
// measured clean everywhere EXCEPT the one case that exercises it hardest:
// TIMBRE at 1, where the cap engages. There, `1/8 of 48 kHz` is 6 kHz --
// literally HALF of Braids' OWN realized ceiling, `1/8 of 96 kHz` = 12 kHz --
// because kick's PITCH-TRACKED cutoff only nears its 1/8 cap at an extreme
// played note kick_engine.cc's own tests never reach, while TIMBRE=1 is
// Cymbal's ordinary, fully-open "bright hi-hat" setting: the FIRST A/B pass
// measured a resonant peak sitting near 6 kHz against Braids' near 12 kHz,
// +5.9 dB AC RMS and +5.2 dB in the one real-energy band nearest 6 kHz, with
// a matching -1.7 dB deficit in Braids' own peak band near 12 kHz -- a
// textbook "cap re-derived for the wrong rate" signature once isolated
// per-side (debug cases against `--case`, not part of the committed
// tests/ab.json). Transplanting Braids' literal 12 kHz absolute value
// instead (`f_norm = 12000/48000 = 0.25`) was tried next and is WORSE, not
// better: it measured +20 dB AC RMS, the naive/Chamberlin recursion audibly
// unstable at that coefficient. The reason both single-rate options fail is
// the SAME reason fold's folders needed 4x oversampling (fold_engine.h RATE):
// 12 kHz sits at 1/4 of Braids' OWN 48 kHz Nyquist (96 kHz native), a safe,
// accurate operating point for the naive SVF formula; the SAME absolute
// 12 kHz at this port's native 48 kHz rate sits at 1/2 of ITS 24 kHz
// Nyquist -- a fundamentally more extreme position for the identical
// formula, not a rate-conversion rounding error to patch around.
//
// So, like fold, this port 2x OVERSAMPLES the SVF stage to reach Braids' own
// 96 kHz operating rate exactly, rather than re-deriving a coefficient for
// 48 kHz at all: `f_norm = min(cutoff_hz / 96000, 1/8)`, Braids' own closed
// form AND Braids' own sample rate, unmodified. The two comparator/noise
// substeps this needs are cheap to get for free -- the SIX phase
// accumulators, the noise clock, and the LCG draws all already run every
// substep for their OWN reasons (a comb oscillator and a clocked noise
// source, not filter state) -- so the whole excitation chain runs 2x
// alongside the filters rather than being zero-order-held across them the
// way BRAIDS' OWN RenderKick holds its excitation across a twice-per-
// iteration SVF (digital_oscillator.cc:2346-2363; kick's excitation is a
// decaying pulse pair with negligible energy near Nyquist either way, so a
// hold costs it nothing -- which is also why kick_engine.cc could resolve
// its own version of this problem WITHOUT oversampling at all, running one
// re-derived 48 kHz recursion per output sample, kick_engine.h RATE).
// Cymbal's excitation is broadband by construction, so a hold would
// reintroduce the same problem this section exists to fix. The two substeps'
// filtered outputs are averaged into one output-rate sample -- a plain 2-tap
// box decimator, NOT
// Plaits' 8-tap `lut_4x_downsampler_fir` (built for a 4x ratio, not 2x) and
// not a hand-rolled halfband either. What it costs is aliasing folded back
// into the 20.48-24 kHz band, which runs hot by +2.5 to +3.3 dB in the five
// cases whose energy reaches up there (and only +0.3 dB in color-metal, whose
// bandpassed metallic side carries almost nothing that high). That band's
// share of total energy is small in most cases but NOT negligible in all of
// them: 2.5% at stock, 3.0% at COLOR=1, 4.2% high-register, and 12.0% at
// TIMBRE=1, where the cutoff is fully open. A declared deviation
// (tests/ab.json's header note). Everything below it is clean: every
// committed case's overall spectrum figure is under 0.75 dB, AC RMS under
// 0.15 dB, TIMBRE=1 included.
//
// TABLES. No table is embedded. `f = 2*sin(pi*f_norm)` is smooth and
// monotonic across TIMBRE's whole domain, with no near-zero-derivative
// truncation trap the way particle-burst's pole-angle table has, so
// evaluating it continuously is the "substitute the formula" half of SPEC's
// table rule -- now against Braids' OWN 96 kHz rate (RATE above), which
// needs no re-derivation at all, only the oversampling to reach it. What the
// substitution does NOT carry is the table's own uint16 quantization, which
// is coarsest where the entries are smallest: at TIMBRE 0 Braids reads
// lut_svf_cutoff[0] = 17 (int(2*sin(pi*8.176/96000) * 32767) = int(17.5)),
// so its realized coefficient is 17/32768 and the continuous form is +0.27 dB
// above it -- a 7.93 vs 8.18 Hz cutoff, at a corner where both filters are
// closed. The gap falls off fast with TIMBRE (+0.04 dB by 0.4, +0.001 dB at
// the 1/8 cap) and never reaches a band the A/B can see.
// `damp`, by contrast, is NOT a continuous axis: resonance is HARDWIRED to
// two constants (12000, 2000) the module never sweeps, so there is nothing
// to substitute a formula for -- the port instead reads the two REALIZED
// numbers straight off Braids' own compiled `lut_svf_damp` (resources.cc:
// 181) at those exact two indices, via the same Interpolate824 arithmetic
// Svf::Process uses (svf.h:78-82, `Interpolate824(lut_svf_damp, resonance_
// << 17)`), extracted programmatically, not retyped:
//   resonance_=12000 -> index 93, frac 49152/65536 -> raw 14751 -> 0.450165
//   resonance_=2000  -> index 15, frac 40960/65536 -> raw 33089 -> 1.009796
// (raw/32768, the scale `>> 15` actually spends them at -- see the constants)
// (`lut_svf_damp`'s own generating formula, lookup_tables.py:102-104, reuses
// the CUTOFF table's per-index f-value as a per-index stability cap -- a
// build-time coincidence of sharing one array across two independently-swept
// runtime controls, not a runtime coupling between resonance_ and
// frequency_ -- so these two numbers are fixed regardless of TIMBRE.)
//
// Both Chamberlin recursions are float ports of braids/svf.h's exact integer
// topology (`notch = in - bp*damp; lp += f*bp; hp = notch - lp; bp += f*hp`),
// clipping `lp`/`bp` STATE after every step (svf.h:93,96) in addition to the
// caller's own clip of whichever output it reads (digital_oscillator.cc:2505,
// 2509) -- both clips are reproduced, matching kick_engine.cc's own Chamberlin
// port (`CONSTRAIN(svf_lp_,...); CONSTRAIN(svf_bp_,...)`) rather than
// stmlib::NaiveSvf, whose only accurate coefficient path
// (`set_f_q<FREQUENCY_EXACT>`) is a fixed `1/resonance` damp mapping that
// cannot hold Braids' two REALIZED damp constants above. `sinf` is called
// directly (`<cmath>`), matching kick_engine.cc's own coefficient path; the
// fourth root there uses stmlib::Sqrt so it stays one instruction per root on
// Cortex-M4 rather than pulling in bare-metal-incompatible libm `powf`.
//
// PITCH. Only the six-oscillator ROOT frequency tracks the played note
// (`note = (40 << 7) + (pitch_ >> 1)`, digital_oscillator.cc:2468 -- 40
// semitones plus HALF the played note, in Braids' 128-counts-per-semitone
// units). This port computes that root directly through
// `NoteToFrequency(40.0f + parameters.note * 0.5f)`, which folds in
// kCorrectedSampleRate (SPEC R6) the same way kick_engine.h's and
// particle_burst_engine.h's own pitch-tracked components do. TIMBRE's cutoff
// mapping, by contrast, does NOT track the played note at all -- it borrows
// the SAME `440 * 2^((i-69)/12)` curve purely as a convenient nonlinear sweep
// over an unrelated knob, so R6 does not apply there; its denominator is the
// flat `kCymbalOversampledRate` (Braids' own 96 kHz, RATE above), never
// `kCorrectedSampleRate`, and would be a flat rate either way even if this
// port had stayed at a single 48 kHz rate.
//
// LINEAGE. Plaits' own analog-hi-hat (plaits/dsp/engine/hi_hat_engine.cc,
// `HiHat<...>` in plaits/dsp/drums/hi_hat.h) is CYMBAL's refined descendant,
// and the resemblance is real -- both are six-square-comparator "metallic
// noise" through a resonant filter -- but the differences are structural, not
// just retuning:
//   - DIFFERENT PARTIAL RATIOS. HiHat's SquareNoise uses {1.304, 1.466,
//     1.787, 1.932, 2.536} for its five non-root partials (hi_hat.h:65-66);
//     CYMBAL's are {1.482, 0.767, 1.124, 1.370, 1.944} (above) -- a
//     completely different voicing, not a subset or a rescale of one another
//     (CYMBAL's second ratio is even BELOW the root, which HiHat's never is).
//   - THE TWO SIDES ARE FILTERED SEPARATELY. Both models crossfade -- the
//     arithmetic is the same lerp on both sides (CYMBAL
//     digital_oscillator.cc:2511, HiHat hi_hat.h:247 `out[i] += noisiness *
//     (noise_sample_ - out[i])`), and both isolate either end (CYMBAL at
//     COLOR 0/1, HiHat at `noisiness` 0/1). What differs is WHERE the blend
//     sits: CYMBAL gives each side its OWN filter -- a bandpass on the
//     metallic side, a highpass on the raw side -- and crossfades the two
//     FILTERED signals, so COLOR sweeps between two differently-coloured
//     textures. HiHat has one filter chain: the noise is mixed in AFTER the
//     single bandpass (hi_hat.h:231) and before the VCA and output highpass
//     (:255-264), so its `noisiness` sweeps toward RAW noise, never toward a
//     separately-filtered one. HiHat's noise clock rate is also itself
//     noisiness-dependent (`f0 * (16 + 16*(1-noisiness))`, hi_hat.h:238)
//     rather than CYMBAL's fixed 24x-root clock.
//   - CYMBAL IS NOT SELF-ENVELOPING (see the top of this file); HiHat is
//     EXPLICITLY self-enveloping, with its own two-stage VCA decay driven by
//     TRIGGER/DECAY/ACCENT (hi_hat.h:199-265, `envelope_`/`sustain_gain_`).
//   - RESONANCE. CYMBAL hardwires both filters to fixed constants the module
//     never exposes (TABLES above). HiHat's own resonant instance (hi_hat_1,
//     the `resonance=true` template argument) ties Q to `tone`
//     (`resonance ? 3.0f + 3.0f*tone : 1.0f`, hi_hat.h:230); its second
//     instance (hi_hat_2, RingModNoise-driven) hardcodes Q at a flat 1.0 with
//     NO resonance control at all. Neither HiHat instance gives a Q axis
//     independent of tone the way this port's MACRO does (below).
//
// THE FOURTH MACRO.
//   MORPH  Spread   Scales the five non-root partials' distance from unison
//                    (digital_oscillator.cc:2472-2476): 0 collapses all six
//                    oscillators onto the root (a single doubled-amplitude
//                    square wave, no comb structure at all), 0.5 is Braids'
//                    own voicing exactly, 1.0 doubles each ratio's distance
//                    from 1.0. The module's five ratios are hardwired; this
//                    reshapes the comb the module cannot move at all, the
//                    same idiom particle_burst_engine.h's Chord width uses
//                    for ITS three hardwired intervals.
//   MACRO  Resonance Scales BOTH filters' damp coefficient around Braids'
//                    two hardwired constants (TABLES above): 0.5 reproduces
//                    the module exactly (damp_bp=0.450179, damp_hp=1.009827),
//                    0 multiplies damp by 3 (heavily damped, a duller/flatter
//                    texture), 1 multiplies it by 0.2 (near-ringing, pushed
//                    well past anything the module's fixed resonance can
//                    reach). Verified not to diverge at any macro/TIMBRE
//                    corner -- the per-step state clip (above) bounds it by
//                    construction, the same guarantee kick_engine.cc's PUNCH
//                    macro relies on for its own self-modulating filter.
//                    analog-hi-hat gives no comparable single Q axis either
//                    (LINEAGE above).
//
// OUT: the crossfaded pair, exactly Braids' own signal. AUX (mono): the
// complementary crossfade -- COLOR at 0 puts the metallic side on OUT and the
// raw side on AUX; at 1, the reverse -- the same "AUX always favours what OUT
// does not" idiom fold_engine.h documents for its own two-source crossfade.
//
// In stereo, the metallic side (deterministic, comb-structured) stays
// IDENTICAL on both channels. That is a cost choice, not an acoustic one: a
// second bank at a different starting phase vector WOULD decorrelate (the six
// relative phases never converge -- they are oscillators, not filters), it
// just costs six more accumulators and a second bandpass for a side already
// narrow-band enough that the width would be slight. The raw-noise side is
// where the width comes from: it gets a second LCG stream and its own
// highpass state, and -- this is the part that has to be deliberate --
// rng_state_[1] is seeded to a DIFFERENT constant in Reset(). Both channels'
// noise clocks run off the same root note and step in lockstep, so two LCGs
// seeded alike would stay bit-identical forever and the second stream would
// be the first one twice over. The draws themselves still happen only inside
// the stereo branch (the SAME R14 idiom particle_burst_engine.h uses), so
// enabling stereo never perturbs the mono/left noise sequence. On top of that
// sits a small opposing offset on COLOR itself (kCymbalStereoBlend, the same
// idea fold_engine.h's kFoldStereoBlend uses for its own crossfade) so the
// two channels differ in BOTH the noise they carry and how much of it they
// hear.
//
// Declared deviations from Braids:
//   - RenderCymbal never reads sync/strike_ at all -- this port likewise
//     ignores parameters.trigger, matching particle_burst_engine.h's
//     precedent for the same free-running behaviour.
//   - the 2x-to-1x decimator is a plain 2-tap box average, not a real
//     halfband FIR -- +2.5 to +3.3 dB of folded-back aliasing in the
//     20.48-24 kHz band in the five cases with energy up there (+0.3 dB in
//     color-metal, which has almost none there), and
//     that band is 2.5-4.2% of total energy in most cases but 12.0% at
//     TIMBRE=1 (RATE above).
//   - both Chamberlin recursions run in float, so they do not carry Braids'
//     per-step `>> 15` truncation.
//   - MORPH and MACRO are this port's own additions, with no Braids
//     behaviour to match at any position off 0.5.
//
// A/B NOTES. Every committed tests/ab.json case measures AC RMS within
// 0.15 dB and overall spectrum under 0.75 dB, TIMBRE=1 (the cap) included.
// Two of the six cases DO give ab_engine.py's autocorrelator a usable lag --
// stock-mid and color-metal, the two where the comb-structured metallic side
// carries most of the energy -- and both declare a cents tolerance; they
// measure -0.9 and +0.1 cents and are what actually pins the `40 + note/2`
// root mapping. The other four (TIMBRE at either end, COLOR at 1, and the
// high register) report no f0 and declare no cents tolerance: with the
// filtered raw-noise side dominant there is no single fundamental for a
// single-lag autocorrelator to lock onto, the same caveat
// particle_burst_engine.h documents for its own inharmonic source.

#ifndef PLAITS_DSP_ENGINE2_CYMBAL_ENGINE_H_
#define PLAITS_DSP_ENGINE2_CYMBAL_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Root offset, digital_oscillator.cc:2468: `(40 << 7) + (pitch_ >> 1)` is
// 40 semitones plus HALF the played note, in Braids' 128-counts-per-semitone
// units -- i.e. 40.0f + note * 0.5f in semitones.
const float kCymbalRootOffsetSemitones = 40.0f;

// The five non-root partials' ratios to the root, digital_oscillator.cc:
// 2472-2476 (`root * C_k >> 4`, root = increments[0] >> 10, so
// ratio_k = C_k / 16384). MORPH's Spread scales each (ratio - 1.0) around
// unity; 1.0x (MORPH at 0.5) is the module's own voicing.
const float kCymbalPartialRatio1 = 24273.0f / 16384.0f;
const float kCymbalPartialRatio2 = 12561.0f / 16384.0f;
const float kCymbalPartialRatio3 = 18417.0f / 16384.0f;
const float kCymbalPartialRatio4 = 22452.0f / 16384.0f;
const float kCymbalPartialRatio5 = 31858.0f / 16384.0f;

// digital_oscillator.cc:2477: the raw-noise side's clock fires 24x per cycle
// of the root note.
const float kCymbalNoiseClockMultiplier = 24.0f;

// Comparator-sum scale, digital_oscillator.cc:2503 (`hat_noise *= 5461`),
// normalized: 5461/32768, half of full scale for headroom ahead of the
// bandpass.
const float kCymbalMetallicScale = 5461.0f / 32768.0f;

// TIMBRE -> cutoff table index, digital_oscillator.cc:2480 (`parameter_[0] >>
// 1`) then Interpolate824's top-8-bit indexing (`>> 7` more, svf.h:80-81,
// `frequency_ << 17` then `>> 24`): combined, index = parameter_[0] / 256,
// continuous as TIMBRE * 32767 / 256.
const float kCymbalToneIndexRange = 32767.0f / 256.0f;

// The cutoff table's own closed form, lookup_tables.py:98
// (`440 * 2^((i-69)/12)`), and its 1/8-of-sample-rate clamp (lookup_tables.py
// :99-100), evaluated against kCymbalOversampledRate below -- Braids' OWN
// rate, unmodified. See the header's RATE section for why a single-rate
// re-derivation of this cap (against kSampleRate, 48 kHz) measures badly
// and this port oversamples instead. This axis does not track pitch
// (PITCH above), so it is a flat rate either way, never
// kCorrectedSampleRate.
const float kCymbalCutoffCapFraction = 1.0f / 8.0f;

// The SVF recursion runs 2x oversampled to reach this rate -- Braids' own
// 96 kHz operating rate, unmodified (see the header's RATE section).
const float kCymbalOversampledRate = 96000.0f;

// The two REALIZED lut_svf_damp values Braids reads at its two hardwired
// resonance_ settings (digital_oscillator.cc:2458,2461), extracted via the
// exact Interpolate824 arithmetic against resources.cc:181 -- see the
// header's TABLES section for the index/frac these came from.
// The raw table words are divided by 32768, not 32767: Svf::Process spends
// them as `bp_ * damp >> 15` (svf.h:91), so the realized float coefficient is
// word/2^15 regardless of the 32767 scale the table was BUILT with
// (lookup_tables.py:111).
const float kCymbalDampBandpassStock = 14751.0f / 32768.0f;
const float kCymbalDampHighpassStock = 33089.0f / 32768.0f;

// MACRO's Resonance range: multipliers on the two damp constants above.
// 3.0x at MACRO 0 (well damped), 0.2x at MACRO 1 (near-ringing) -- verified
// not to diverge at either extreme combined with TIMBRE's own extremes (the
// per-step state clip in the .cc bounds it regardless).
const float kCymbalResonanceMinScale = 3.0f;
const float kCymbalResonanceMaxScale = 0.2f;

// MORPH's Spread range: 0x (unison) through 1.0x (the module, at MORPH 0.5)
// to 2.0x (double the module's spread).
const float kCymbalSpreadMax = 2.0f;

// The opposing offset stereo applies to COLOR itself, on top of giving the
// raw-noise side its own independent RNG stream -- the same small-splay idea
// fold_engine.h's kFoldStereoBlend uses for its own two-source crossfade.
const float kCymbalStereoBlend = 0.15f;

class CymbalEngine : public Engine {
 public:
  CymbalEngine() { }
  ~CymbalEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_CYMBAL; }

 private:
  // The six comparator phases (root + five detuned partials), shared by both
  // stereo channels -- see the header's stereo note for why this side is
  // never duplicated.
  float partial_phase_[6];

  // Bandpass (metallic) Chamberlin state, one instance, shared.
  float bp_lp_;
  float bp_bp_;

  // The raw-noise side: clock phase, LCG state, and highpass Chamberlin
  // state, duplicated per channel (index 1 used only in stereo, R14).
  float noise_clock_[2];
  uint32_t rng_state_[2];
  float hp_lp_[2];
  float hp_bp_[2];

  // Smoothed crossfade (COLOR).
  float xfade_;

  DISALLOW_COPY_AND_ASSIGN(CymbalEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_CYMBAL_ENGINE_H_
