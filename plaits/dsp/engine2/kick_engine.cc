// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' KICK: two decaying excitation pulses into a self-modulating
// resonant bandpass, smoothed by an output one-pole.

#include "plaits/dsp/engine2/kick_engine.h"

#include <cmath>

#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace stmlib;

namespace {

// Braids' TIMBRE -> the SVF's resonance (digital_oscillator.cc:2334-2338),
// continuous in float rather than through Braids' int16 truncation of TIMBRE
// itself -- see kick_engine.h's TABLES note for why that substitution is
// safe here (a smooth quartic-ish falloff, not a near-a-cusp coefficient).
// Returns the SAME 0..~0.98 domain lookup_tables.py:102 calls `resonance`.
inline float KickResonanceDomain(float timbre) {
  const float decay = timbre * 32767.0f;
  const float scaled0 = 65535.0f - 2.0f * decay;
  const float squared = scaled0 * scaled0 * (1.0f / 65536.0f);
  const float scaled1 = squared * scaled0 * (1.0f / 262144.0f);
  const float resonance = 32640.0f - scaled1;  // 32768 - 128 - scaled, line 2338
  // Interpolate824(lut_svf_damp, resonance << 17) indexes at
  // (resonance << 17) >> 24 = resonance >> 7 (svf.h:81); the generator's own
  // `resonance` (lookup_tables.py:102) is that index / 260, so the domain
  // this port needs is (resonance / 128) / 260 = resonance / 33280.
  return resonance * (1.0f / 33280.0f);
}

// Braids' COLOR -> the output one-pole's feedback coefficient
// (digital_oscillator.cc:2340-2343,2357), same float-continuous treatment as
// KickResonanceDomain -- BUT this one-pole is a SECOND place RATE matters
// beyond the SVF (kick_engine.h RATE): `lp_state` is updated inside the SAME
// `for (j = 0; j < 2; ++j)` block the SVF is (digital_oscillator.cc:
// 2354-2361), so it too runs twice per iteration, at Braids' native 96 kHz,
// not once. A one-pole applied k once per PORT sample decays by (1-k) per
// sample; Braids applies k TWICE per iteration, decaying (1-k)^2 over the
// SAME iteration -- so transplanting k verbatim into a single 48 kHz-rate
// step (as this port does everywhere else, per RATE's re-derivation
// argument) makes the smoothing decay only half as fast per real second as
// Braids', which is inaudible near COLOR's default (k~0.1, cutoff far above
// the kick's own fundamental either way) but measures as -3.7 dB AC RMS at
// COLOR=0 (k~0.004, the port's smoothing becomes twice as SLOW as intended,
// i.e. noticeably duller): the port's single-step coefficient is solved from
// k' = 1 - (1-k)^2 instead, so (1-k') = (1-k)^2 matches Braids' net decay per
// iteration in one step rather than two.
inline float KickToneCoefficient(float color) {
  const float c = color * 32767.0f;
  const float c1 = c * c * (1.0f / 32768.0f);
  const float c2 = c1 * c1 * (1.0f / 32768.0f);
  const float lp_coefficient = 128.0f + 1.5f * c2;  // 128 + (c2>>1)*3, line 2343
  const float k = lp_coefficient * (1.0f / 32768.0f);
  const float one_minus_k = 1.0f - k;
  return 1.0f - one_minus_k * one_minus_k;
}

// braids/excitation.h, reproduced in float. Trigger does NOT reset `state`
// (excitation.h:56-59 has no such reset either) -- a strike arriving mid-
// decay adds to whatever is still ringing down, matching Braids exactly.
struct KickPulse {
  float state;
  int32_t counter;

  inline void Reset() {
    state = 0.0f;
    counter = 0;
  }

  inline void Trigger(int32_t delay_ticks) {
    counter = delay_ticks + 1;
  }

  // excitation.h:61-63 -- checked BEFORE Process() is called for this same
  // sample, i.e. against the state left over from the previous sample.
  inline bool DoneBefore() const { return counter == 0; }

  // excitation.h:65-74, decay/level already converted to this port's float
  // scale by the caller.
  inline float Process(float decay, float level) {
    state *= decay;
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

void KickEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void KickEngine::Reset() {
  pulse0_state_ = 0.0f;
  pulse0_counter_ = 0;
  pulse1_state_ = 0.0f;
  pulse1_counter_ = 0;
  click_counter_ = 0;
  svf_lp_ = 0.0f;
  svf_bp_ = 0.0f;
  lp_state_ = 0.0f;
  resonance_domain_ = KickResonanceDomain(0.5f);
  tone_coefficient_ = KickToneCoefficient(0.5f);
  punch_scale_ = 1.0f;
  balance_gain0_ = 1.0f;
  balance_gain1_ = 1.0f;
}

void KickEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    pulse0_counter_ = kKickPulse0DelayTicks + 1;
    pulse1_counter_ = kKickPulse1DelayTicks + 1;
    click_counter_ = kKickClickDelayTicks + 1;
  }

