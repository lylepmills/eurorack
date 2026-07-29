// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SNARE: two decaying excitation pulses into two fixed-interval
// tonal resonators, and a decaying noise burst into a third, high, fixed
// resonator, all self-enveloping and summed.

#include "plaits/dsp/engine2/snare_engine.h"

#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' SVF coefficient formulas, re-derived from their closed forms
// (braids/resources/lookup_tables.py:98-107) -- see the header's RATE and
// TABLES notes for why this transfers at the table's own 96 kHz constant
// rather than being re-derived at Plaits' rate the way kick_engine.cc's
// KickResonanceDomain is.
inline float SvfFrequencyCoefficient(float note_semitones) {
  // NoteToFrequency(note) * kCorrectedSampleRate recovers the SAME raw Hz
  // value lookup_tables.py:98's `440 * 2^((note-69)/12)` computes (both
  // formulas share the A4=69=440 Hz reference; the multiply exactly cancels
  // NoteToFrequency's own kCorrectedSampleRate division), so this reuses
  // Plaits' own pitch LUT instead of a fresh powf/exp2f call. kCorrectedSampleRate
  // has NO effect on the result -- it cancels algebraically -- so SPEC R6 is
  // a non-issue for this specific quantity.
  const float cutoff_hz = NoteToFrequency(note_semitones) * kCorrectedSampleRate;
  float f_norm = cutoff_hz / kSnareSvfLutSampleRate;
  if (f_norm > kSnareSvfMaxNormalizedFrequency) {
    f_norm = kSnareSvfMaxNormalizedFrequency;
  }
  return 2.0f * sinf(float(M_PI) * f_norm);
}

inline float SvfResonanceDomain(float resonance_code) {
  // Interpolate824(lut_svf_damp, resonance << 17) indexes at
  // (resonance << 17) >> 24 = resonance >> 7 (svf.h:81); the generator's own
  // `resonance` (lookup_tables.py:102) is that index / 260.
  return resonance_code * (1.0f / 128.0f) * (1.0f / 260.0f);
}

inline float SvfDamp(float resonance_domain) {
  // lookup_tables.py:101-104's resonance term. The frequency-ceiling clamp
  // term is omitted -- it never binds for this engine's resonance codes,
  // verified against the real table; see the header's TABLES note.
  float damp = 2.0f * (1.0f - powf(resonance_domain, 0.25f));
  if (damp > 2.0f) {
    damp = 2.0f;
  }
  return damp;
}

// One Chamberlin SVF step (svf.h:78-98), bandpass output, in float.
inline float SvfStep(float in, float f, float damp, float& lp, float& bp) {
  const float notch = in - bp * damp;
  lp += f * bp;
  CONSTRAIN(lp, -kSnareClip, kSnareClip);
  const float hp = notch - lp;
  bp += f * hp;
  CONSTRAIN(bp, -kSnareClip, kSnareClip);
  return bp;
}

// braids/excitation.h, reproduced in float -- the same idiom kick_engine.
// cc's KickPulse uses, except this stores `level` from Trigger() (excitation.
// h's own level_ member) rather than taking it as a Process() argument:
// pulse 3's level varies per strike (see the header's PARAMETER MAPPING),
// where Kick's four levels never do.
struct SnarePulse {
  float state;
  int32_t counter;
  float level;

  inline void Reset() {
    state = 0.0f;
    counter = 0;
    level = 0.0f;
  }

  // Trigger does NOT reset state (excitation.h:56-59 has no such reset) --
  // a strike arriving mid-decay adds to whatever is still ringing down,
  // matching Braids exactly.
  inline void Trigger(int32_t delay_ticks, float lvl) {
    counter = delay_ticks + 1;
    level = lvl;
  }

  // excitation.h:61-63, checked AFTER Process() has run for this same
  // sample (contrast kick_engine.cc's KickPulse::DoneBefore, checked
  // BEFORE -- RenderSnare calls Process() before testing .done() at both
  // its call sites, digital_oscillator.cc:2429-2431 and :2434-2435, the
  // opposite order from RenderKick's :2348-2350).
  inline bool Done() const { return counter == 0; }

