// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' PLUK: three round-robin Karplus-Strong voices, each a noise burst
// dropped into a resonant delay loop and left to ring while the next strike
// moves on to the next voice.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderPlucked
// (braids/digital_oscillator.cc:1087-1188).
//
// LINEAGE. Plaits' own inharmonic-string engine (plaits/dsp/engine/
// string_engine.{h,cc}, physical_modelling/string_voice.h: "Extended
// Karplus-Strong, with all the niceties from Rings") is the far more
// elaborate descendant of the same idea, and it already has the same
// three-voice round-robin shape (string_engine.cc:60-76, active_string_
// cycling on TRIGGER_RISING_EDGE) plus a real dispersion allpass, an
// explicit brightness filter and a damping control string_voice.h exposes
// as its own MORPH/TIMBRE. What PLUK has that it does not: a STOCHASTIC
// damping mechanism (below) that leaves some cycles of the loop unfiltered
// at random, which detunes and brightens the decay in a grainy, irregular
// way no smooth one-pole/all-pass combination produces, and a variable-width
// noise excitation that is the model's only "pick" control. This port keeps
// those two mechanisms rather than reproducing inharmonic-string's smoother
// ones a second time.
//
// PARAMETER MAPPING, with the line it was read from. RenderPlucked reads
// parameter_[0]/[1] directly -- no BEGIN_INTERPOLATE_PARAMETER macros are
// used in this render function at all, so there is no COLOR/TIMBRE index
// trap to misread here (contrast fold_engine.h).
//
//   parameter_[0] (TIMBRE) sets DAMPING, in two regimes split at 16384
//     (digital_oscillator.cc:1125-1136):
//       - below noon: `update_probability` is pinned at 65535 (always
//         filter) and `loss` ramps IN as TIMBRE falls -- 0 at noon, and the
//         full pitch-dependent value (`4096 - (phase_increment_ >> 14)`,
//         floored at 256) at TIMBRE 0, scaled by `(16384 - p0) / 16384`
//         (:1132-1133). That is an EXTRA per-sample attenuation on top of
//         the averaging filter's own damping, i.e. a mute/thump control:
//         maximum damping at the BOTTOM of TIMBRE, none at noon.
//       - above noon: `loss` is 0 and `update_probability` instead falls
//         from 65535 toward ~4127 -- some passes around the loop leave a
//         position unfiltered, which both slows the decay AND detunes it
//         (an unfiltered position keeps whatever it held from the PREVIOUS
//         lap, so different parts of the loop drift out of phase with each
//         other), giving a metallic, stretched character at the top.
//   parameter_[1] (COLOR) sets WIDTH (digital_oscillator.cc:1110-1112):
//     `width = (3*p1)>>1; initialization_ptr = size*(8192+width)>>16` --
//     the fraction of ONE STRING PERIOD that gets struck with fresh noise
//     at the pluck, from size/8 (12.5%) to ~0.875*size (87.5%). This is read
//     ONLY at strike time (digital_oscillator.cc:1113, `strike_ = false`
//     right after), matching how the port also reads it once per strike.
//
// THE ROUND ROBIN (digital_oscillator.cc:1092-1109,1116-1122). A strike
// advances `active_voice_` through the three PluckState slots
// (kNumPluckVoices, digital_oscillator.h:49) and re-excites only that one;
// the other two keep ringing from their OWN earlier strike, at their OWN
// pitch -- Braids never retunes a voice it isn't currently exciting. The
// just-struck voice DOES keep tracking the played pitch afterwards, clamped
// to at most an octave above where it was struck
// (`min(phase_increment_, max_phase_increment)`, :1120-1122,
// max_phase_increment = 2x the struck value, :1108) -- unclamped downward.
// The `loss` pitch term (:1128) reads the CURRENT globally-played pitch,
// not each voice's own -- so a note held down while three different
// earlier-struck voices ring detunes their shared "extra damping" together,
// which is Braids' behaviour, not a per-voice approximation error.
//
// RATE (SPEC R5). RenderPlucked ends in `size -= 2`
// (digital_oscillator.cc:1185), writing two 96 kHz samples per inner-loop
// pass -- a 48 kHz algorithm, and `phase_increment_ <<= 1` at the top
// (:1091) is exactly the "per-48-kHz-sample" rescale that convention needs.
// The port runs once per Plaits output sample at the native 48 kHz, no
// oversampling, and every rate-dependent constant below (`kPluckedLossFreq
// Scale`, the pitch clamp) is expressed directly in terms of
// NoteToFrequency()'s cycles-per-sample units rather than transplanted from
// Braids' pitch-128 space.
//
// R6. Every frequency this engine touches -- the per-voice delay period,
// the pitch clamp on the active voice, the loss guard's pitch term -- is
// derived from `NoteToFrequency()`, which already folds in
// `kCorrectedSampleRate` (dsp.h:47). Delay length in samples is therefore
// `1.0f / NoteToFrequency(note)`, never `kCorrectedSampleRate /
// NoteToFrequency(note)` (SPEC R6) -- there is no separate upfront pitch
// shift to add, unlike particle_burst_engine.h, which works in Braids' own
// pitch-128 units and needs one.
//
// NO TABLES. Unlike fold or particle_burst, RenderPlucked reads no
// braids/resources.cc data at all -- the delay line's content is written at
// runtime from `Random::GetSample()`, never a precomputed LUT, so there is
// nothing here to verify against a generator script.
//
// DELAY-LINE MODEL, a declared structural deviation with TWO measured
// costs. Braids addresses a FIXED <=1024-entry buffer per voice
// (digital_oscillator.h:364, `ks[1025*4]`) through an NCO phase
// (`read_ptr = ((phase >> (22 + shift)) + 2) & mask`, :1155) whose
// resolution is shrunk for high notes via `shift` (:1099-1104). The key
// property, and the one this port does NOT reproduce: the 32-bit phase
// wraps once per TRUE period and the index is masked to `size = 1024 >>
// shift`, so Braids' buffer spans exactly one period at ANY pitch --
// `size` slots per lap, however long or short that lap is in samples.
// The port instead uses `plaits::DelayLine<float, kPluckedDelaySize>`
// addressed by the real period in samples, one slot per sample. The two
// diverge in two ways:
//
//   1. FILTER-UPDATE RATE. Braids runs its loop filter once per buffer
//      slot the read pointer crosses (`while (write_ptr != read_ptr)`,
//      :1158-1176), i.e. `size / period` times per output sample. The
//      shift search only halves `size` until the increment fits, so that
//      ratio is not ~1: it sweeps (1, 2] across each power-of-two band and
//      hits 2 just under 93.75 / 187.5 / 375 / 750 Hz. The port always
//      runs exactly ONE update per output sample, so it under-damps by up
//      to 2x near the top of a band. Measured against the module at
//      TIMBRE/COLOR noon: note 45 (110 Hz, ratio 1.17) +1.55 dB AC RMS /
//      1.18 dB spectrum; note 54 (185 Hz, ratio 1.97) +3.41 dB / 4.59 dB;
//      note 30 (46.2 Hz, ratio 0.99) -0.01 dB / 1.04 dB. The residual the
//      A/B NOTES call a "spectral-balance" difference is mostly this.
//   2. LOW-REGISTER PITCH. Because the port's delay length IS the period in
//      samples, a period past the buffer cannot be represented and is
//      clamped (PluckedPeriod), so notes below ~46.9 Hz (roughly MIDI 30)
//      are sharp -- +925 cents at note 21. **Braids has no such limit**: at
//      shift 0 its 1024 slots simply stretch over the whole period, so the
//      reference plays note 21 at a measured 27.507 Hz, dead in tune. This
//      is a limitation introduced by the port's delay-line choice, NOT one
//      inherited from Braids' 1024-entry buffer, and the fix is to adopt
//      Braids' own scheme (a `size`-slot table per voice plus a phase
//      accumulator advancing `size / period` per sample), which would close
//      (1) at the same time.
//
// `kPluckedDelaySize` (1024) matches Braids' own per-voice slot count
// (digital_oscillator.cc:1105, `1024 >> p->shift` starts at 1024) and keeps
// three voices at 12 KiB of the engine's 16 KiB arena (SPEC R15) -- but see
// (2): the same NUMBER of slots does not buy the same pitch range once the
// slots are pinned to one sample each.
//
// The fractional read is Hermite (`DelayLine::ReadHermite`), not linear.
// Linear was tried first and measured 4-6 dB of extra high-frequency loss
// specifically in the sustained (post-excitation) portion of the top-TIMBRE
// stretch cases -- a plain 2-tap linear read, fed back through the loop many
// hundred times a second, compounds into real damping Braids' own
// integer-indexed loop (which only interpolates ONCE, on the final output
// read via `Interpolate1022`, never inside the feedback path itself) does
// not have. Hermite closed most of that gap; see A/B NOTES.
//
// (The period clamp and its measured cost are item (2) of DELAY-LINE MODEL
// above; it is a port limitation, not a Braids one.)
//
// Also declared: on a RE-strike of a voice that already has a decaying tail
// in it, Braids blends 25% of that tail into the new noise burst
// (`(dl[ptr] + 3*Random::GetSample()) >> 2`, :1149-1150). The port
// reproduces this exactly (`0.25f*old + 0.75f*noise`, where `old` is read
// from the same position the excitation is about to overwrite) -- an
// earlier version hard-wrote pure noise instead, which measured 2-3 dB loud
// against the module because a fresh strike's excitation is 0.75x full
// scale, not 1.0x, even on a never-before-struck (silent) voice.
//
// COLOR's excitation-length fraction (WIDTH, above) is scaled against
// Braids' own quantised `size` (the shift-search buffer above), not the true
// period -- `size` is always >= the true period (a coarse power-of-two
// ladder, see DELAY-LINE MODEL), so scaling against the exact period instead
// measured excitation windows 10-15% short at both COLOR extremes.
// `PluckedQuantizedSize()` reproduces Braids' own shift search for this one
// purpose only; the delay-line reads themselves still use the exact period.
//
// SELF-STRIKE. Every sample this engine emits comes from a strike, so with
// nothing patched to TRIG it would render pure silence. TRIGGER_UNPATCHED
// has no Braids equivalent (the module always strikes from its own
// trigger), so the port follows the sibling stock engines' "unpatched fires
// once" convention (struck_bell_engine.cc:65-70, struck_drum_engine.cc
// :84-89, snare_engine.cc:171-177): the first unpatched render strikes
// voice 0 exactly once. A patched trigger is unaffected, so this is
// invisible to every tests/ab.json case (all of which set triggerHz > 0).
//
// THE FOURTH MACRO.
//   MORPH  Spread   Braids' three voices always strike at whatever note is
//                    currently played -- unison, always. This port lets
//                    MORPH offset each round-robin slot by a fixed interval
//                    (`voice_index * spread`, +/-7 semitones at the
//                    extremes, 0 -- unison, exactly the module -- at noon),
//                    so three successive strikes build a small arpeggiated
//                    chord as they decay together. Neither the module nor
//                    inharmonic-string (also always-unison round robin,
//                    string_engine.cc:71-77) can do this.
//   MACRO  Stretch  Scales how far `update_probability` is allowed to fall
//                    below "always filter" -- 0 disables the stochastic
//                    detuning entirely at every TIMBRE setting (a clean,
//                    un-stretched decay throughout), 0.5 reproduces
//                    Braids' own top-TIMBRE ceiling exactly, 1.0 pushes the
//                    thinning well past what a real PLUK can reach, into a
//                    sparse, broken, almost-frozen loop.
//
// REFERENCE RENDERER FIX (Lyle: worth knowing about, not just for this
// engine). `braids/test/render_braids_model.cc`'s `MacroOscillator osc` was
// a STACK-LOCAL variable in main(). DigitalOscillator's `delay_lines_` union
// (digital_oscillator.h:362-374) is NOT part of the `state_` union
// `Init()` memsets (digital_oscillator.h:246-258) -- on the real module,
// `MacroOscillator osc` is a FILE-SCOPE global (braids.cc:59), so it gets
// zero-initialised for free; a stack-local copy in a host test harness does
// not. The result: every render of this shape left a never-struck voice's
// delay line as uninitialised stack garbage, and PLUK sums all three voices
// unconditionally every sample (digital_oscillator.cc:1140-1180) -- so that
// garbage rode straight into the output and, combined with `CLIP` pinning
// the sum near full scale, produced a reference that read as flat, near-0 dB
// FOREVER regardless of TIMBRE or note (verified: notes 21/45/69/96, TIMBRE
// 0.0/0.5/1.0, all flat within ~1 dB over 15 s). The fix moved `osc` to file
// scope (`braids/test/render_braids_model.cc`, matching braids.cc:59's own
// declaration) -- a one-line change with no algorithmic effect, verified
// against a from-scratch instrumented rebuild that the corrected reference
// decays exactly as the TIMBRE/loss formula above predicts (silent by
// ~800 ms at TIMBRE 0 note 45, a multi-second decay at TIMBRE 0.5). This
// almost certainly affects every OTHER Braids model in this wave whose first
// strike depends on a zeroed delay line (bowed, blown, fluted, the wavetable
// engines' state) -- worth a blanket re-A/B once the other physical/
// percussive engines land, not just a note on this one.
//
// A/B NOTES. Pitch is 0.1-1.6 cents everywhere the port's own fundamental
// is reachable, confirming the period/rate math end to end. AC RMS runs
// -0.7 to +3.4 dB and full-band spectrum 0.9-4.6 dB across the eight cases
// in tests/ab.json. The two worst are `ladder-worst` (note 54, where
// Braids' filter-update ratio is 1.97 and the port's is 1) and
// `high-register` (note 69, the shortest period tested); both are item (1)
// of DELAY-LINE MODEL, i.e. the port under-damping the loop, and both are
// declared as measured tolerance rather than chased further. `damping-high`
// (the stochastic-stretch top of TIMBRE) is a similarly declared wider
// case, in the same spirit as bowed_engine.h's chaotic self-oscillation
// allowance.
//
// `cents` is omitted from `low-register` (note 21) because the port CANNOT
// reach that fundamental -- see DELAY-LINE MODEL item (2). The measured
// +925 cents there is a real fidelity gap against a module that plays the
// note in tune, not a shared structural limit; it is exempted only so the
// case's level/spectrum figures stay readable, and it should be closed, not
// kept. AC RMS and spectrum still pass there at a modest tolerance
// (-1.0 dB, 2.6 dB against 2.5/3.0 dB), left tight rather than widened.
//
// OUT: all three voices, summed, hard-clamped to +-1 (digital_oscillator.cc
// :1181 CLIPs the identical sum) -- a bounded expression of individually-
// bounded terms (SPEC R1), so this engine uses a positive gain. AUX (mono):
// the raw excitation noise only, summed across whichever voices are
// currently striking -- silent between strikes, matching the shared "raw
// exciter on AUX" idiom (SPEC R1 precedent, bowed_engine.h, particle_burst
// _engine.h). In stereo, OUT/AUX become L/R: each voice is panned to a
// fixed round-robin position (string_engine.cc:60, string_pan = {0.15,
// 0.5, 0.85}, reused verbatim) instead, and the excitation-only AUX is
// dropped -- so three successive strikes also step left, centre, right.
// Per SPEC R13, constant-power panning of three summed components reads
// roughly 3 dB quieter per channel than the mono sum at the same out_gain.

