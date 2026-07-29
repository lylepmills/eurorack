// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SAW SWARM: seven naive, unison-detuned sawtooths summed, driven
// through a soft saturator, and filtered by a state-variable filter whose
// cutoff tracks the note.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderSawSwarm
// (braids/digital_oscillator.cc:164-242).
//
// LINEAGE. Plaits' own SWARM engine (plaits/dsp/engine/swarm_engine.h) shares
// only the word: it is a granular cloud of 8 saw/sine pairs, each voice's
// pitch gliding between a randomized "grain" endpoint and the next under a
// GrainEnvelope, producing an evolving, never-quite-settling cluster. SAW
// SWARM is the opposite kind of instrument -- seven voices at FIXED,stable
// detune offsets from a single held pitch, deliberately using RAW,
// non-band-limited phase-truncation sawtooths (this is one of Braids'
// "digital" oscillators, not one of its BLEP-corrected "analog" ones -- see
// RATE below), summed and pushed through a fixed soft-clip, then carved by a
// resonant filter whose cutoff the module ties to COLOR. It is a static
// chorus-into-filter voice, not a granular texture, and it is meant to
// alias at the top of its range the way Plaits' own (BLEP-clean) SWARM never
// does. The two engines have nothing in their signal paths in common beyond
// "several detuned saws."
//
// THE BRAIDS CONTROLS, with line numbers.
//   TIMBRE (parameter_[0]) -- the detune spread between the seven saws.
//     digital_oscillator.cc:168-179: `detune = ((parameter_[0]+1024)^2)>>9`,
//     then voice i (i=0..6, rank = i-3) is detuned by `detune*rank`, read as
//     a fixed-point pitch offset via ComputePhaseIncrement, interpolated
//     across the nearest two 1/128-semitone table steps.
//   COLOR (parameter_[1]) -- the resonant filter's cutoff, tracked to the
//     note with two different slopes either side of a pivot near COLOR=1/3.
//     digital_oscillator.cc:186-196: below the pivot the cutoff falls away
//     from the note twice as fast as it climbs above it.
// There is no BEGIN_INTERPOLATE_PARAMETER trap here -- RenderSawSwarm reads
// parameter_[0]/parameter_[1] directly, unlike the models that route through
// the interpolation macros (see fold_engine.h for that trap in full).
//
// THE FILTER. digital_oscillator.cc:198-199 read a fixed cutoff-frequency
// coefficient from `lut_svf_cutoff` and a FIXED, non-user-controlled damping
// coefficient `lut_svf_damp[0]` (verified against
// braids/resources/lookup_tables.py's generator: resonance index 0 gives
// damp=2.0 exactly before quantisation; the stored table entry is 65534,
// 0 LSB off; applied through Braids' `>>15` (i.e. /32768) scaling that is
// 1.999939, not the 2.0 the generator's own /32767 scale implies -- a
// half-LSB rounding artefact of the fixed-point port). digital_oscillator.cc:
// 203-238 is a naive (Chamberlin, non-zero-delay-feedback) two-integrator
// SVF that computes LP, BP and HP simultaneously every sample and outputs
// only HP.
//
// A first version of this port kept that exact topology, re-deriving only
// the cutoff-frequency table (see RATE below) and reusing Braids' fixed damp
// as a literal constant. The A/B caught what that missed: a naive/Chamberlin
// SVF's gain, for a FIXED damp, grows sharply as the cutoff-to-samplerate
// ratio climbs -- and porting the SAME target cutoff-in-Hz to 48 kHz instead
// of Braids' native 96 kHz DOUBLES that ratio for every color setting. At
// note 48, color 1.0 (cutoff ~5.3 kHz) this measured as the port running
// ~2.2x hotter than the reference (+6.8 dB AC RMS, hard-clipping) even
// though the SAME cutoff at Braids' own 96 kHz sits far enough from its
// clamp that Braids' reference shows no such peak at all -- confirmed by a
// standalone simulation of both topologies at the two effective ratios
// (0.055 at 96 kHz vs. 0.110 at 48 kHz for this cutoff): the naive
// structure's passband gain went from a flat ~1.0x to ~1.85x purely from
// halving the rate, while a zero-delay-feedback (ZDF) SVF at the SAME ratio
// and Q stayed flat. So the port uses Plaits' own `stmlib::Svf` (ZDF,
// tan-prewarped) instead of Braids' naive one -- it does not carry this
// rate-relative peaking, so it reproduces Braids' actual (non-peaky, no
// audible resonance) measured behaviour without needing to match Braids'
// internal sample rate. `stmlib::Svf`'s damping term `r_` (r_ = 1/resonance)
// plays the identical role as Braids' `damp` (both subtract `r*bandpass`
// from the input to form the highpass), so the stock resonance is carried
// over unchanged as `resonance = 1/damp = 1/1.999939 = 0.500015` -- the
// same fixed operating point Braids uses, on a topology that keeps it flat
// across the whole cutoff range rather than only across Braids' own 96 kHz
// one. `stmlib::Svf::Process<FILTER_MODE_HIGH_PASS, FILTER_MODE_BAND_PASS>`
// returns HP and BP from one state update (its own class does not expose a
// simultaneous 3-output call); LP is recovered algebraically from the
// SVF's own defining identity `input = HP + r*BP + LP`, needing no second
// state update. See MACRO/MORPH below for what this opens up.
//
// THE SUM AND SHAPER. digital_oscillator.cc:220-227 sum seven UNIPOLAR
// truncated-phase ramps (`phase>>19`, i.e. floor(phase)*8192, no BLEP) and
// subtract a bias of 28672 = 7*8192/2, centring the sum. That is exactly
// 4096 * (sum of seven BIPOLAR naive sawtooths -1..1), so the port sums
// `2*phase-1` per voice and scales by 4096/32768 = 0.125 before the shaper --
// see the sum-gain derivation in the .cc. digital_oscillator.cc:228 then
// reads `ws_moderate_overdrive`, generated in
// braids/resources/waveshapers.py:40 as `tanh(2x)` (x in -1..~0.992, `scale`-
// normalized to int16). Verified programmatically against the reference
// table (generator formula reproduced with the SAME `scale()` normalization
// and 257-entry structure, then compared to the actual embedded
// `ws_moderate_overdrive` array from braids/resources.cc, which the
// regenerated formula matches to 0 LSB): over the -0.875..0.875 domain this
// engine actually drives it (seven +-1 voices at a 0.125 sum-gain), the
// closed form `tanh(2x)/tanh(2)` sits within 2.3 LSB (~-83 dB) of the
// table's own 257 entries, and within 4.5 LSB (~-77 dB) of the value the
// engine really reads, i.e. after Interpolate88's 8.8 linear interpolation
// between entries. (The residue is two effects: the generator's `scale()`
// centres on the array mean and normalizes to 32766, not 32767/tanh(2), so
// the two curves differ by a constant 1.00008 factor; the rest is the
// table's chord-vs-curve interpolation error.) Only right at the table's own
// |x|>0.99 saturation edge -- which this engine's input range never touches
// -- does the deviation grow to 40 LSB (~-58 dB), an artefact of the
// generator's own last-sample-duplicate quirk in an already fully-saturated
// region. Substituted as a formula per SPEC R4 (one tanh per output sample,
// not oversampled) rather than embedded.
//
// RATE. RenderSawSwarm does not end in `size -= 2`: braids.cc:141 runs the
// whole digital-oscillator bank at a native 96 kHz (`F_CPU/96000`), and this
// function shows no internal 2x/4x oversampling of its own (one phase
// advance, one filter step per sample) -- SPEC R5's "96 kHz algorithm" case,
// where every rate-dependent constant must be re-derived, not transplanted.
// The filter cutoff table is the one that matters, and re-deriving it turned
// out to mean more than swapping the prewarp formula's sample rate -- see
// THE FILTER above for the topology change that was actually needed. What
// DOES carry over as a straightforward re-derivation is cutoff-to-note
// mapping itself: `cutoff_hz = 440 * 2^((note-69)/12)`
// (lookup_tables.py:98, sample_rate=96000 still in effect there -- the
// file's OTHER `sample_rate = 48000` at line 254 is set later, for
// unrelated tables, and does not apply here), computed directly rather than
// table-driven, since Plaits' own note-to-frequency machinery already does
// the equivalent conversion R6-corrected. The generator's `fc <= fs/8`
// clamp (lookup_tables.py:100) is carried over as its EFFECTIVE ABSOLUTE
// ceiling -- 96000/8 = 12 kHz, the real frequency past which Braids'
// cutoff plateaus regardless of how much further COLOR or the note would
// otherwise push it -- rather than recomputed as 1/8 of the port's own 48
// kHz (which would halve that ceiling to 6 kHz for no acoustic reason; the
// ZDF filter does not need a stability clamp at all, only this fidelity
// one). Everything else (the sawtooths' pitch, the detune spread, the tanh
// shaper) is derived from Plaits' own note-to-frequency machinery, already
// R6-corrected, so nothing else needed re-deriving.
//
// OUT/AUX and the fourth macro. Braids computes LP, BP and HP every sample
// but wires out only HP -- an unreachable-by-the-module choice, and exactly
// where SPEC's "spend the fourth macro on what the model implies but the
// module can't reach" applies twice over:
//   MACRO   Resonance   the fixed resonance opened up as a control, centred
//                       (macro=0.5) on the module's own value.
//   MORPH   Filter      sweeps the SAME filter's output from LP (0) through
//                       Braids' own HP (0.5, the module's operating point)
//                       to BP (1).
// AUX plays the mirrored sweep (1-MORPH) off the SAME filter state, so it is
// always the tap OUT is not favouring -- LP when OUT leans HP/BP, and so on;
// the two coincide only at the exact centre, same as FOLD's OUT/AUX pair.
//
// Declared deviations from Braids:
//   - the fixed-point 1/128-semitone-stepped detune and cutoff mappings are
//     ported as their exact continuous closed forms (verified against the
//     integer arithmetic across a 1000-point sweep of each knob: the detune
//     to within 0.0001 semitones, the cutoff to within 0.012 notes -- the
//     latter being just the two floor operations Braids performs, the
//     `>>5` on the pitch offset and the truncation of COLOR into a 15-bit
//     code),
//     not as re-embedded LUTs -- Braids itself does not table-drive either
//     one, so there is no table to embed.
//   - the waveshaper is the formula substitution measured above (~-77 dB
//     against the interpolated table read), not the embedded table.
//   - the filter is Plaits' own ZDF `stmlib::Svf`, not Braids' naive
//     Chamberlin one -- see THE FILTER above for the measured reason. Its
//     fixed resonance and the LP/BP taps it discards are opened up as MACRO
//     and MORPH.
//   - Braids randomizes six of the seven voices' phase on Strike() and
//     leaves the seventh (rank -3, which happens to alias the base class's
//     shared `phase_` member) continuous -- an artefact of DigitalOscillator
//     reusing that member across many models, not a deliberate design
//     choice. This port MIRRORS that split at Reset() (index 0 zeroed, 1..6
//     randomized), because Braids reaches it too: DigitalOscillator::Init()
//     zeroes `phase_` and raises `strike_` (digital_oscillator.h:246-257),
//     and Render calls Init() on every shape change
//     (digital_oscillator.cc:111-115), so selecting SAW SWARM always lands
//     on six-random-plus-one-zero. Leaving all seven at 0 instead -- the
//     port's first version -- made an untriggered drone start on maximum
//     constructive interference and disperse only as the voices beat apart,
//     which at low detune takes on the order of ten seconds; it cost about
//     1 dB of level and 2 dB of low-band error in the A/B (see MEASURED).
//     On TRIGGER_RISING_EDGE the port still randomizes all seven uniformly,
//     which is the one place it differs from Braids' six-plus-one.
//   - alt firmware, stereo mode: not in the module. The seven voices pan
//     across the field by detune rank, as Plaits' own SWARM engine's alt
//     stereo mode already does for its voices; each side runs its own copy
//     of the shaper and filter (independent state) on its own panned sum.
//     OUT/AUX become L/R, both using the SAME (unmirrored) MORPH position.
//
// MEASURED. All six of tests/ab.json's cases hold to <=1.5 dB AC RMS /
// <=10 cents / <=1.5 dB spectrum: AC RMS +0.05 / -0.53 / +0.04 / +0.03 /
// -0.43 / -0.18 dB and spectrum 0.45 / 1.33 / 0.41 / 0.45 / 0.39 / 0.70 dB
// for stock-mid / unison / wide-chorus / color-low / color-high / high-note.
//
// An earlier revision measured `unison` (TIMBRE near zero, the slowest
// possible beat) at +1.2 to +2.7 dB AC RMS depending on note, and widened
// that case's tolerance to 3 dB on the theory that the two filters'
// transient response to a slow amplitude envelope differed. That was a
// misdiagnosis, and the experiment offered for it -- swapping the
// TRIGGER_RISING_EDGE randomization for Braids' six-plus-one scheme and
// seeing no change -- could not have shown anything, because ab.json's
// cases declare no triggerHz, so render_model.cc drives TRIGGER_UNPATCHED
// and that branch never runs. The real cause was Reset() leaving all seven
// voices phase-aligned while the reference renderer's Strike() randomizes
// six of them (see the deviation above). Mirroring Braids' initial phases
// took `unison` to -0.53 dB and pulled the fundamental's own octave band
// back inside 1 dB in every case (color-low 80-160 Hz: -2.49 -> -0.60 dB;
// stock-mid: -1.89 -> -0.89 dB), so no case needs a widened tolerance.
//
// What is left, per --bands, is a consistent +2 to +4 dB in the top octave
// (20480-24000 Hz) and about +1 dB in 10240-20480 Hz. That is the expected
// decimator-class residue: Braids' naive sawtooths run at 96 kHz and the
// reference halfband-filters everything above 24 kHz away, while the port's
// run at 48 kHz and fold their images back into the band. `unison`'s
// spectrum figure (1.33 dB) is the one that is not decimator residue -- at
// COLOR 0.5 and note 48 the cutoff sits at ~330 Hz, well above the 131 Hz
// fundamental, so the fundamental lands on the filter's steep skirt where
// the ZDF and Chamberlin topologies diverge most (80-160 Hz: -3.60 dB, on
// 3.3% of the energy). That is the declared cost of the topology change in
// THE FILTER above, and it is the only place it is visible.

