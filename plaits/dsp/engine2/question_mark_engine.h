// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' QUESTION_MARK model: a Morse-code transmitter. A 1064-byte table of
// packed 2-bit symbols keys a sine on and off over a wandering noise bed, and
// the sum runs through a squared-distortion stage.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderQuestionMark
// (braids/digital_oscillator.cc:2235-2301).
//
// THE MODEL THE MODULE HIDES. QUESTION_MARK is not on the shape list. The
// firmware selects it only while the MARQUEE settings page is on screen AND
// the marquee text has been set to "49" (braids.cc:193-194 `if (ui.paques())
// osc.set_shape(MACRO_OSC_SHAPE_QUESTION_MARK)`, gated by ui.h:89-93 and
// settings.cc:116-118 `paques_ = !strcmp(data_.marquee_text, "49")`; paques is
// French for Easter). Leave that page and the module goes back to the selected
// shape, so on hardware it is a thing you visit rather than a thing you patch.
// This port makes it an ordinary engine: it is selectable, it responds to a
// trigger, and it keeps running.
//
// The transmitted text is the "Scope" bar passage from Thomas Pynchon's THE
// CRYING OF LOT 49 -- which is what the marquee code "49" refers to. It is
// carried here as `kMorseCode`, byte-for-byte the table the module already
// ships, and is not reproduced as text anywhere in this port.
//
// PARAMETER MAPPING, with the line each reading comes from. RenderQuestionMark
// contains no BEGIN_INTERPOLATE_PARAMETER_N macro at all, so the trap
// fold_engine.h documents (the suffix is the parameter INDEX, not an ordinal)
// cannot arise here -- both parameters are read straight off the array.
//
//   parameter_[0] = TIMBRE = the keying speed, and nothing else
//     :2252  dit_duration = 3600 + ((32767 - parameter_[0]) >> 2)
//     Counted in 96 kHz samples; a tick is dit_duration + 1 samples, because
//     the test is `if (++cycle_phase > dit_duration)` (:2262). TIMBRE 1 gives
//     3601 samples = 37.5 ms per dit, TIMBRE 0 gives 11792 = 122.8 ms.
//
//   parameter_[1] = COLOR = the noise bed AND the distortion, welded together
//     :2253  noise_threshold = 1024 + (parameter_[1] >> 3)   the bed's floor
//     :2296  sample += distorted * parameter_[1] >> 15       the grit
//     One knob doing two jobs is exactly the thing this port unbundles; see
//     the two spare macros below.
//
// THE STATE MACHINE, since it is the whole model and an arithmetic slip in it
// shows up as the transmission diverging part way through rather than as a
// timbre error. `state_.clk` is reused (the ClockedNoiseState union member,
// digital_oscillator.h:206-212), so the field names read oddly: `rng_state` is
// the keying gate, `cycle_phase` a sample counter, `cycle_phase_increment` the
// symbol index, `sample` a countdown of ticks left in the current symbol.
//
//   strike (:2241-2248)  gate off, counter 0, 10 ticks to run, symbol index
//                        -1 (as uint32), noise seed 32767.
//   each tick expiry     decrement the countdown; when it reaches zero, step
//     (:2262-2279)       the symbol index, TOGGLE the gate, and read the next
//                        2-bit symbol -- element = symbol_index >> 2 selects
//                        the byte, (symbol_index & 3) << 1 the bit pair --
//                        giving a duration of (2 << code) - 1 ticks: 1, 3, 7
//                        or 15. Then reset the sine phase to a quarter cycle.
//   duration 15 (:2271)  is the terminator, not a symbol: 100 ticks of silence
//                        and the symbol index back to -1, so the message
//                        repeats forever.
//
// Because the gate toggles on every symbol, the stream alternates mark, space,
// mark, space from the first symbol on. Decoded, the table holds 4252 symbols
// = 8240 ticks, 939 characters in 158 words, and every symbol pattern in it is
// a valid International Morse character (verified, see the .cc). One pass plus
// the terminator gap is 8340 ticks: 5 min 13 s at TIMBRE 1, 17 min 4 s at
// TIMBRE 0. A trigger restarts it from the top.
//
// RATE (SPEC R5). RenderQuestionMark ends in a plain `while (size--)`
// (:2254), not `size -= 2`: a genuinely 96 kHz-native algorithm. FOUR separate
// constants in it are rate-bearing, which is more than most models in this
// port carry:
//   - dit_duration (:2252) counts 96 kHz samples;
//   - the noise-intensity random walk (:2280) takes one step per 96 kHz
//     sample, so its drift per SECOND scales with the rate, not per sample;
//   - the noise draw (:2291) is one white sample per 96 kHz sample, so its
//     bandwidth is the rate;
//   - the phase increment.
// Re-deriving four constants independently is four chances to get one wrong,
// so this port does what fold and cymbal do instead: it runs the ENTIRE state
// machine and signal path 2x oversampled, at Braids' own 96 kHz, and every one
// of the four transfers verbatim. The only quantity that had to be chosen for
// the port is the decimator.
//
// The decimator is a 2-tap box average, following cymbal_engine.cc rather than
// fold's 8-tap FIR (which is a 4x kernel). For this model the choice costs
// less than it looks, because the dominant term is broadband noise and a 2-tap
// box decimating 2:1 leaves white noise EXACTLY flat: the passband droop
// cos^2(pi*f/96k) and the fold-back from (48k - f) sum to
// cos^2(theta) + sin^2(theta) = 1 at every output frequency. What it does not
// leave flat is what the two HARD NONLINEARITIES put above 24 kHz -- the CLIP
// at :2294/:2297 and the squared distortion at :2295 -- which folds back
// attenuated instead of being removed. The A/B measures that directly: the
// 20.48-24 kHz band reads +0.59 dB at COLOR 0, where the clip is the only
// nonlinearity running, and +1.21 dB at COLOR 1, where the distortion is at
// full depth.
//
// TABLES. Two are embedded, both extracted programmatically from
// braids/resources.cc and both verified rather than asserted (SPEC §4):
//
//   kMorseCode  = wt_code, 1064 bytes. Its "generator" is not a formula -- it
//     is a literal list in braids/resources/waveforms.py, and the embedded
//     copy is byte-identical to it: 0 of 1064 bytes differ. Repacking the
//     decoded symbol stream reproduces all 1064 bytes, and the semantic check
//     is stronger than either: all 4252 symbols decode to well-formed Morse
//     with zero unrecognised patterns, which no wrong bit-order or wrong
//     shift would survive.
//
//   kBraidsSine = wav_sine, 257 int16. Generated by
//     braids/resources/waveforms.py:97,117 as scale(sine[quadrature]) --
//     -cos(2*pi*x) sampled at 257 points, mean-centred, rescaled and
//     2nd-order dithered. Reproducing that closed form gives a maximum
//     deviation of 1.68 LSB (mean 0.54), which is the dither and nothing
//     else. The table is NOT approximable by a plain -cos: against
//     -cos(2*pi*x)*32766 it is off by up to 254.6 LSB, because `scale`
//     centres on a 257-point mean that includes a duplicated endpoint, which
//     is why wav_sine[0] is -32512 rather than -32766.
//
// Embedding wav_sine rather than calling Plaits' Sine() also disposes of the
// -cos/sin trap fold_engine.h carries a phase offset for, and is REQUIRED by
// the second of the two reads:
//
//   :2258  Interpolate824(wav_sine, phase)          the keyed tone
//   :2292  wav_sine[(phase >> 22) & 0xff]           the noise's ring modulator
//
// The second is a TRUNCATED read, and reproducing the truncation is not
// optional (SPEC §4). `phase >> 22` spans 0..1023 over one cycle of the played
// note and `& 0xff` folds that to 0..255, so the modulator runs at FOUR times
// the note, as a 256-step zero-order-held staircase, not a smooth sine. It is
// applied to the noise on EVERY sample, including the silences -- so the bed
// is always ring-modulated at 4*f0 even when nothing is being keyed.
//
// NOISE, and why this engine carries its own PRNG. Braids draws from the
// global stmlib::Random LCG (state * 1664525 + 1013904223, top 16 bits read
// signed), twice per sample: one step drives an UNBOUNDED random walk in
// `seed`, whose magnitude >> 8 becomes the bed's intensity, clamped below by
// noise_threshold and above at 16000 (:2280-2290); the second is the noise
// sample itself. The walk has no leak, so the bed starts near silence at
// strike (seed = 32767 gives intensity 127, under every threshold) and drifts
// UP over seconds until it spends most of its time pinned at the 16000
// ceiling. That slow swell is an audible part of the model, and it is why the
// A/B windows below are 8 seconds rather than 3.
//
// This port steps an identical LCG held per instance, initialised to 0x21 --
// stmlib::Random's own power-on value (stmlib/utils/random.cc). On the module
// the stream that reaches this model depends on whatever ran before it; per
// instance it is deterministic, which is what makes the A/B below a
// reproducible measurement of the STATE MACHINE rather than a comparison of
// two noise realisations.
//
// THE TWO SPARE MACROS both split COLOR's welded pair, which is the one thing
// the module's two knobs cannot do:
//
//   HARMONICS  Static    Braids' COLOR: the bed's floor and the grit together.
//   TIMBRE     Speed     Braids' TIMBRE: the keying rate.
//   MORPH      Bed       the noise level alone, 0 to 2.5x the module's. At 0
//                        the transmission is a bare keyed tone, which the
//                        module cannot reach at any COLOR -- the floor is
//                        1024 even at COLOR 0, and the random walk climbs
//                        away from it regardless.
//   MACRO      Grit      the squared-distortion depth alone, 0 to 2x. At 0 a
//                        clean sum; at noon exactly Braids' own
//                        `distorted * parameter_[1] >> 15`.
//
// OUT is the transmission, Braids' own signal. AUX (mono) is the keyed carrier
// alone -- the gated sine before the bed and the grit -- which is the message
// without the atmosphere. In stereo OUT/AUX become L/R and both carry the full
// signal, sharing the keying state machine, the phase and the intensity walk
// but drawing their noise SAMPLES from independent LCGs: the tone stays
// centred and the bed opens out. The width therefore tracks the Bed macro
// rather than being a fixed image, and it is modest at the module's own
// settings -- measured L/R correlation over 6 s is 1.0000 with Bed at 0 (a
// mono keyed tone), 0.9928 at all four macros noon, and 0.9090 with Bed at 1
// and the keying slow. The host renderer never sets EngineParameters::stereo,
// so those figures come from a direct harness, not from the A/B below.
//
// Declared deviations from Braids (R8 -- no bit-fidelity is claimed anywhere):
//   - the decimator is a 2-tap box average; Braids does not decimate at all,
//     and the reference render uses a 127-tap halfband.
//   - the phase increment is derived from Plaits' NoteToFrequency, which reads
//     kCorrectedSampleRate (SPEC R6), not Braids' pitch table. Braids' own
//     pitch CEILING is kept: the note is clamped to 127.9921875 before the
//     conversion, matching ComputePhaseIncrement's saturation (see
//     kQuestionMarkHighestNote).
//   - a one-pole DC blocker at ~1.5 Hz sits on each output. The squared
//     distortion is a strictly positive term, so this signal carries real DC:
//     Braids' own render measures +0.187 at note 45 / COLOR 1, and with the
//     blocker disabled this engine measures +0.175 at the same setting, +0.283
//     with Grit at 1 and +0.326 with Bed there too. With the blocker it is
//     +0.005 to +0.008. R1 therefore also puts this engine on the
//     negative-gain limiter path.
//   - the PRNG is per instance rather than the global one the module shares
//     between models (see NOISE above).
//
// A/B NOTES (tests/ab.json, `python3 ab_engine.py packages/mutable-instruments/
// question-mark --bands`). SEVEN cases, 8 s each: six inside the module's own
// range (both Braids axes at both ends plus a high register), and a seventh at
// note 135 that exists only to hold the pitch ceiling above. Over the six,
// worst AC RMS -0.47 dB and worst energy-weighted spectrum 0.71 dB, both at
// COLOR 1; the best (COLOR 0) reads -0.00 dB and 0.10 dB. The pitch-ceiling
// case reads -0.83 dB and 0.56 dB -- and -1.96 dB / 1.17 dB, i.e. a double
// FAIL, if the clamp is removed, which is what makes it a guard rather than a
// seventh sound.
// No `cents` tolerance is declared anywhere -- see tests/ab.json, and note the
// estimator returns -0.8, -4.6, -2.1, +10.6, +455.0 and "no f0" across the
// seven cases, which is the shape of a reading that is not measuring anything.
//
// THE STATE MACHINE IS CHECKED SEPARATELY, because none of the three headline
// metrics would catch a transmission that starts right and drifts. Predicting
// the keying grid straight from kMorseCode (tick = dit_duration + 1 samples at
// 96 kHz, 10 ticks of lead-in, then the symbol durations in order) gives 57
// gate edges in the first 7.5 s at TIMBRE 0.7. A 10 ms Goertzel at the carrier
// finds all 57 on BOTH sides, to the same 2.0 ms worst / 0.9 ms mean offset --
// which is the detector's own rise time, identical either way. Across the six
// in-range cases the two carrier envelopes correlate 0.9913 to 0.9995 at ZERO lag
// (best lag is 0 in all six), and the last quarter of each 8 s window
// correlates almost as well as the first -- speed-slow 0.9976 -> 0.9947, the
// worst of them static-high 0.9955 -> 0.9860 -- so the symbol clock is not
// slipping.
//
// TWO band-level residuals are declared rather than hidden. Both are quoted
// over the six in-range cases; the pitch-ceiling case is measured separately at
// the end, because at note 135 the spectrum is a single 13.3 kHz carrier and
// almost none of the energy is where these two paragraphs look.
//
// (1) The two lowest bands, 20-80 Hz, run -0.9 to -4.6 dB over 3.5-6.5% of the
// energy -- EXCEPT at COLOR 0, where they are within 0.31 dB. That is this
// engine's DC blocker, and the exception is the proof: at COLOR 0 the
// distortion term is switched off, so there is no DC to block and the deficit
// disappears. ab_compare removes only the GLOBAL mean, so the reference keeps
// its slowly-varying, keying-driven offset, and a 2048-point Hann frame puts a
// frame's DC almost entirely into bins 0-2 -- i.e. into exactly those two
// bands. Rebuilt with the blocker disabled the same cases read 0.10-0.29 dB
// overall and 0.00 dB at 20-40 Hz, which is the measurement of what the
// blocker costs. It is kept: Braids puts this DC out of the module, a Plaits
// engine should not, and the measured +0.283 at Grit 1 would in any case run
// past the SDK's own 0.2 gate.
//
// (2) The 20.48-24 kHz band runs +0.59 to +1.21 dB over 7.7-8.8% of the
// energy: the box decimator folding back what the clip and the squared
// distortion put above 24 kHz (see RATE). It is the first thing to revisit if
// a real halfband is ever worth its cycles.
//
// Everything between 80 Hz and 20.48 kHz -- 81 to 94% of the energy, depending
// on the case -- is within 0.8 dB. Four of the six hold every band there inside
// 0.55 dB; the two that do not are static-low and high-register, at 320-640 Hz
// (-0.80 and +0.77 dB, over 1.9% and 2.1% of the energy) and, for
// high-register, 160-320 Hz too (+0.75 dB over 1.0%).
//
// THE PITCH-CEILING CASE runs wider and is not folded into those figures: it
// sits at +0.66 to +1.12 dB from 40 Hz to 10.24 kHz and +1.51 dB above
// 20.48 kHz, while the 10.24-20.48 kHz band that actually holds the carrier --
// 64.7% of the energy on its own -- reads +0.31 dB. Everything outside that one
// band is 0.1-3.4% of the energy each, so the wide numbers are the low-level
// bed and its 4*f0 ring-modulation images being compared where there is barely
// any signal, not the carrier being wrong. It is kept as a REGRESSION GUARD on
// the ceiling, not as a fidelity claim.

