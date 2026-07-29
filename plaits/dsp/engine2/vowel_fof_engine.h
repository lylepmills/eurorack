// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' VFOF: a bank of five very narrow state-variable filters excited by
// a band-limited saw, sweeping five vowels across five vocal registers.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderVowelFof. It ends
// in `size -= 2`, so it is a 48 kHz algorithm, and TWO of its three half-rate
// adjustments are exact identities here and correctly drop out: the
// `phase_increment_ << 1`, and the `+ (12 << 7)` octave offset (lut_svf_cutoff
// is generated at 96 kHz while the bank runs once per two output samples).
//
// The third, `*buffer++ = (out + previous_sample) >> 1`, is NOT a compensation
// and does not cancel. It is a 2x linear interpolator reconstructing 96 kHz
// from the 48 kHz algorithm, so referred to the algorithm rate it is a real
// lowpass: (1 + cos(2 pi f / 96000)) / 2, which is -0.95 dB at 10 kHz and
// -6.0 dB at 24 kHz. It is deliberately NOT reproduced. It belongs to the
// module's output stage rather than to the algorithm, and at 48 kHz it is a
// half-sample fractional delay rather than a two-tap average (a two-tap average
// at 48 kHz is -2.0 dB at 10 kHz, twice too much), so a faithful copy costs a
// fractional-delay filter on the tightest engine in the port. The price is that
// the port runs brighter than the module above 10 kHz. On the corrected linear
// A/B, `stock-mid` reads +1.72 dB at 10-20.5 kHz and +5.66 dB at 20.5-24 kHz;
// those bands hold 2.3% of the energy and the energy-weighted overall spectral
// difference is 0.15 dB. The output averager contributes to the excess, but an
// older attempt to separate its share used a port WAV already clipped by the
// preview harness, so that split is deliberately no longer quoted. Declared,
// not corrected; `ab_engine.py --bands` reports every band.
//
// THE FINDING THAT JUSTIFIES THE ENGINE, confirmed in the source:
//   out += svf_bp[i] * amplitudes[0] >> 17;
// reads `amplitudes[0]`, not `amplitudes[i]` -- and column 0 of the amplitude
// table is 16384 in all 25 rows. So 100 of the 125 amplitude entries have
// been dead since 2013 and every formant is voiced flat. MACRO restores them:
// at the detent the bank is flat, which is what hardware does; above it the
// formant balance the table always intended.
//
// The amplitudes are LINEAR with 16384 as unity, not semitone attenuations,
// so the tilt is a linear interpolation between flat and true rather than a
// ratio law. Built the other way it would be shaping the wrong quantity.
//
// TIMBRE is the VOWEL and MORPH the REGISTER, which is what Braids does and is
// NOT what `speech` does. `RenderVowelFof` calls InterpolateFormantParameter(
// table, parameter_[1], parameter_[0], i) -- COLOR is the OUTER index -- and
// the outer dimension of `formant_f_data` is the register (its own `// bass`
// .. `// soprano` block comments; /a/ F1 rises 601 -> 795 Hz across it) while
// the middle one is the vowel. The tables here are vendored in that same
// [register][vowel][formant] order and `InterpolateFormant` passes MORPH as the
// outer index, so the port agrees with the module exactly.
//
// An earlier revision of this header claimed the two were swapped to line up
// with `speech`. They never were -- the transpose was described but never
// applied -- so the panel labels named the wrong axes for the engine's whole
// shipping life. The LABELS were corrected, not the mapping: the mapping is
// faithful, and moving it would silently re-voice every patch already saved
// against this engine. The cost is that `vowel-fof` and `speech` run their
// vocal axes opposite ways round, which is a real wart and is Braids' own.
//
// HARMONICS is new: Braids' excitation is a bare saw, and this crossfades it
// toward noise so the bank can whisper. The noise gets a per-formant makeup
// of 1/sqrt(f) because a Q = 64 bandpass has bandwidth fc/64 -- 9.4 Hz at a
// bass F1 against 77 Hz at a soprano F5, an 8:1 energy spread that one
// constant cannot level.
//
// OUT: the bank under the current tilt.
//
// AUX (mono): the GLOTTAL SOURCE, ahead of the bank -- the saw crossfaded to
// noise by HARMONICS, at one neutral makeup instead of the five per-formant
// ones. The raw-exciter idiom (inharmonic-string, modal-resonator and
// particle-noise all put theirs on AUX, and all three drop it in stereo), and
// the natural sibling of speech, whose AUX is its secondary path. Cheaper than
// what it replaces, on the engine with the least headroom in the port.
//
// In stereo OUT/AUX become L/R and AUX reverts to the same five filter outputs
// summed with the formant weighting REVERSED, so it leans on the upper
// formants -- a different weighted sum of state the engine has already
// computed, not a second bank (a second five-filter bank is a full extra
// render path and z-filter measured what those cost). Two weightings of one
// bank make a decent L/R pair; a bare source would not.
//
// That reversal is at FULL strength and does NOT track MACRO. An earlier
// revision scaled it by the same `tilt` MACRO drives, and `tilt` is exactly
// 0.0f at the detent, so L and R were bit-identical -- correlation 1.000000 --
// in the state the voice ships in: a stereo engine that was mono by default.
// It now runs at a channel correlation of 0.45-0.83 at the eight positions the
// makeup was fitted over; across the whole reachable grid that widens to
// 0.32-0.996 at the detent, and off the detent the pair goes hard-panned. See
// kVowelFofStereoMakeup, which carries the measured ranges and the points they
// occur at.
//
// TRIG has no counterpart in the model. `RenderVowelFof` never reads `strike_`,
// so the module ignores a strike on this shape entirely, and there is no
// envelope here either (`*already_enveloped = false`). A rising edge re-inits
// the bank -- the five SVF states zeroed and the noise reseeded -- which is a
// re-articulation, not a re-triggered envelope. It is cheap against the model:
// at 8 Hz `tests/ab.json` reads +0.01 dB AC RMS and 0.42 dB spectral
// difference, against +0.06 / 0.15 untriggered, because a Q = 64 bank refills
// in about 40 ms. These are pre-Voice, linear A/B figures; the firmware's
// negative output gain takes the separate limiter path.

