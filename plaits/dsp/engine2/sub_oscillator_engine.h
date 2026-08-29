// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SUB models: a shaped main oscillator against a square sub, one or
// two octaves down.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderSub, which covers
// two display models -- SQUARE_SUB and SAW_SUB -- differing only in whether
// the main oscillator is AnalogOscillator::RenderSquare or
// AnalogOscillator::RenderVariableSaw. HARMONICS merges them into one
// continuous axis, which is the whole reason two Braids models fit in one
// palette slot: at 0.0 the main oscillator IS RenderSquare, at 1.0 it IS
// RenderVariableSaw, and in between the two waveforms crossfade off a single
// shared phase.
//
// MORPH is Braids' COLOR verbatim, and it is worth knowing its shape: the sub
// level is a V. Fully counter-clockwise gives a half-and-half mix with the sub
// two octaves down, the CENTRE gives no sub at all, and fully clockwise gives
// a half-and-half mix one octave down. The sub is at its loudest at BOTH ends.
//
// MACRO is new: Braids welds the sub to a plain square, and this narrows its
// pulse. The DETENT and everything ABOVE it are Braids (ApplyMacro's maximum
// equals its stock value); turning MACRO down from noon narrows the sub
// toward 12% pulse width, which Braids cannot do.
//
// OUT: the mix. AUX: the sub alone at full level -- not the mix-scaled sub,
// which would be silent at the centre of MORPH exactly where a player would
// go looking for it.
//
// -----------------------------------------------------------------------------
// THE ARITHMETIC, and why it is written out rather than approximated
// -----------------------------------------------------------------------------
//
// Both Braids shapes take their width from ONE 15-bit integer, and BOTH clamp
// it -- differently, and in opposite directions:
//
//   parameter_ = int16(TIMBRE * 32767)            macro_oscillator.cc:230
//
//   RenderSquare       parameter_ = min(parameter_, 32000)   :194-196
//                      pw = (32768 - parameter_) << 16       :206
//                      -> LOW fraction 0.5 down to 768/65536 = 0.011719
//
//   RenderVariableSaw  parameter_ = max(parameter_, 1024)    :344-346
//                      pw = parameter_ << 16                 :353
//                      -> ramp spacing 1024/65536 = 0.015625 up to 0.499985
//
// So TIMBRE runs the square's pulse CLOSED as it runs the saw's ramps APART,
// and each end of the travel is set by a clamp, not by the linear law. Both
// clamps are reproduced here on the integer, exactly as written. Nothing in
// this engine clamps a pulse width against FREQUENCY: Braids does not, and
// the previous implementation's frequency-dependent floor was the single
// largest error in the engine above MIDI 61 (see the changelog below).
//
// RenderVariableSaw's naive sample is `(phase_ >> 18) + ((phase_ - pw) >> 18)`
// on a wrapping uint32 phase (analog_oscillator.cc:414-415) -- the sum of TWO
// unit ramps `pw` apart. In the +-1 domain that closes to
//
//   phase <  pw:  2*phase - pw
//   phase >= pw:  2*phase - pw - 1
//
// two ramps of slope 2, each cut by a unit downward jump. It is a comb, not a
// shape morph: at TIMBRE 0 the ramps sit 1/64 cycle apart and null harmonic 32;
// at TIMBRE 1 they sit half a cycle apart, the waveform doubles to an octave-up
// saw and its amplitude HALVES. Both facts fall straight out of the closed
// form, and neither is reachable from a shape-morphing oscillator.
//
// RATE. MacroOscillator::RenderSub is a plain `while (size--)`
// (macro_oscillator.cc:245) and neither RenderSquare nor RenderVariableSaw
// oversamples internally -- each advances `phase_ += phase_increment` exactly
// once per output sample (analog_oscillator.cc:227, :376). A polyBLEP
// correction is a fraction of ONE sample period, so it is rate-relative by
// construction and no rate constant has to be re-derived for the 48 kHz port.
// (Same conclusion, same reasoning, as saw_square_engine.h.)
//
// -----------------------------------------------------------------------------
// Declared deviations from Braids (tests/ab.json, reproducible via ab_engine.py)
// -----------------------------------------------------------------------------
//
//  - DC is removed ANALYTICALLY, per sample, by subtracting each waveform's
//    exact naive mean (`1 - 2*pw` for a square; the variable saw's mean is
//    exactly zero, so it needs nothing). Braids leaves its offset in. This
//    replaces the 0.999-pole DC blocker the engine shipped with, whose 7.6 Hz
//    corner was NOT clear of the sub -- the two-octave sub crosses it at note
//    21 and cleared it by only 3.6x at note 45, and at note 24 the blocker
//    took 2.7 dB and 43 degrees off it. Subtracting the mean is exact at every
//    note, adds no phase shift and has no settling time, so it also removes
//    the per-trigger DC step the blocker's reset used to emit.
//    THE COST, measured: tests/ab.json `square-timbre-sweep` reads -0.51 dB
//    AC RMS / 1.70 dB spectrum and carries a case-specific 1.9 dB limit.
//    Braids' offset ramps 0.017 -> 0.949 across
//    that render (100 ms block means, measured), and 1.44 dB of the 1.70 is
//    that offset arriving in a metric this engine cannot answer. The two
//    lowest analysis bands are DC METERS by construction: at a 2048-point
//    frame the bin spacing is 23.44 Hz, ab_compare.spectral_difference puts
//    bin 1 inside BOTH the 20-40 Hz and the 40-80 Hz band, and a Hann window
//    lands a frame's DC offset in bin +-1 at a quarter of its bin-0
//    amplitude. So those two bands read 5.4% of the REFERENCE's energy at a
//    note whose lowest partial is 110 Hz and whose sub is silenced (MORPH
//    0.5), and this engine has nothing there: -34.36 dB and -11.78 dB.
//    THE CONTROLS, both reproducible from committed tools. (1) Strip the slow
//    DC from the reference and the port measures 0.44 dB / -0.16 dB against
//    it, with every band from 20 Hz to 10 kHz inside 0.4 dB. (2) Measure the
//    reference against its OWN DC-detrend and the metric reads 1.44 dB with
//    the same -33.44 / -11.15 dB in those two bands -- that is the floor for
//    ANY port that removes DC, and it is above the ordinary 1.0 dB baseline
//    on its own. The remaining 0.26 dB is NOT the DC policy; it is the top-octave
//    polyBLEP deficit described next (-1.92 dB at 10.2-20.5 kHz and -2.88 dB
//    at 20.5-24 kHz against the detrended reference).
//    The offset cannot be kept: at TIMBRE 0.9 it is 0.9 of full scale, which
//    the SDK's audio-health gate rejects outright (|DC| <= 0.2). Nor does the
//    old DC blocker score better -- a ramp this slow (0.32 per second) leaves
//    only 0.007 through a 7.6 Hz corner, so every DC-removing policy, filter
//    or subtraction, reads the same here.
//  - Float polyBLEP rather than Braids' int16 fixed-point BLEP (R8), and the
//    oscillator runs at 48 kHz where Braids runs it at 96 kHz. The KERNELS
//    are the same function: braids/analog_oscillator.h's `t * t >> 18` on a
//    16-bit t is stmlib's `0.5f * t * t` in fixed point, against a 32767 step.
//    So the whole of this deviation is the RATE, and it shows in two places.
//    (a) Everywhere, as a top-octave deficit -- roughly -1.2 to -2.0 dB at
//    10-20 kHz and -1 to -3 dB above 20 kHz, in every case including the
//    passing ones, because a 2-sample polyBLEP spans 41.7 us at 48 kHz and
//    20.8 us at 96 kHz. (b) Once the pulse is narrower than one 48 kHz sample
//    -- TIMBRE at maximum above roughly MIDI 61 -- where the port has half
//    Braids' room to band-limit it. tests/ab.json `square-timbre-max-high-note`
//    (note 84, TIMBRE 1.0, a 0.537-sample notch) reads -0.92 dB AC RMS /
//    1.35 dB spectrum. It carries a 1.6 dB spectral limit for the
//    reference-renderer artifact below and omits autocorrelation pitch.
//    THE SPECTRUM FIGURE THERE IS THE REFERENCE RENDERER, not this engine.
//    render_braids_model decimates 96 -> 48 kHz with a 127-tap halfband and
//    then clamps to int16, and a full-scale Braids square rings PAST full
//    scale through that filter: the clamp fires on 13.0% of samples even at
//    `square-stock` (5.5% at the ceiling, 7.5% at the floor), where it costs
//    the reference 0.17 dB, and on 47.4% here. Render the same setting with
//    `--rate 96000` and decimate in float with the same taps, and the port
//    measures 0.80 dB spectrum / -1.19 dB AC RMS against it -- inside the
//    case's own tolerance, with every band from 640 Hz to 24 kHz inside
//    1.3 dB. The raw cents figure comes from the 48 kHz grid, not the clamp
//    (the unclamped reference reports the identical -2398.2): the port's
//    period is 45.745 samples, so a 0.537-sample notch repeats its sub-sample
//    alignment every 4 cycles and autocorrelation prefers lag 183 (0.9843) to
//    lag 46 (0.9116). Braids' reference peaks at lag 46 (0.9703) because at
//    96 kHz that notch is 1.07 samples wide and the same polyBLEP resolves it.
//    Resolved at the fundamental the port reads -1.7 cents, inside tolerance;
//    autocorrelation is omitted because it identifies the wrong octave.
//    THE ESTIMATOR IS THE COMMON FACTOR, NOT THE NOTCH -- the notch is what
//    makes THIS case's lag-46 correlation weak, but note 84 breaks the metric
//    with no notch at all. tests/ab.json `saw-timbre-max-high-note` renders
//    the SAW at the same note and TIMBRE, where pw is 0.49998 and the
//    waveform is two half-cycle ramps rather than a thin pulse: its spectrum
//    is 0.38 dB and its AC RMS -0.22 dB -- both comfortably inside tolerance,
//    the waveform demonstrably right -- while autocorrelation still reads
//    -2399.0, so this control omits that metric too.
//    At note 84 four periods span 182.98 samples, so the 48 kHz grid repeats
//    its sub-sample alignment every 4 cycles for ANY waveform with real
//    energy near Nyquist, and autocorrelation prefers that lag.
//    (Not a deviation, but worth stating next to it: a trigger does NOTHING
//    to the oscillators, exactly as Braids' MacroOscillator::Strike() does
//    nothing to its analog pair -- tests/ab.json `square-timbre-max-retrigger`
//    renders both sides at 2 Hz and holds them to the same tolerance as the
//    untriggered cases.)
//  - Braids' shape-change re-Init is not reproduced. AnalogOscillator::Render
//    Init()s when shape_ changes (analog_oscillator.cc:75-78) and Init() puts
//    pitch_ back to 60 << 7, so the module's first 24-sample block renders at
//    MIDI 60. It is common-mode here -- RenderSub sets both oscillators'
//    shapes on the same first Render -- so main and sub stay in relative lock.
//  - HARMONICS itself is the port's own axis: Braids switches models with a
//    discrete menu, so only HARMONICS 0.0 and 1.0 are A/B-able. MACRO likewise
//    has no Braids equivalent; its stock plateau (detent and above) is Braids.
//  - AUX did not exist in Braids' SUB models (mono algorithm); it is
//    manufactured as the bare sub.
//
// -----------------------------------------------------------------------------
// What changed, and how an existing patch will sound different
// -----------------------------------------------------------------------------
//
// This engine shipped in wave 1 built on VariableShapeOscillator, and three
// things about that were wrong. All three are fixed here, and the result is
// that MOST of this engine's parameter space now sounds different. The one
// region that is genuinely untouched is HARMONICS at 0.0 with TIMBRE below
// about 0.88 -- Braids' SQUARE_SUB below the pulse-width knee. Everything
// else moved, and how much is measured below.
//
//  1. SAW_SUB WAS NOT PORTED, AND IT BLED INTO THE WHOLE HARMONICS AXIS.
//     The old oscillator's `waveshape` was `1 - 0.5 * HARMONICS`, which makes
//     VariableShapeOscillator's `square_amount` exactly `1 - HARMONICS` and
//     its `triangle_amount` zero (variable_shape_oscillator.h:137-138), so its
//     naive sample was `HARMONICS * phase + (1 - HARMONICS) * pulse`: a
//     crossfade between the pulse and a PLAIN RAMP. At HARMONICS 1.0 that
//     left a bare sawtooth with a dead TIMBRE knob -- square_amount is zero,
//     so pulse width reached the output through nothing at all -- but the
//     error was NOT confined to the top of the axis. Every setting with
//     HARMONICS above 0 was crossfading in a plain ramp where Braids has its
//     twin-ramp comb, and the comb's level and its notch positions both track
//     TIMBRE where the ramp did neither.
//     AUDIBLE CHANGE, measured by RENDERING both engines -- the wave-1 build
//     at git HEAD and this one -- at note 45 with MORPH at noon so the sub is
//     silenced, and taking OUT's AC RMS (dB, new vs old). These are whole
//     shipped signal paths, polyBLEP and all, not naive-waveform models:
//
//                    HARMONICS  0.00   0.25   0.50   0.75   1.00
//        TIMBRE 0.00           +0.01  -0.02  -0.07  -0.13  -0.18
//        TIMBRE 0.25           -0.01  -0.42  -0.92  -1.45  -1.71
//        TIMBRE 0.50           -0.11  -1.42  -3.20  -4.69  -3.58
//        TIMBRE 0.88           -1.11  -2.03  -3.55  -5.12  -5.85
//        TIMBRE 1.00           -2.72  -3.81  -5.21  -5.87  -6.03
//
//     So: the bottom of TIMBRE is safe at ANY HARMONICS (the whole row is
//     inside 0.21 dB, because a twin ramp 1/64 cycle apart is nearly a single
//     ramp); the left COLUMN is safe up to the pulse-width knee; and the whole
//     interior between them is quieter by 1 to 6 dB. It is a spectral change
//     as well as a level one -- at TIMBRE 0.5 / HARMONICS 1.0 harmonics 2, 6
//     and 10 are now ANNIHILATED by the comb (they sat at -6.0, -15.6 and
//     -20.0 dB under the old plain ramp), and at TIMBRE 0.5 / HARMONICS 0.5
//     the fundamental drops 3.2 dB with harmonics 5 and 9 down 4.0 and 4.6 dB.
//     A patch anywhere above HARMONICS 0 needs its level and its filter
//     re-checked, and near the top of both knobs it is a different sound, not
//     a trimmed one.
//  2. THE PULSE-WIDTH LAW WAS WRONG at the top of TIMBRE. The old
//     `pw = 0.5 - 0.48*timbre` reached 0.02 where Braids reaches 0.011719, and
//     diverged multiplicatively as the pulse narrowed. AUDIBLE CHANGE: above
//     about TIMBRE 0.88 the pulse is now genuinely thinner -- brighter, more
//     nasal, and a few dB quieter into the LPG at the same knob position.
//  3. THE FLOOR MOVED WITH PITCH, and Braids' does not.
//     VariableShapeOscillator::Render CONSTRAINs pw to [2f, 1-2f]
//     (variable_shape_oscillator.h:107-111); above roughly MIDI 61 that, not
//     TIMBRE, set the width, and above MIDI 71 TIMBRE was inert at the top of
//     its travel. AUDIBLE CHANGE: high notes with TIMBRE up now thin out the
//     way low notes always did, instead of stalling at a fat pulse.
//
// Three further changes are quieter but real, and they reach patches that
// never touch HARMONICS:
//
//  4. THE DC BLOCKER IS GONE, on BOTH outputs. Its 7.6 Hz corner sat on the
//     two-octave sub rather than below it, so low subs used to lose level and
//     phase; below about note 33 they now keep both (note 24, MORPH fully
//     down, went from -1.11 dB AC RMS against Braids to -0.01 dB).
//     AUX gets the same correction and gets it LARGEST AT MACRO'S STOCK
//     PLATEAU, not with MACRO turned down. A DC blocker is a linear filter:
//     what it took was the fundamental, and a 50% square puts more of its AC
//     energy there than a 12% one does, so narrowing the sub SHRINKS the
//     correction rather than growing it. Measured (AUX AC RMS, new vs old,
//     MORPH fully down, TIMBRE 0.5):
//
//                     MACRO 0.0   MACRO 0.25   MACRO >= 0.5
//        note 21        +1.24        +2.38        +2.72
//        note 24        +0.96        +1.85        +2.18
//        note 28        +0.68        +1.32        +1.51
//        note 33        +0.42        +0.82        +0.94
//        note 45        +0.11        +0.23        +0.26
//        note 60        -0.00        +0.04        +0.04
//
//     (An earlier draft of this note had that dependence backwards.)
//  5. A TRIGGER NO LONGER RESETS ANYTHING. Render() used to call Reset() on
//     TRIGGER_RISING_EDGE, which re-Init()ed both oscillators and zeroed the
//     DC blocker. That is gone, matching MacroOscillator::Strike(), and it
//     changes the ATTACK of every triggered patch three ways: no 0.54
//     full-scale DC step decaying over ~20 ms into the LPG, no one-block
//     pitch glide up from the oscillator's 0.01 default, and no phase reset
//     -- notes now start wherever the free-running phase happens to be, so a
//     short percussive envelope no longer gets the same click every time. On
//     a sustained patch this is inaudible; on a plucky one it is the attack.
//  6. MORPH SMOOTHS WHEN IT MOVES. The V's LEVEL law did not change -- the
//     old maximum blend was 0.5 and Braids' is 32766/65536 = 0.49997, so a
//     HELD MORPH is the same sub level it always was. What changed is that
//     the blend used to be computed once per block and now interpolates the
//     raw COLOR integer per sample and re-folds the V, as Braids does. A
//     swept or modulated MORPH no longer stair-steps at the block rate.
//  7. AUX DROPS AN OCTAVE AT MORPH'S EXACT CENTRE. The octave now comes off
//     Braids' integer -- `int16(MORPH * 32767) < 16384` picks the two-octave
//     sub -- and MORPH exactly 0.5 quantises to 16383, so it is the LOW side.
//     The old test was `MORPH - 0.5f < 0.0f`, which put exactly 0.5 on the
//     HIGH side. The two disagree only on MORPH in [0.5, 0.500016), and OUT
//     cannot show it there at all (the blend is exactly zero at noon, which
//     is the whole point of the V) -- but AUX carries the sub at full level
//     regardless of the blend, so AUX at a centred MORPH now sounds two
//     octaves down where it used to sound one. Measured at note 84, AUX's f0
//     goes 524.7 Hz -> 262.3 Hz; at note 72, 262.3 -> 131.2. Noon is a detent
//     position and it is exactly where a patch reaches for AUX (it is the one
//     setting where the sub is audible ONLY on AUX), so this is small in
//     span and easy to hit. It matches Braids -- ToParameter(0.5) is 16383
//     and macro_oscillator.cc:236 takes the 24-semitone branch -- and it is a
//     fix, but a patch using AUX at noon will hear it.
//
// The one region that sounds exactly as it did is HARMONICS 0.0, TIMBRE below
// about 0.88, note below about 61, MORPH held still and off its exact centre
// (see 7 -- AUX only), and no trigger.