#ifndef PLAITS_DSP_ENGINE2_QUESTION_MARK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_QUESTION_MARK_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// digital_oscillator.cc:2252-2253. Kept here so the two knob laws are visible
// beside the engine rather than buried in the render loop.
const int32_t kQuestionMarkDitBase = 3600;
const int32_t kQuestionMarkNoiseFloorBase = 1024;

// digital_oscillator.cc:2246. The intensity walk's value at strike; >> 8 puts
// it at 127, below every reachable noise_threshold, so the bed starts at its
// floor and climbs from there.
const int32_t kQuestionMarkSeedAtStrike = 32767;

// digital_oscillator.cc:2244. Ticks of silence before the first symbol.
const int16_t kQuestionMarkLeadInTicks = 10;

// digital_oscillator.cc:2272. The terminator's silent gap before the message
// repeats.
const int16_t kQuestionMarkTerminatorTicks = 100;

// digital_oscillator.cc:2288-2289. The intensity ceiling.
const int32_t kQuestionMarkMaxIntensity = 16000;

// digital_oscillator.cc:51-53. RenderQuestionMark's phase_increment_ comes from
// DigitalOscillator::ComputePhaseIncrement (:117), whose FIRST line saturates
// its argument at kPitchTableStart - 1 = 16383 -- MIDI 16383/128 = 127.9921875,
// 13.28 kHz. That is Braids' own ceiling and the carrier stops rising there
// however far the pitch CV goes, so R4 takes it. It is NOT the same as the
// note 136 plaits::NoteToFrequency CONSTRAINs at on its own: measured, an
// unclamped port plays 19965 Hz at note 135 where Braids plays 13281 -- seven
// semitones sharp. (The resulting increment is 0.277 expressed at the output
// rate, above kMaxFrequency but well under Nyquist; the carrier is a bare sine
// table read, so nothing folds. Braids' 4*f0 ring modulator aliases at the
// internal 96 kHz exactly as it does on the module.)
const float kQuestionMarkHighestNote = 127.9921875f;

