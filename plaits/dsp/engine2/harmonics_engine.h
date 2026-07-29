// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' HARMONICS model: twelve integer partials whose amplitudes are drawn
// as the sum of two Lorentzian bumps over the harmonic index, normalized to
// unit sum and slewed toward with an integer one-pole.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderHarmonics
// (braids/digital_oscillator.cc:922-987).
//
// WHICH BRAIDS PARAMETER DOES WHAT, with lines (braids/digital_oscillator.cc
// unless noted). RenderHarmonics reads parameter_[0] and parameter_[1]
// DIRECTLY -- it contains no BEGIN_INTERPOLATE_PARAMETER_* macro at all -- so
// the fold trap (the suffix is the parameter INDEX, not an ordinal) has no
// surface here. It is still proved rather than assumed: tests/ab.json holds a
// case at each end of each axis, and re-running the suite with the engine's
// TIMBRE and HARMONICS exchanged blows peak-low out to 58.10 dB of spectral
// difference and peak-high to 25.62 dB, against 0.03 and 0.04 dB unswapped.
// macro_oscillator.h:73-78 fixes parameter_[0] = TIMBRE, parameter_[1] = COLOR,
// and macro_oscillator.cc:356 forwards both to the digital oscillator unchanged.
//
//   :53 (digital_oscillator.h)  kNumAdditiveHarmonics = 12. A fixed count, not
//        a parameter: partials 1..12 of the played note, nothing else.
//   :932  peak = (12 * parameter_[0]) >> 7. TIMBRE. Positions are carried in
//        units of 1/256 of a harmonic (x = i << 8 at :942), so peak sweeps the
//        first bump from partial 1 to partial 12.
//   :933  second_peak = (peak >> 1) + 12 * 128. TIMBRE again, WELDED: the
//        second bump always sits six partials above HALF the first one's
//        index. It is not independently reachable on the module.
//   :934  second_peak_amount = parameter_[1] * parameter_[1] >> 15. COLOR
//        squared: the second bump's height, 0 to 32766 against the first
//        bump's fixed 32768.
//   :936-939  width, also COLOR, and a TRIANGLE in it:
//        sqrtsqrt = p1 < 16384 ? p1 >> 6 : 511 - (p1 >> 6), then squared,
//        >> 10, squared again, + 4. So width runs 4 -> 3973 -> 4 across the
//        knob. COLOR therefore drives BOTH the bump width and the second
//        bump's height, and drives them in opposite directions across the
//        upper half: "wide with a strong second bump" and "narrow with a weak
//        one" are both off the module's map.
//   :945-949  g = 32768*128 / (128 + d*d/width), summed for both bumps. A
//        Lorentzian in harmonic index, so the skirts fall as 1/d^2 and never
//        reach zero.
//   :954-960  attenuation = 2147483647 / total, then g * attenuation >> 16:
//        the twelve amplitudes are normalized to sum to 32768. That pins the
//        PEAK, not the loudness -- one partial and a twelve-partial stack at
//        the same peak have very different crest factors, and the reference
//        renders bear it out: AC RMS runs -3.33 dB at COLOR 0 against
//        -13.70 dB at COLOR 0.5, same TIMBRE. Do not read this as levelling.
//   :956  THE PITCH-DEPENDENT PARTIAL GUARD -- and it is a truncated integer.
//        `(phase_increment >> 16) * (i + 1) > 0x4000` zeroes partial i when its
//        frequency passes a quarter of the inner sample rate (12 kHz at 48 k),
//        using the TOP 16 BITS of the phase increment, not the exact value.
//        The port takes the same floor (kHarmonicsIncrementScale below) rather
//        than the float closed form.
//   :973-974  the render loop: out += sine(phase * (i+1)) * amplitude[i] >> 15,
//        then amplitude[i] += (target_amplitude[i] - amplitude[i]) >> 8.
//
// THE >> 8 SLEW IS NOT A SMOOTHER, IT IS A GATE, and the port reproduces it as
// integers for that reason. An arithmetic shift floors, so a positive
// difference below 256 shifts to zero and the amplitude STOPS: from below, a
// partial settles somewhere in [target-255, target], and a partial whose
// target is under 256 (out of 32768, i.e. -42 dB) never leaves zero at all.
// digital_oscillator.h:246-258 memsets state_ to zero in Init(), and
// :111-115 calls Init() on every shape change, so zero is exactly where Braids
// starts -- the gate is what the module does on every note, not a transient.
// Measured on the settled targets over the full TIMBRE x COLOR plane at 0.05
// steps (441 settings, low enough in pitch that the guard is not involved):
// 65% of settings render fewer than twelve partials, the audible count runs 1
// to 12 with a median of 9, and the settled sum sits 0.17 to 0.85 dB under the
// normalized 32768 (mean 0.72 dB). Evaluating the slew in float instead would
// put every one of those partials back and remove that loss.
//
//   HARMONICS  Colour     Braids' COLOR: bump width and second-bump height,
//                         welded together as the module has them.
//   TIMBRE     Peak       Braids' TIMBRE: the first bump's partial index.
//   MORPH      Spread     moves the second bump off the +6-partial weld, by
//                         -6 to +6 partials. Zero at noon, which is Braids.
//   MACRO      Width      scales the bump width 1/16x to 16x around whatever
//                         Colour set, so width and second-bump height come
//                         apart. 1.0x at noon, which is Braids.
//
// LINEAGE. Plaits' harmonic engine (plaits/dsp/engine/additive_engine.cc) is
// the descendant. Four differences are readable straight off the source:
//   - 24 integer partials (additive_engine.h:45) against Braids' 12.
//   - its bump is `gain = 1 - |i - center| * slope`, rectified and then raised
//     to the eighth power (:81-91) -- COMPACT SUPPORT, so every partial past
//     1/slope of the centre is exactly zero. Braids' Lorentzian skirt falls as
//     1/d^2 and is never zero, though its own >> 8 gate truncates it.
//   - its slew is `ONE_POLE(amplitudes[j], gain, 0.001f)` in float (:103),
//     which has no gate at all; Braids' integer >> 8 does, per above.
//   - its ripple count (`bumps`, :141) is driven by HARMONICS and can put many
//     peaks across the series; Braids has exactly two, at a fixed relation.
//     Spread is the control that breaks that relation. MACRO there is an
//     odd/even balance (:149-154); MACRO here is width.
// Measured rather than asserted, at a pairing stated in full: note 45, 3 s,
// both engines at TIMBRE 0.5 and MACRO 0.5 (each engine's stock fourth macro),
// each with its bump at its widest -- Harmonics at Colour 0.5, the neighbour
// at MORPH 0.0 with HARMONICS 0.0 so its ripple is off -- and level-matched by
// ab_compare.py, the two differ by 4.65 dB energy-weighted across the
// spectrum. The difference is NOT in the formant: the 640-1280 Hz octave
// carrying 70% of the energy differs by 3.9 dB, while the bands above the
// twelfth partial (12 x 110 Hz = 1.3 kHz) differ by 24.1 dB and 59.9 dB,
// which is the neighbour's partials 13-24 continuing where this engine stops.
// So on this measurement the partial COUNT dominates and the skirt-shape
// difference does not separate out; the skirt claim above rests on the two
// formulas, not on this number. Reported as a difference between two engines,
// not as a fidelity figure for either.
//
// RATE (SPEC R5). RenderHarmonics ENDS IN `size -= 2` (:980) and writes two
// output samples per iteration from one `phase += phase_increment` (:967), so
// the inner algorithm runs at 48 kHz and every constant in it transfers
// verbatim to a 48 kHz port. There is no internal oversampling: :928 doubles
// the increment exactly once, which is what makes the inner rate half of
// Braids' 96 kHz output rate. The `(out + previous_sample) >> 1` pair at
// :977-978 is Braids' own 2x linear interpolator up to 96 kHz, not part of the
// synthesis. The port writes the 48 kHz stream directly and does not
// oversample; the guard at :956 needs no re-derivation because it is stated
// against the same 48 kHz inner rate the port runs at.
//
// ANTI-ALIASING (SPEC R7), measured, not asserted. No filter is fitted: the
// guard at :956 IS the anti-aliasing measure, because every partial the model
// renders is explicit and the guard silences each one before it reaches fs/4.
// The only content beyond those twelve partials is the sine table's
// linear-interpolation error. Stated as non-harmonic energy against total,
// measured on the A/B renders with a 16384-point Blackman-Harris FFT
// (-92 dB sidelobes) excluding +/-6 bins around every harmonic:
//
//                                    port      Braids ref
//   note 45, both knobs at noon     -85.5 dB    -79.1 dB
//   note 45, Colour 0               -87.5 dB    -88.5 dB
//   note 45, Peak 1.0               -87.1 dB    -80.7 dB
//
// So the port is at or below the module everywhere measured, and no filter
// would have anything to remove.
//
// OUT: the model. AUX (mono): the same model with Peak reflected (1 - TIMBRE),
// so the two carry opposite ends of the harmonic series and coincide when Peak
// is centred, as fold's pair does at its blend centre. In stereo OUT/AUX become
// L/R and the two sides take small opposing Peak offsets instead of the full
// reflection.
//
// LEVEL (SPEC R1). The gains are POSITIVE, which is the rarer branch and is
// justified here: the output is a bounded expression of bounded terms after
// every stage. :954-959 normalizes the twelve targets to sum to at most 32768
// (attenuation is floored, so the sum cannot exceed it), the slew only ever
// approaches a target from one side, and Braids' sine table peaks at 32639
// (measured below), so |out| <= 32639/32768 = 0.996 for every parameter
// setting including the two new macros, which change only the g[i] going into
// the same normalizer. There is no DC blocker and no feedback loop. A final
// CONSTRAIN to +/-1 stands in for Braids' CLIP (stmlib.h:39) and is not
// expected to engage. The bound is tight rather than theoretical, because
// -cos aligns all twelve partials at phase 0: the largest peak `check --full`
// reads across the five scenarios is 0.7903 with |out_gain| 0.8 applied, i.e.
// 0.988 in the engine domain, and |DC| stays at or below 0.00005.
//
// Declared deviations from Braids:
//   - the sine is Plaits' 512-entry lut_sine read once for the fundamental,
//     with -cos(n*x) generated for the remaining partials by the exact
//     harmonic cosine recurrence in the .cc, not Braids' wav_sine.
//     Re-starting that recurrence every sample bounds numerical error and
//     reduces the calibrated hardware estimate from 587.4 instructions /
//     sample (113% of budget) to 384.4 (74%); the full A/B suite remains
//     inside tolerance. wav_sine reproduces EXACTLY as
//     round(-32639 * cos(2*pi*i/256) + 127) to within 2 LSB across all 257
//     entries -- the residual is the order-2 dither its generator applies
//     (braids/resources/waveforms.py:87-98); a 256-point DFT of one full
//     period puts every harmonic above the first at or below -99.5 dB relative
//     to it (the largest is the Nyquist bin, which is where an order-2 shaper
//     puts its noise), i.e. at the int16 quantisation floor rather than at any
//     audible structure. The port keeps
//     the 32639/32768 amplitude and drops the +127 DC (0.39% of full scale),
//     which is an artefact of `scale(center=True)` averaging a 257-entry array
//     over a 256-sample period. The port's table is also 512 entries against
//     Braids' 256, so its linear-interpolation error is smaller.
//     MEASURED, not assumed: the sine feeds only a linear sum here (:187-189,
//     the twelve partials are weighted by amplitude_[i]/amplitude_aux_[i] and
//     added; there is no wavefolder, waveshaper, clipper, comparator, or
//     feedback path anywhere in this engine, and Voice::Render carries no
//     downstream DC blocker for this engine's output either -- so nothing
//     downstream removes the pedestal, it would just ride through as a static
//     offset). Reproducing wav_sine exactly (amplitude 32639/32768 -- 32638
//     fits the table equally well and is indistinguishable under its own
//     dither -- plus the +127/32768 pedestal folded per-partial as
//     [target_amplitude-weighted sum]*127/32768^2, matching :973-974's
//     `sine(phase)*amplitude[i] >> 15` exactly) was implemented and re-run
//     through `ab_engine.py --bands` across all seven tests/ab.json cases.
//     Every top-line AC RMS, pitch, and energy-weighted spectrum number was
//     UNCHANGED to the reported precision (one case, colour-narrow, showed a
//     0.2->0.1 cent pitch wobble, inside the tracker's existing 0.0-0.2 cent
//     noise floor on that case, not attributable to the pedestal). The only
//     per-band movement was in bands carrying 0.0% of signal energy (FFT
//     leakage into empty near-DC bins), consistent with the algebra: the
//     pedestal is `(sum_i target_amplitude[i]) * 127/32768^2`, bounded by
//     127/32768 = 0.00388 of full scale, added identically to every sample
//     regardless of phase -- literally DC, which an AC-RMS/spectrum harness
//     cannot see and which does not push the output past the +/-1 CONSTRAIN
//     (SPEC R1's 0.996 bound + 0.00388 stays under 1.0, so it does not even
//     add a clip). Reverted rather than kept, per the no-churn-on-unmeasurable-
//     change rule: the port keeps the unit zero-mean sine (kHarmonicsSineAmplitude,
//     no DC term) and this note stands as the record of the measurement.
//   - the per-partial `>> 15` truncation at :973 is not reproduced; the port
//     accumulates the partial sum in float. Braids' floor there costs up to
//     one LSB per partial per sample, i.e. below -68 dB of the twelve-partial
//     sum, and biases DC by about -6/32768.
//   - the amplitudes and the whole target computation ARE reproduced as
//     integers, including the truncated width (:938-939 quantises width to 64
//     distinct values; at COLOR 0.25 it lands on 229 where the float closed
//     form gives 259.9, a 12% difference in the skirt), the floored attenuation,
//     and the >> 8 gate.
//   - Braids' 2x linear interpolation to 96 kHz (:977) is not reproduced.
//     Zero-stuffing convolved with [0.5, 1, 0.5] is a mild lowpass on the
//     48 kHz stream, magnitude cos^2(pi*f/96000): -0.02 dB at 1.3 kHz,
//     -0.37 dB at 6.3 kHz, -1.06 dB at 10.6 kHz. The port does not apply it,
//     so the port is slightly brighter and slightly louder at high notes. This
//     is the ONLY difference the A/B leaves anywhere: the guard-band case
//     (note 93) reads +0.81 dB AC RMS, and summing the settled amplitudes
//     through that same cos^2 predicts exactly 0.81 dB. Per partial, the
//     measured ratio of h1 to h6 differs by 1.03 dB between the two renders
//     where the formula predicts 1.02 dB.
//   - phase is smoothed toward the new note with a ParameterInterpolator;
//     Braids steps the increment per block.
//   - the guard's increment comes from NoteToFrequency, which is scaled by
//     kCorrectedSampleRate (SPEC R6), where Braids reads a lut_oscillator_
//     increments interpolation against a nominal 48 kHz. The two agree to
//     0.27%, so the port drops a partial 4.6 cents lower than the module does.
//     Only notes inside 4.6 cents of one of the twelve knees are affected.
//
// STEREO (SPEC R13, pattern B). Measured on 1 s at note 45, Colour 0.5, both
// new macros at noon: mono OUT/AUX correlate at +1.00 with Peak at 0.5 (they
// are the same stack, as documented) and at +0.43 with Peak at 0.2; the stereo
// pair correlates at +0.82 and +0.85 at those two settings, which is the
// intended small spectral widening rather than two different sounds. Peak is
// 0.9028 in the engine domain in every one of those four renders.
//
// A/B (tests/ab.json, `ab_engine.py --bands`). Six of the seven cases -- both
// ends of TIMBRE, three points of COLOR, and both at noon -- land at 0.00 to
// 0.01 dB AC RMS, 0.0 to 0.2 cents, and 0.03 to 0.15 dB energy-weighted
// spectrum. The seventh is the guard-band case above at +0.81 dB / 0.31 dB,
// accounted for to 0.01 dB by the missing interpolator. The parameter mapping
// is proved rather than assumed: re-running the suite with the engine's TIMBRE
// and HARMONICS exchanged takes peak-low to 58.10 dB of spectral difference and
// +9.83 dB of AC RMS, and peak-high to 25.62 dB and +5.13 dB, so the kind of
// swap fold shipped could not survive this harness.