#ifndef PLAITS_DSP_ENGINE2_SAW_SWARM_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SAW_SWARM_ENGINE_H_

#include "stmlib/dsp/filter.h"

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kNumSawSwarmVoices = 7;

// digital_oscillator.cc:169: detune = ((parameter_[0]+1024)^2)>>9, then
// saw_detune = detune*rank, read as a Q7 (128/semitone) pitch offset via
// saw_detune>>16. In continuous terms (parameter_[0] = 32767*timbre):
// semitone_offset(rank) = rank * (32*timbre+1)^2 / 4096 -- verified against
// the integer arithmetic above (within 0.0001 semitones at every rank over
// a 1000-point TIMBRE sweep).
const float kSawSwarmDetuneScale = 1.0f / 4096.0f;

// digital_oscillator.cc:187-196: the pivot is COLOR code 10922 (not exactly
// 32767/3, but within 0.01%), below which the cutoff-vs-note slope is twice
// what it is above. Reproduced as exact fractions of the original integer
// constants (24 and 12, over a 4096 denominator, scaled to a 0..1 COLOR
// range by 32767) rather than the rounded 192/96 they are numerically
// indistinguishable from.
const float kSawSwarmColorPivot = 10922.0f / 32767.0f;
const float kSawSwarmColorSlopeBelow = 24.0f * 32767.0f / 4096.0f;
const float kSawSwarmColorSlopeAbove = 12.0f * 32767.0f / 4096.0f;