#ifndef PLAITS_DSP_ENGINE2_VOWEL_FOF_ENGINE_H_
#define PLAITS_DSP_ENGINE2_VOWEL_FOF_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/oscillator.h"

namespace plaits {

const int kVowelFofNumFormants = 5;
const int kVowelFofGridSize = 5;

// Braids' `notch = in - (svf_bp >> 6)`: Q = 64.
const float kVowelFofDamping = 1.0f / 64.0f;

// `out += svf_bp[i] * amplitudes[0] >> 17` with amplitudes[0] = 16384 is a
// gain of 0.125 per formant.
const float kVowelFofOutputScale = 0.125f;

// Braids excites the bank with a UNIPOLAR saw: `next_saw_sample += phase >> 17`
// yields 0..32767, i.e. 0..1 with a mean of 0.5. Plaits' Oscillator builds that
// same 0..1 saw internally -- same BLEP polynomial, `0.5 t^2` either side --
// and then emits `2 * this_sample - 1`, so `0.5f * (x + 1.0f)` recovers Braids'
// sample exactly.
//
// The DC is load-bearing, not decoration. The Chamberlin lowpass state settles
// at its input's DC, so on hardware it sits at half scale and hard-clips
// against +-1 on 5-21% of samples; that clipping is where most of this engine's
// ring comes from. An earlier revision matched only the peak-to-peak with a
// bipolar saw centred on zero, whose state clipped on 1-8% of samples, and it
// cost 2.0-2.9 dB of level wherever the excitation is dense.
const float kVowelFofExcitationScale = 0.5f;

// Mono AUX is the glottal source, which is a full-scale saw where OUT is five
// very narrow bandpasses at an eighth gain each -- so it arrives far hotter and
// has to be scaled down. 0.387f was fitted over eight fixed grid positions
// (both ends of both Braids axes, the interior, an off-node point, and notes 33
// and 81, all at the stock exciter with MACRO at the detent), where AUX lands
// within +-0.68 dB of OUT -- which is why aux_gain stays equal to out_gain.
//
// THOSE ARE EIGHT POINTS, NOT A SPAN, and the difference matters. Measured over
// what the engine can actually reach, still at the stock exciter and still at
// the MACRO detent -- notes 21 to 93 against the full vowel x register grid,
// 2025 points -- AUX against OUT runs
//
//     -4.95 dB  (note 90, TIMBRE 0.25, MORPH 0.50)
//     +3.50 dB  (note 21, TIMBRE 0.50, MORPH 1.00)
//
// and restricting to the four octaves the fit spans does not rescue it: notes
// 33 to 81 over the same grid still run -4.15 .. +1.59 dB. What the +-0.7 dB
// figure really describes is MID REGISTER: note 45 across the whole vowel x
// register grid is -0.73 .. +0.06 dB. Away from there it is the pitch that
// moves, because AUX is the exciter and its level barely follows the note while
// OUT follows how many harmonics land inside the bank. `tests/ab.json` renders
// both ends of that: +3.1 dB at note 21, -3.9 dB at note 93.
//
// The gain was NOT re-fitted to close that, because no constant can. The spread
// is 8.5 dB wide and pitch-driven, so a constant only slides it: the
// best-centred value would still leave +-4.2 dB, and it would spend the
// mid-register match -- the one operating point where the two ARE a matched
// pair -- to buy nothing. 0.387f is kept and the range is declared instead.
//
// Two further axes widen it, both measured over the same note x grid:
//
//   HARMONICS darkens AUX faster than OUT. At full breath, at the detent, AUX
//   runs -6.45 .. -3.60 dB against OUT -- always under it, never within a dB.
//
//   MACRO. Full tilt costs OUT several dB (attenuating formants 2-5 is the
//   point of the control) while leaving AUX untouched, so at MACRO 1.0 AUX is
//   hotter EVERYWHERE: +1.18 dB at its closest, +20.7 dB at note 93 /
//   TIMBRE 1.00 / MORPH 0.00.
//
// Over the whole HARMONICS x MACRO x note x grid plane the two span -13.2 dB to
// +20.8 dB. The manifest gains are set for one operating point, and this engine
// does not hold that point outside mid register at the stock exciter.
//
// It also did not hold before the unipolar saw and tap-saturator fixes: the
// quieter bank made AUX disproportionately hot. The old numeric range is not
// quoted because it came from the superseded clipped A/B harness.
const float kVowelFofSourceGain = 0.387f;

// Makeup for the stereo AUX's reversed weighting. Reversing the table puts its
// F5 entry (0.003-0.1) on the F1 tap, and F1 carries most of the bank's energy,
// so the reversed sum lands far under OUT and needs a fixed makeup to be usable
// as the other half of an L/R pair. Fitted at the MACRO detent, which is where
// the voice ships, over the SAME eight fixed positions kVowelFofSourceGain used
// -- there L/R sits within +-1.66 dB at a channel correlation of 0.45-0.83 (it
// was exactly 1.0 -- bit-identical -- before the detent fix).
//
// Same caveat as kVowelFofSourceGain and for the same reason -- R is the bank
// under a FIXED weighting while L follows MACRO and the note -- so those eight
// numbers are a point estimate, not the grid:
//
//   AT THE DETENT, over notes 21 to 93 against the full vowel x register grid
//   (2025 points), the pair runs -4.90 dB (note 21, TIMBRE 1.00, MORPH 1.00) to
//   +4.01 dB (note 90, TIMBRE 0.00, MORPH 1.00), at a correlation of 0.32 to
//   0.996. The TOP of that correlation range is the part worth knowing: near
//   note 93 at TIMBRE 0.75 / MORPH 0.375 the two weightings pick out very
//   nearly the same signal and the image narrows back to almost mono. It is not
//   the bit-identical mono the pre-fix version had everywhere -- but "0.45-0.83
//   correlation" describes eight points, not the corner of the grid where the
//   width quietly goes away.
//
//   OFF THE DETENT it stops being a pair at all. At MACRO 1.0 full tilt leaves
//   OUT almost nothing while R is untouched, so R runs hotter than L over 98%
//   of the grid -- median +5.4 dB, 28% of it past +10 dB, only 41 of 2025
//   points the other way round at all (worst -0.83 dB) -- and it reaches
//   +24.0 dB at note 93 / TIMBRE 1.00 / MORPH 0.50; with HARMONICS in play the
//   worst measured point is +25.2 dB (note 86, HARMONICS 0.25, TIMBRE 0.56,
//   MORPH 0.50). Correlation goes NEGATIVE up there too -- -0.40 at note 93 /
//   TIMBRE 0.125 / MORPH 0.75, where the imbalance is +13.8 dB -- so the two
//   channels are partly out of phase as well as lopsided. Over the whole
//   HARMONICS x MACRO x note x grid plane L/R spans -8.1 dB to +25.2 dB. A
//   stereo AUX at full MACRO is a hard-panned image by construction, not a
//   balanced pair, and neither this header nor the README used to say so.
//
// Half-strength reversal was measured as the alternative: correlation 0.94-0.98
// at +-0.8 dB. Full reversal was taken -- a stereo mode whose channels are 96%
// the same signal is not worth the aux output, and the level drift across a
// vowel sweep is inaudible next to the width it buys.
const float kVowelFofStereoMakeup = 2.21f;

class VowelFofEngine : public Engine {
 public:
  VowelFofEngine() { }
  ~VowelFofEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern B: mono AUX is the glottal source; the stereo branch replaces it
  // with the reversed formant weighting so L/R stay a matched pair.
  virtual bool stereo_capable() const { return PLAITS_STEREO_VOWEL_FOF; }

 private:
  Oscillator excitation_;
  float svf_lp_[kVowelFofNumFormants];
  float svf_bp_[kVowelFofNumFormants];
  uint32_t noise_state_;

  DISALLOW_COPY_AND_ASSIGN(VowelFofEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_VOWEL_FOF_ENGINE_H_