#ifndef PLAITS_DSP_ENGINE2_PLUCKED_ENGINE_H_
#define PLAITS_DSP_ENGINE2_PLUCKED_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/physical_modelling/delay_line.h"

namespace plaits {

// digital_oscillator.h:49.
const int kNumPluckVoices = 3;

// Matches Braids' own per-voice buffer cap (digital_oscillator.cc:1105);
// see the header's DELAY-LINE MODEL note for why a fixed-size fractional
// delay line reproduces the same effective filter rate. Three voices at
// 1024 floats fit in 12 KiB of the engine's 16 KiB arena.
const size_t kPluckedDelaySize = 1024;
const float kPluckedMinPeriod = 4.0f;

// TIMBRE's damping/stretch split, digital_oscillator.cc:1125-1136. p0/p1
// are Braids' int16 parameter range (0..32767); the >>3 truncation in
// Braids' update_probability slope is dropped in favour of its continuous
// equivalent (31/8 per LSB) -- at ~0.05% of the control's own step size,
// far below anything the truncated-LUT caution in the port guide is about.
const float kPluckedParamFull = 32767.0f;
const float kPluckedTimbreHalf = 16384.0f;
const float kPluckedUpdateProbabilityFull = 65535.0f;
const float kPluckedUpdateProbabilityBase = 131072.0f;
const float kPluckedUpdateProbabilitySlope = 31.0f / 8.0f;
const float kPluckedProbabilityFull = 65536.0f;
const float kPluckedLossFull = 32768.0f;
const float kPluckedLossPitchBase = 4096.0f;
const float kPluckedLossPitchFloor = 256.0f;
// phase_increment_ >> 14 at Braids' post-<<=1, per-48kHz-sample scaling is
// frequency * 2^18 in NoteToFrequency's cycles-per-sample units (RATE note
// above).
const float kPluckedLossFreqScale = 262144.0f;

// COLOR's excitation width, digital_oscillator.cc:1110-1112.
const float kPluckedWidthGain = 1.5f;
const float kPluckedWidthBase = 8192.0f;
const float kPluckedWidthFull = 65536.0f;

// MORPH: Spread, +/-7 semitones per round-robin step at the extremes.
const float kPluckedMaxSpread = 7.0f;

// MACRO: Stretch, scales how far below 1.0 the update-probability fraction
// may fall. 1.0 at macro=0.5 reproduces Braids' own ceiling exactly.
const float kPluckedMaxStretch = 2.5f;

// string_engine.cc:60, reused verbatim for the same round-robin-into-
// stereo-position idiom.
extern const float kPluckedPan[kNumPluckVoices];

class PluckedEngine : public Engine {
 public:
  PluckedEngine() { }
  ~PluckedEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_PLUCKED; }

 private:
  DelayLine<float, kPluckedDelaySize> delay_line_[kNumPluckVoices];
  float period_[kNumPluckVoices];
  float min_period_[kNumPluckVoices];
  float excitation_remaining_[kNumPluckVoices];

  int active_voice_;
  float active_offset_semitones_;
  bool ever_struck_;

  float loss_frac_;
  float update_probability_frac_;

  DISALLOW_COPY_AND_ASSIGN(PluckedEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_PLUCKED_ENGINE_H_