#ifndef PLAITS_DSP_ENGINE2_HARMONICS_ENGINE_H_
#define PLAITS_DSP_ENGINE2_HARMONICS_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// digital_oscillator.h:53.
const int kHarmonicsNumPartials = 12;

// Positions are carried in 1/256 of a partial (x = i << 8, :942).
const int kHarmonicsPositionScale = 256;

// Braids' wav_sine is -32639 * cos(2*pi*phase) + 127, verified against
// braids/resources.cc to within 2 LSB over all 257 entries. The port keeps the
// amplitude and drops the DC.
const float kHarmonicsSineAmplitude = 32639.0f / 32768.0f;

// The guard at :956 reads (phase_increment >> 16), i.e. the top 16 bits of a
// 32-bit increment at the inner rate. This is the same floor.
const float kHarmonicsIncrementScale = 65536.0f;
const int32_t kHarmonicsGuardThreshold = 0x4000;

// Spread travels six partials either side of Braids' fixed +6 weld, which is
// far enough to put the second bump on the fundamental or past the twelfth.
const float kHarmonicsMaxSpread = 6.0f;

// Width scales multiplicatively around whatever Colour chose, so noon is
// exactly Braids at every Colour setting rather than at one of them.
const float kHarmonicsMinWidthScale = 1.0f / 16.0f;
const float kHarmonicsMaxWidthScale = 16.0f;

// The two channels take opposing Peak offsets in stereo. Small, because Peak
// is the model's main gesture and a wide split would make the two sides two
// different formants rather than a stereo image of one.
const float kHarmonicsStereoPeak = 0.12f;

class HarmonicsEngine : public Engine {
 public:
  HarmonicsEngine() { }
  ~HarmonicsEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_HARMONICS; }

 private:
  void ComputeTargets(
      float peak_position,
      float colour,
      float spread,
      float width_scale,
      int32_t increment_msb,
      int32_t* target);

  float phase_;
  float frequency_;

  int32_t amplitude_[kHarmonicsNumPartials];
  int32_t amplitude_aux_[kHarmonicsNumPartials];

  DISALLOW_COPY_AND_ASSIGN(HarmonicsEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_HARMONICS_ENGINE_H_