// The filter cutoff table is generated over MIDI notes 0..256
// (braids/resources/lookup_tables.py:98: `440 * 2**((idx-69)/12)`) and
// Braids clamps hp_cutoff (and so the note fed to it) to that same span.
const float kSawSwarmCutoffNoteMax = 256.0f;

// The generator's `fc <= fs/8` clamp (lookup_tables.py:100) at Braids' native
// 96 kHz -- kept as this absolute Hz ceiling (see RATE in the header above
// for why it is not re-expressed as fs/8 of the port's own 48 kHz).
const float kSawSwarmCutoffHzMax = 12000.0f;

// lut_svf_damp[0] (resources.cc:182) = 65534, applied via Braids' `>>15`
// (i.e. /32768) scaling: 65534/32768 = 1.999939 -- 0 LSB off the generator's
// own damp=2.0 at resonance index 0 (verified against
// braids/resources/lookup_tables.py's formula), the residual being the
// >>15-vs-/32767 rounding the fixed-point port itself introduces, not a
// table error. This is SAW SWARM's whole, fixed resonance; the module has no
// control for it. `stmlib::Svf`'s `r_ = 1/resonance` plays Braids' `damp`
// role directly (see THE FILTER above), so the stock RESONANCE is this
// value's reciprocal.
const float kSawSwarmDampStock = 65534.0f / 32768.0f;
const float kSawSwarmResonanceStock = 1.0f / kSawSwarmDampStock;

