// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' BOWD: a bowed-string waveguide with a stick-slip friction exciter.

#include "plaits/dsp/engine2/bowed_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids stores every waveguide sample as `value >> 8` into an int8. The
// shift FLOORS (R8); the saturation is this port's deviation, because a wrap
// inside a feedback loop can send the string off on its own.
inline int8_t QuantizeToInt8(float value) {
  const float scaled = value * 128.0f;
  int quantized = static_cast<int>(scaled);
  // static_cast truncates toward zero; step down to make it a floor.
  if (scaled < static_cast<float>(quantized)) {
    --quantized;
  }
  CONSTRAIN(quantized, -128, 127);
  return static_cast<int8_t>(quantized);
}

// Braids' fractional-delay read, reproduced exactly. digital_oscillator.cc:1247
// is `Mix(a, b, frac) << 8` against int8 line storage, and stmlib::Mix
// (stmlib/utils/dsp.h:86) is `(a * (65535 - balance) + b * balance) >> 16`
// returning an int16 -- so the interpolation between the two taps is FLOORED to
// whole int8 counts, 1/128 of full scale, before the shift. That truncation is
// a loss term inside the stick-slip feedback loop, and it is what lets the bow
// SLIP; interpolating in float here removes the slip regime entirely.
inline int32_t MixInt8(int32_t a, int32_t b, uint32_t balance) {
  const int32_t b_int = static_cast<int32_t>(balance);
  // Arithmetic >> on a negative value floors, which is what Braids relies on.
  return (a * (65535 - b_int) + b * b_int) >> 16;
}

// lut_bowing_friction, which holds min(1, 1/(d + 0.75)^4) on an axis of
// d = index/64: exactly 32768 at d = 0 and exactly 64 at d = 4. The table is
// that curve FLOORED to 1/32768ths (verified at all 257 entries), so the floor
// is reproduced rather than the table shipped.
inline float BowingFriction(float d) {
  const float g = d + 0.75f;
  const float g2 = g * g;
  float friction = 1.0f / (g2 * g2);
  if (friction > 1.0f) {
    friction = 1.0f;
  }
  const int32_t quantized = static_cast<int32_t>(friction * 32768.0f);
  return static_cast<float>(quantized) * (1.0f / 32768.0f);
}

// lut_bowing_envelope as three line segments: a 600-step linear rise to the
// peak, a 120-step decay, then a flat sustain that Braids reaches by clamping
// its own read pointer.
inline float BowingEnvelope(float index) {
  if (index >= kBowedEnvelopeHold) {
    return kBowedEnvelopeSustain;
  }
  if (index <= kBowedEnvelopeRise) {
    return kBowedEnvelopePeak * (index / kBowedEnvelopeRise);
  }
  const float t = (index - kBowedEnvelopeRise) / \
      (kBowedEnvelopeHold - kBowedEnvelopeRise);
  return kBowedEnvelopePeak + \
      (kBowedEnvelopeSustain - kBowedEnvelopePeak) * t;
}

}  // namespace

void BowedEngine::Init(BufferAllocator* allocator) {
  bridge_line_ = allocator->Allocate<int8_t>(kBowedBridgeLength);
  neck_line_ = allocator->Allocate<int8_t>(kBowedNeckLength);
  Reset();
}

void BowedEngine::Reset() {
  // Another engine's buffers alias these at the same addresses, so the lines
  // have to be cleared on every engine switch, not just at Init (R15).
  if (bridge_line_) {
    for (size_t i = 0; i < kBowedBridgeLength; ++i) {
      bridge_line_[i] = 0;
    }
  }
  if (neck_line_) {
    for (size_t i = 0; i < kBowedNeckLength; ++i) {
      neck_line_[i] = 0;
    }
  }
  delay_pointer_ = 0;
  excitation_ = 0.0f;
  bridge_lp_ = 0.0f;
  body_y0_ = 0.0f;
  body_y1_ = 0.0f;
  body_aux_y0_ = 0.0f;
  body_aux_y1_ = 0.0f;
  tilt_state_ = 0.0f;
  tilt_state_aux_ = 0.0f;
  exciter_dc_in_ = 0.0f;
  exciter_dc_out_ = 0.0f;
}

void BowedEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (!bridge_line_ || !neck_line_) {
    // Allocate<T> returns NULL silently when the arena is exhausted (R15).
    for (size_t i = 0; i < size; ++i) {
      out[i] = 0.0f;
      aux[i] = 0.0f;
    }
    return;
  }

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Reset();
  }

  // The loop runs once per OUTPUT sample here, exactly as Braids' does per
  // pair of its 96 kHz samples, so the delay is in 48 kHz samples and Braids'
  // `- 2` compensation for the one-pole transfers unchanged.
  const float frequency = max(1e-7f, NoteToFrequency(parameters.note));
  float delay = 1.0f / frequency - 2.0f;

  // Braids' `parameter_1 = 6 + (COLOR >> 9)` is an INTEGER in [6, 69] over
  // 256. The quantization is audible rather than cosmetic: bow position sets
  // a comb null at harmonic 256/parameter_1, so a 2% error there moves the
  // null a whole harmonic. Same reasoning as ring-mod's detune shift.
  const int bow_steps = 6 + (static_cast<int>(
      parameters.harmonics * 32767.0f) >> 9);
  const float bow_position = static_cast<float>(bow_steps) * (1.0f / 256.0f);
  float bridge_delay = delay * bow_position;

  // Braids' octave fold: halve until both taps fit their line. Keeping its
  // line lengths keeps its 11.4 Hz floor.
  int guard = 0;
  while (guard < 16 &&
         ((delay - bridge_delay) > static_cast<float>(kBowedNeckLength - 1) ||
          bridge_delay > static_cast<float>(kBowedBridgeLength - 1))) {
    delay *= 0.5f;
    bridge_delay *= 0.5f;
    ++guard;
  }
  // Read(d) is only meaningful for d >= 1: below that the integral part is
  // zero and the read wraps a whole line back, which is exactly what Braids
  // does from MIDI 84.5 upward at HARMONICS 0. Clamp the TAP at one sample,
  // and take the neck delay from the CLAMPED value so the total loop -- and
  // therefore the pitch -- is unchanged.
  CONSTRAIN(bridge_delay, 1.0f, static_cast<float>(kBowedBridgeLength - 4));
  float neck_delay = delay - bridge_delay;
  CONSTRAIN(neck_delay, 4.0f, static_cast<float>(kBowedNeckLength - 4));

  const uint32_t bridge_integral = static_cast<uint32_t>(bridge_delay);
  const uint32_t neck_integral = static_cast<uint32_t>(neck_delay);
  // Braids carries the delays as 16.16 and its interpolation balance is the
  // low word (`bridge_delay & 0xffff`), a uint16 -- so the balance is on the
  // same 1/65536 grid here.
  uint32_t bridge_balance = static_cast<uint32_t>(
      (bridge_delay - static_cast<float>(bridge_integral)) * 65536.0f);
  uint32_t neck_balance = static_cast<uint32_t>(
      (neck_delay - static_cast<float>(neck_integral)) * 65536.0f);
  if (bridge_balance > 65535) {
    bridge_balance = 65535;
  }
  if (neck_balance > 65535) {
    neck_balance = 65535;
  }

  // TIMBRE is Braids' `172 - (parameter_[0] >> 8)`, also an integer, in
  // [45, 172] and scaled by 1/32.
  const int pressure_steps = 172 - (static_cast<int>(
      parameters.timbre * 32767.0f) >> 8);
  const float pressure = static_cast<float>(pressure_steps) * (1.0f / 32.0f);

  // MORPH opens the nut reflection Braids welds to -1.0.
  const float nut_gain = 0.55f + 0.45f * parameters.morph;

  // MACRO moves the body pole +-1 octave. At the detent the offset is zero,
  // and the coefficients reduce to Braids' to within the 0.94% error in
  // kBowedStockTheta itself -- see the header.
  const float body_offset = ApplyMacro(0.0f, -12.0f, 12.0f, parameters.macro);
  const float theta = kBowedStockTheta * SemitonesToRatio(body_offset);
  // cos(theta) without libm: Sine(x + 0.25) is cos(2*pi*x), and theta is far
  // too small to leave SineNoWrap's safe range.
  const float body_a1 = 2.0f * kBowedBodyRadius * \
      SineNoWrap(theta * (1.0f / 6.2831853f) + 0.25f);
  const float body_a2 = -kBowedBodyRadius * kBowedBodyRadius;

  for (size_t i = 0; i < size; ++i) {
    const uint32_t bridge_read = \
        (delay_pointer_ + 2 * kBowedBridgeLength - bridge_integral);
    const uint32_t neck_read = \
        (delay_pointer_ + 2 * kBowedNeckLength - neck_integral);

    const int32_t bridge_a = \
        bridge_line_[bridge_read & (kBowedBridgeLength - 1)];
    const int32_t bridge_b = \
        bridge_line_[(bridge_read - 1) & (kBowedBridgeLength - 1)];
    const int32_t neck_a = neck_line_[neck_read & (kBowedNeckLength - 1)];
    const int32_t neck_b = neck_line_[(neck_read - 1) & (kBowedNeckLength - 1)];

    // Braids' `Mix(a, b, frac) << 8`: the tap read is quantised back to whole
    // int8 counts before it re-enters the loop. 1/128 of full scale here is
    // that `<< 8` against Braids' 32768-per-unit accumulator.
    const float bridge_value = static_cast<float>(
        MixInt8(bridge_a, bridge_b, bridge_balance)) * (1.0f / 128.0f);
    const float nut_value = static_cast<float>(
        MixInt8(neck_a, neck_b, neck_balance)) * (1.0f / 128.0f);

    bridge_lp_ = kBowedBridgeLpGain * bridge_value + \
        kBowedBridgeLpPole * bridge_lp_;
    const float bridge_reflection = -bridge_lp_;
    const float nut_reflection = -nut_gain * nut_value;
    const float string_velocity = bridge_reflection + nut_reflection;

    const float bow_velocity = BowingEnvelope(excitation_);
    const float velocity_delta = bow_velocity - string_velocity;

    float friction_argument = velocity_delta * pressure;
    if (friction_argument < 0.0f) {
      friction_argument = -friction_argument;
    }
    // Braids reads lut_bowing_friction at an INTEGER index and does not
    // interpolate (its Interpolate824 call is commented out), so the friction
    // curve is a 256-step staircase in d. Inside a stick-slip loop that
    // staircase is not a rounding detail -- it is where the slip happens.
    int friction_index = static_cast<int>(friction_argument * 64.0f);
    if (friction_index > 255) {
      friction_index = 255;
    }
    const float new_velocity = BowingFriction(
        static_cast<float>(friction_index) * (1.0f / 64.0f)) * velocity_delta;

    neck_line_[delay_pointer_ & (kBowedNeckLength - 1)] = \
        QuantizeToInt8(bridge_reflection + new_velocity);
    bridge_line_[delay_pointer_ & (kBowedBridgeLength - 1)] = \
        QuantizeToInt8(nut_reflection + new_velocity);
    ++delay_pointer_;

    // Body resonance: y[n] = g*x[n] + a1*y[n-1] + a2*y[n-2], read out through
    // the 1 - z^-2 numerator Braids uses.
    const float body = kBowedBodyGain * bridge_value + \
        body_a1 * body_y0_ + body_a2 * body_y1_;
    const float bridge_out = body - body_y1_;
    body_y1_ = body_y0_;
    body_y0_ = body;

    // Stands in for the baseband tilt of Braids' 2x interpolating upsampler.
    tilt_state_ += (1.0f - kBowedTilt) * (bridge_out - tilt_state_);

    // Make-up INSIDE the clip (R3): SoftClip is near-linear at the real
    // operating level, so `1.6f * SoftClip(x)` would emit +-1.6 unprotected.
    out[i] = SoftClip(1.6f * tilt_state_);

    if (PLAITS_STEREO_BOWED && parameters.stereo) {
      // L/R: the neck tap through its own copy of the body filter, so the two
      // channels are the same string heard at two pickup positions.
      const float body_aux = kBowedBodyGain * nut_value + \
          body_a1 * body_aux_y0_ + body_a2 * body_aux_y1_;
      const float neck_out = body_aux - body_aux_y1_;
      body_aux_y1_ = body_aux_y0_;
      body_aux_y0_ = body_aux;

      tilt_state_aux_ += (1.0f - kBowedTilt) * (neck_out - tilt_state_aux_);
      aux[i] = SoftClip(1.6f * tilt_state_aux_);
    } else {
      // Mono AUX: the bow itself. `new_velocity` is the stick-slip friction
      // output -- what the exciter injects into the string, before the string
      // or the body has coloured it. The in-tree idiom for a physical model
      // (inharmonic-string, modal-resonator and particle-noise all put their
      // raw exciter here), and a different thing in kind rather than the same
      // string at another position: dry, unpitched, and useful for exciting
      // something else.
      //
      // The bowing envelope is unipolar, so the friction output carries the
      // bow pressure as DC -- enough to park the LPG open and to fail the
      // SDK's audio-health gate. The blocker's corner is far below anything
      // the model produces, so the scrape itself is untouched.
      exciter_dc_out_ = new_velocity - exciter_dc_in_ + \
          kBowedExciterDcPole * exciter_dc_out_;
      exciter_dc_in_ = new_velocity;
      aux[i] = SoftClip(kBowedExciterGain * exciter_dc_out_);
    }

    // One envelope step per two output samples, Braids' `excitation_ptr >> 1`
    // at its 48 kHz iteration rate. 600 steps of rise is 25 ms.
    excitation_ += 0.5f;
    if (excitation_ > kBowedEnvelopeHold) {
      excitation_ = kBowedEnvelopeHold;
    }
  }
}

}  // namespace plaits