// stmlib::Random's power-on state (stmlib/utils/random.cc). See the header's
// NOISE note for why this engine carries its own copy of the LCG.
const uint32_t kQuestionMarkRngSeed = 0x21;
const uint32_t kQuestionMarkRngSeedAux = 0x9e3779b9;

// Bed spans silence to well past the module; Grit spans clean to twice the
// module's depth. Both are exactly the module at 0.5 (ApplyMacro's midpoint is
// its stock argument).
const float kQuestionMarkMaxBed = 2.5f;
const float kQuestionMarkMaxGrit = 2.0f;

// ~1.53 Hz at 48 kHz (c * fs / 2pi) -- more than three and a half octaves
// below the lowest band ab_compare analyses (20 Hz), so it removes the
// distortion stage's DC without touching anything the keying puts into the
// audio band.
const float kQuestionMarkDcCoefficient = 0.0002f;

class QuestionMarkEngine : public Engine {
 public:
  QuestionMarkEngine() { }
  ~QuestionMarkEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_QUESTION_MARK; }
  virtual bool fast_fm_capable() const { return true; }

 private:
  void Strike();

  // Braids' phase_, at the 96 kHz internal rate.
  uint32_t phase_;

  // state_.clk, under names that say what the fields do here.
  uint32_t tick_phase_;       // state->cycle_phase
  uint32_t symbol_index_;     // state->cycle_phase_increment
  uint32_t key_;              // state->rng_state
  int32_t seed_;              // state->seed
  int16_t ticks_left_;        // state->sample

  uint32_t rng_state_;
  uint32_t rng_state_aux_;

  float dc_state_;
  float dc_state_aux_;

  DISALLOW_COPY_AND_ASSIGN(QuestionMarkEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_QUESTION_MARK_ENGINE_H_