// MACRO's Resonance range around that stock value: calmer (more damped, no
// peak at all) at macro=0, a controlled resonant peak at macro=1. Unlike
// Braids' naive filter, the ZDF `stmlib::Svf` has no stability ceiling to
// respect here -- ordinary Q bounds.
const float kSawSwarmResonanceCalm = 0.35f;
const float kSawSwarmResonancePeak = 3.0f;

// Sum-gain from digital_oscillator.cc:220-227: seven +-1 naive sawtooths
// summed and scaled by 4096/32768 before the shaper (see the .cc for the
// full derivation from Braids' `phase>>19` / bias-28672 form).
const float kSawSwarmSumGain = 4096.0f / 32768.0f;

// tanh(2.0): the shaper's own peak before the generator's `scale()`
// normalizes it to full scale (braids/resources/waveshapers.py:40, 62).
const float kSawSwarmShaperPeak = 0.9640275800758169f;

// alt firmware, stereo: pan position per voice (rank -3..3, i.e. index
// 0..6), mirroring how Plaits' own SWARM engine spreads its voices by rank.
const float kSawSwarmPan[kNumSawSwarmVoices] = {
  0.05f, 0.2f, 0.35f, 0.5f, 0.65f, 0.8f, 0.95f
};

class SawSwarmEngine : public Engine {
 public:
  SawSwarmEngine() { }
  ~SawSwarmEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_SAW_SWARM; }

 private:
  float phase_[kNumSawSwarmVoices];
  float frequency_[kNumSawSwarmVoices];

  float cutoff_note_;
  float resonance_;
  float morph_;

  stmlib::Svf svf_;
  stmlib::Svf svf_aux_;

  DISALLOW_COPY_AND_ASSIGN(SawSwarmEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SAW_SWARM_ENGINE_H_