#ifndef PLAITS_DSP_ENGINE2_SUB_OSCILLATOR_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SUB_OSCILLATOR_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids picks the sub octave from which side of centre COLOR sits on
// (macro_oscillator.cc:236) and always renders it as a plain 50% square
// (`set_parameter(0)`, macro_oscillator.cc:234-235).
const float kSubOscillatorLowOctave = -24.0f;
const float kSubOscillatorHighOctave = -12.0f;

// Braids' TIMBRE reaches both oscillators as one 15-bit integer, clamped in
// opposite directions by the two render functions.
//   RenderSquare:      parameter_ = min(parameter_, 32000)  -> pw floor 0.011719
//   RenderVariableSaw: parameter_ = max(parameter_, 1024)   -> spacing floor 1/64
const float kSubOscillatorParameterFullScale = 32767.0f;
const int32_t kSubOscillatorSquareParameterMax = 32000;
const int32_t kSubOscillatorSawParameterMin = 1024;

// Both render functions build their pulse width as `parameter_ << 16` against
// a 32-bit phase, i.e. a fraction with 65536 as its denominator.
const float kSubOscillatorParameterScale = 1.0f / 65536.0f;

// `sub_gain = (p2 < 16384 ? 16383 - p2 : p2 - 16384) << 1` (macro_oscillator.cc
// :247-248) tops out at 32766, which through stmlib's Mix() is 32766/65536 --
// so the sub never quite reaches an equal blend.
const float kSubOscillatorColourCentre = 16384.0f;