  // excitation.h:65-74. Braids' `state_` is an int32 in raw units, so
  // `state_ = state_ * decay_ >> 12` FLOORS every sample -- and that floor is
  // not a rounding detail here: pulse 3's coefficient is near unity (header
  // PARAMETER MAPPING), so the ~0.5-per-sample truncation loss overtakes the
  // exponential loss while the noise envelope is still ~29 dB above zero and
  // terminates it hard, where a pure float `state *= decay` rings on
  // asymptotically. Reproduced by truncating in Braids' own raw units --
  // `state` is never negative (Trigger adds fabsf(level)), so the int32 cast
  // IS floor, and costs two VCVT instructions rather than a libm floorf.
  inline float Process(float decay) {
    state = float(int32_t(state * 32768.0f * decay)) * (1.0f / 32768.0f);
    if (counter > 0) {
      --counter;
      if (counter == 0) {
        state += fabsf(level);
      }
    }
    return level < 0.0f ? -state : state;
  }
};

}  // namespace

void SnareEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void SnareEngine::Reset() {
  ever_struck_ = false;

  pulse0_state_ = 0.0f;
  pulse0_counter_ = 0;
  pulse0_level_ = 0.0f;
  pulse1_state_ = 0.0f;
  pulse1_counter_ = 0;
  pulse1_level_ = 0.0f;
  pulse2_state_ = 0.0f;
  pulse2_counter_ = 0;
  pulse2_level_ = 0.0f;
  pulse3_state_ = 0.0f;
  pulse3_counter_ = 0;
  pulse3_level_ = 0.0f;
  pulse3_decay_ = kSnarePulse3DecayBase * kSnarePulse3DecayNormalize;

  svf0_lp_ = 0.0f;
  svf0_bp_ = 0.0f;
  svf1_lp_ = 0.0f;
  svf1_bp_ = 0.0f;
  svf2_lp_ = 0.0f;
  svf2_bp_ = 0.0f;

  tone0_resonance_domain_ = SvfResonanceDomain(kSnareTone0ResonanceBase);
  tone1_resonance_domain_ = SvfResonanceDomain(kSnareTone1ResonanceBase);

  gain0_ = kSnareBalanceBase;
  gain1_ = kSnareBalanceBase;
  tone_mix_gain_ = 1.0f;
  noise_mix_gain_ = 1.0f;
  partial_spread_ = 1.0f;
}

void SnareEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  // The decay in this engine IS the note's whole loudness envelope --
  // Braids has no separate VCA stage, see the header's LINEAGE note.
  *already_enveloped = true;

  const bool stereo = PLAITS_STEREO_SNARE && parameters.stereo;

  const bool unpatched = (parameters.trigger & TRIGGER_UNPATCHED) != 0;
  const bool rising_edge = (parameters.trigger & TRIGGER_RISING_EDGE) != 0;
  // TRIGGER_UNPATCHED has no Braids equivalent; this follows struck_drum_
  // engine.cc's "unpatched fires once" convention -- see the header's
  // SELF-STRIKE note.
  const bool self_strike = unpatched && !ever_struck_;
  const bool strike = rising_edge || self_strike;

  float note = parameters.note;
  CONSTRAIN(note, kSnareMinNote, kSnareMaxNote);   // digital_oscillator.cc:120-124

  SnarePulse pulse0;
  pulse0.state = pulse0_state_;
  pulse0.counter = pulse0_counter_;
  pulse0.level = pulse0_level_;
  SnarePulse pulse1;
  pulse1.state = pulse1_state_;
  pulse1.counter = pulse1_counter_;
  pulse1.level = pulse1_level_;
  SnarePulse pulse2;
  pulse2.state = pulse2_state_;
  pulse2.counter = pulse2_counter_;
  pulse2.level = pulse2_level_;
  SnarePulse pulse3;
  pulse3.state = pulse3_state_;
  pulse3.counter = pulse3_counter_;
  pulse3.level = pulse3_level_;
  float pulse3_decay = pulse3_decay_;
  float tone0_resonance_domain = tone0_resonance_domain_;
  float tone1_resonance_domain = tone1_resonance_domain_;

  if (strike) {
    // digital_oscillator.cc:2400-2407, kept in Braids' own Q7-pitch-scaled
    // integer domain (pitch_ = note * 128) so every constant below
    // transfers at its literal source value -- see the header's PARAMETER
    // MAPPING.
    const float pitch_q7 = note * 128.0f;
    float decay = kSnareDecayPitchBase - pitch_q7;   // :2400
    // parameter_[1] is a true int16 in Braids -- floor it here so every
    // downstream comparison/arithmetic below matches Braids' integer
    // domain exactly, not just approximately.
    const float color_q15 = floorf(parameters.harmonics * 32767.0f);
    if (color_q15 >= kSnareColorDecayThreshold) {
      decay += color_q15 - kSnareColorDecayThreshold;   // :2401
    }
    if (decay > kSnareDecayMax) {
      decay = kSnareDecayMax;   // :2402-2404
    }

    // `decay >> 5` and `decay >> 14` are Braids' own integer right shifts,
    // i.e. FLOOR (decay is always > 0 in this engine's domain -- floorf
    // matches C++'s truncation for positive values). This is NOT a
    // sub-LSB rounding detail to smooth over: pulse_[3]'s decay coefficient
    // (below) sits within a few parts in 4096 of 1.0 (near-unity, i.e. a
    // very slowly decaying one-pole), so a fraction of one unit of
    // truncation error in `decay >> 14` compounds, over the thousands of
    // samples the noise envelope actually rings for, into a decay TIME
    // error of tens of percent -- reproducing the float closed form
    // continuously here, instead of the truncated integer, measured
    // +3 to +6 dB of excess mid/high-band energy on a real strike (the
    // "I used the mathematically correct value where Braids uses a
    // quantised one" trap, same class as particle-burst's resonator pole).
    tone0_resonance_domain = SvfResonanceDomain(
        kSnareTone0ResonanceBase +
        floorf(decay * kSnareResonanceDecayScale));   // :2405
    tone1_resonance_domain = SvfResonanceDomain(
        kSnareTone1ResonanceBase +
        floorf(decay * kSnareResonanceDecayScale));   // :2406
    pulse3_decay = (kSnarePulse3DecayBase +
        floorf(decay * kSnarePulse3DecayScale)) *
        kSnarePulse3DecayNormalize;   // :2407

    pulse0.Trigger(kSnarePulse0DelayTicks, kSnarePulse0Level);   // :2409
    pulse1.Trigger(kSnarePulse1DelayTicks, kSnarePulse1Level);   // :2410
    pulse2.Trigger(kSnarePulse2DelayTicks, kSnarePulse2Level);   // :2411

    float snappy = color_q15;
    if (snappy > kSnareSnappyCeiling) {
      snappy = kSnareSnappyCeiling;   // :2413-2415
    }
    pulse3.Trigger(kSnarePulse3DelayTicks,
        kSnarePulse3LevelBase + snappy * kSnarePulse3LevelSlope);   // :2416

    ever_struck_ = true;
  }

  // TIMBRE: the live tonal balance (digital_oscillator.cc:2424-2425).
  const float target_gain0 = kSnareBalanceBase -
      parameters.timbre * kSnareBalanceSpan;
  const float target_gain1 = kSnareBalanceBase +
      parameters.timbre * kSnareBalanceSpan;

  // MORPH: Snap balance (header THE FOURTH MACRO) -- kick_engine.cc's own
  // crossfade shape.
  float target_tone_mix_gain = 2.0f * (1.0f - parameters.morph);
  if (target_tone_mix_gain > 1.0f) {
    target_tone_mix_gain = 1.0f;
  }
  float target_noise_mix_gain = 2.0f * parameters.morph;
  if (target_noise_mix_gain > 1.0f) {
    target_noise_mix_gain = 1.0f;
  }

  // MACRO: Spread (header THE FOURTH MACRO). Read block-rate, not
  // ParameterInterpolator-smoothed per sample: SvfFrequencyCoefficient's
  // sinf is by far the most expensive thing in this engine's inner loop
  // (measured with qemu/estimate.py --sweep -- three per-sample sinf calls
  // put the whole engine at the edge of the module's CPU budget), and
  // Braids itself already retunes svf_[0]/svf_[1] only once per
  // RenderSnare() call, i.e. at the SAME block rate this now matches
  // (digital_oscillator.cc:2420-2422 sit outside the per-sample while
  // loop). A hand-turned macro stepping at Plaits' own block rate
  // (12-24 samples, well under 1 ms) is inaudible; this is not the same
  // tradeoff as smoothing an audio-rate-modulated Braids parameter like
  // TIMBRE or COLOR, so it costs nothing this port is trying to preserve.
  partial_spread_ = ApplyMacro(
      1.0f, 0.0f, kSnareMaxPartialSpread, parameters.macro);

  const float f0 = SvfFrequencyCoefficient(
      note + kSnareTone0SemitoneOffset * partial_spread_);
  const float f1 = SvfFrequencyCoefficient(
      note + kSnareTone1SemitoneOffset * partial_spread_);
  const float f2 = SvfFrequencyCoefficient(note + kSnareNoiseSemitoneOffset);

  ParameterInterpolator gain0_modulation(&gain0_, target_gain0, size);
  ParameterInterpolator gain1_modulation(&gain1_, target_gain1, size);
  ParameterInterpolator tone_mix_modulation(
      &tone_mix_gain_, target_tone_mix_gain, size);
  ParameterInterpolator noise_mix_modulation(
      &noise_mix_gain_, target_noise_mix_gain, size);

  float svf0_lp = svf0_lp_;
  float svf0_bp = svf0_bp_;
  float svf1_lp = svf1_lp_;
  float svf1_bp = svf1_bp_;
  float svf2_lp = svf2_lp_;
  float svf2_bp = svf2_bp_;

  // The two tonal resonators' resonance is a per-strike snapshot (above);
  // the noise resonator's is fixed for the engine's whole life
  // (digital_oscillator.cc:2393) -- see the header's PARAMETER MAPPING.
  const float damp0 = SvfDamp(tone0_resonance_domain);
  const float damp1 = SvfDamp(tone1_resonance_domain);
  const float damp2 = SvfDamp(SvfResonanceDomain(kSnareNoiseResonanceCode));

  while (size--) {
    const float gain0 = gain0_modulation.Next();
    const float gain1 = gain1_modulation.Next();
    const float tone_mix_gain = tone_mix_modulation.Next();
    const float noise_mix_gain = noise_mix_modulation.Next();

    // digital_oscillator.cc:2428-2431.
    const float pulse0_out = pulse0.Process(kSnarePulse0Decay);
    const float pulse1_out = pulse1.Process(kSnarePulse1Decay);
    const bool pulse1_active = !pulse1.Done();
    const float excitation0 = pulse0_out + pulse1_out +
        (pulse1_active ? kSnarePulse1Hold : 0.0f);

    // digital_oscillator.cc:2433-2435.
    const float pulse2_out = pulse2.Process(kSnarePulse2Decay);
    const bool pulse2_active = !pulse2.Done();
    const float excitation1 = pulse2_out +
        (pulse2_active ? kSnarePulse2Hold : 0.0f);

    // digital_oscillator.cc:2437.
    const float random_sample = Random::GetSample() * (1.0f / 32768.0f);
    const float pulse3_out = pulse3.Process(pulse3_decay);
    const float noise_sample = random_sample * pulse3_out;

    // digital_oscillator.cc:2440-2442.
    const float tone0 = SvfStep(excitation0, f0, damp0, svf0_lp, svf0_bp) +
        excitation0 * kSnareDryMix;
    const float tone1 = SvfStep(excitation1, f1, damp1, svf1_lp, svf1_bp) +
        excitation1 * kSnareDryMix;
    const float noise = SvfStep(noise_sample, f2, damp2, svf2_lp, svf2_bp);

    // MORPH crossfades the tonal sum against the snap resonator around
    // Braids' native unweighted sum (digital_oscillator.cc:2439-2442).
    const float tone_sum = tone0 * gain0 + tone1 * gain1;
    float sample = tone_sum * tone_mix_gain + noise * noise_mix_gain;
    CONSTRAIN(sample, -kSnareClip, kSnareClip);   // :2443

    if (stereo) {
      // Mid/side: AUX is a component already summed into OUT, the same
      // relationship kick_engine.cc's raw-excitation AUX has to its own
      // OUT -- see the header's stereo note.
      const float side = kSnareStereoSpread * SoftClip(noise);
      *out++ = sample + side;
      *aux++ = sample - side;
    } else {
      *out++ = sample;
      *aux++ = SoftClip(noise);
    }
  }

  pulse0_state_ = pulse0.state;
  pulse0_counter_ = pulse0.counter;
  pulse0_level_ = pulse0.level;
  pulse1_state_ = pulse1.state;
  pulse1_counter_ = pulse1.counter;
  pulse1_level_ = pulse1.level;
  pulse2_state_ = pulse2.state;
  pulse2_counter_ = pulse2.counter;
  pulse2_level_ = pulse2.level;
  pulse3_state_ = pulse3.state;
  pulse3_counter_ = pulse3.counter;
  pulse3_level_ = pulse3.level;
  pulse3_decay_ = pulse3_decay;
  tone0_resonance_domain_ = tone0_resonance_domain;
  tone1_resonance_domain_ = tone1_resonance_domain;

  svf0_lp_ = svf0_lp;
  svf0_bp_ = svf0_bp;
  svf1_lp_ = svf1_lp;
  svf1_bp_ = svf1_bp;
  svf2_lp_ = svf2_lp;
  svf2_bp_ = svf2_bp;
}

}  // namespace plaits