  const bool stereo = PLAITS_STEREO_KICK && parameters.stereo;

  const float target_resonance_domain = KickResonanceDomain(parameters.timbre);
  const float target_tone = KickToneCoefficient(parameters.harmonics);
  const float target_punch_scale = ApplyMacro(
      1.0f, 0.0f, kKickMaxPunchScale, parameters.macro);

  // MORPH: exciter balance -- 0.5 leaves both pulses at Braids' native
  // level (kick_engine.h THE FOURTH MACRO).
  float target_gain0 = 2.0f * (1.0f - parameters.morph);
  if (target_gain0 > 1.0f) target_gain0 = 1.0f;
  float target_gain1 = 2.0f * parameters.morph;
  if (target_gain1 > 1.0f) target_gain1 = 1.0f;

  ParameterInterpolator resonance_modulation(
      &resonance_domain_, target_resonance_domain, size);
  ParameterInterpolator tone_modulation(&tone_coefficient_, target_tone, size);
  ParameterInterpolator punch_modulation(
      &punch_scale_, target_punch_scale, size);
  ParameterInterpolator gain0_modulation(&balance_gain0_, target_gain0, size);
  ParameterInterpolator gain1_modulation(&balance_gain1_, target_gain1, size);

  KickPulse pulse0 = { pulse0_state_, pulse0_counter_ };
  KickPulse pulse1 = { pulse1_state_, pulse1_counter_ };

  while (size--) {
    // Pulse 2's countdown gates the resonator's centre frequency
    // (digital_oscillator.cc:2351-2352) -- decrement THEN check, matching
    // `pulse_[2].Process()` (which decrements) followed by `.done()`.
    if (click_counter_ > 0) {
      --click_counter_;
    }
    const bool click_active = click_counter_ > 0;

    const float note = parameters.note +
        (click_active ? kKickClickSemitones : 0.0f);
    float f_norm = NoteToFrequency(note);
    if (f_norm > kKickSvfMaxNormalizedFrequency) {
      f_norm = kKickSvfMaxNormalizedFrequency;
    }
    const float f = 2.0f * sinf(float(M_PI) * f_norm);

    const float resonance_domain = resonance_modulation.Next();
    const float damp_from_resonance =
        2.0f * (1.0f - Sqrt(Sqrt(resonance_domain)));
    const float damp_from_freq_ceiling = 2.0f / f - f * 0.5f;
    float damp = damp_from_resonance < damp_from_freq_ceiling
        ? damp_from_resonance : damp_from_freq_ceiling;
    if (damp > 2.0f) damp = 2.0f;

    const float punch_scale = punch_modulation.Next();
    float f_eff = f;
    float damp_eff = damp;
    if (punch_scale > 0.0f) {
      const float punch_signal = svf_lp_ > kKickPunchThreshold
          ? svf_lp_ : kKickPunchFloor;
      f_eff += punch_signal * kKickPunchStockCoefficient *
          kKickPunchFreqScale * punch_scale;
      damp_eff += (punch_signal - kKickPunchFloor) *
          kKickPunchDampScale * punch_scale;
    }

    const float gain0 = gain0_modulation.Next();
    const float gain1 = gain1_modulation.Next();

    const bool pulse1_done_before = pulse1.DoneBefore();
    const float pulse0_out = pulse0.Process(kKickPulse0Decay, kKickPulse0Level);
    const float pulse1_hold = pulse1_done_before ? 0.0f : kKickPulse1Hold;
    const float pulse1_out = pulse1.Process(kKickPulse1Decay, kKickPulse1Level);

    const float excitation =
        pulse0_out * gain0 + (pulse1_hold + pulse1_out) * gain1;

    // The Chamberlin recursion (svf.h:91-96), in float.
    const float notch = excitation - svf_bp_ * damp_eff;
    svf_lp_ += f_eff * svf_bp_;
    CONSTRAIN(svf_lp_, -kKickClip, kKickClip);
    const float hp = notch - svf_lp_;
    svf_bp_ += f_eff * hp;
    CONSTRAIN(svf_bp_, -kKickClip, kKickClip);

    const float resonator_output =
        excitation * kKickExcitationDryMix + svf_bp_;

    const float tone = tone_modulation.Next();
    lp_state_ += (resonator_output - lp_state_) * tone;
    CONSTRAIN(lp_state_, -kKickClip, kKickClip);

    const float sample = lp_state_;

    if (stereo) {
      const float side = kKickStereoSpread * SoftClip(excitation);
      *out++ = sample + side;
      *aux++ = sample - side;
    } else {
      *out++ = sample;
      *aux++ = SoftClip(excitation);
    }
  }

  pulse0_state_ = pulse0.state;
  pulse0_counter_ = pulse0.counter;
  pulse1_state_ = pulse1.state;
  pulse1_counter_ = pulse1.counter;
}

}  // namespace plaits