// MACRO's range for the sub's own pulse width. The maximum equals the stock
// value, so the detent and everything above it are Braids exactly.
const float kSubOscillatorSubPwStock = 0.5f;
const float kSubOscillatorSubPwMin = 0.12f;

class SubOscillatorEngine : public Engine {
 public:
  SubOscillatorEngine() { }
  ~SubOscillatorEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: the mix against the bare sub, decorrelated at matched gain.
  virtual bool stereo_capable() const { return true; }
  virtual bool fast_fm_capable() const { return true; }
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  virtual void HardSync() {
    phase_ = 0.0f;
    phase_sub_ = 0.0f;
  }
#endif

 private:
  // Both main waveforms run off ONE phase: Braids' two SUB models are the same
  // oscillator with a different render function, so the HARMONICS crossfade
  // only means anything if they are phase-locked.
  float phase_;
  float phase_sub_;

  float next_sample_square_;
  float next_sample_saw_;
  float next_sample_sub_;

  bool high_square_;
  bool high_saw_;
  bool high_sub_;

  float previous_pw_square_;
  float previous_pw_saw_;
  float previous_pw_sub_;

  // For interpolation of parameters.
  float frequency_;
  float pw_square_;
  float pw_saw_;
  float pw_sub_;
  float colour_;
  float shape_;

  DISALLOW_COPY_AND_ASSIGN(SubOscillatorEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SUB_OSCILLATOR_ENGINE_H_
