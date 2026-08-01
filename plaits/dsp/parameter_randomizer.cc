// Copyright 2026 Rubato Audio.

#include "plaits/dsp/parameter_randomizer.h"

namespace plaits {

namespace {

const float kDeadZone = 0.07f;

inline float Absolute(float value) {
  return value < 0.0f ? -value : value;
}

inline float SoftBipolar(float value, float scale) {
  const float normalized = value * scale;
  return normalized / (0.4f + Absolute(normalized));
}

}  // namespace

void ParameterRandomizer::Init() {
  // Points already on the four attractors. Pre-settling them offline avoids
  // eighty thousand Euler steps during module startup.
  const ChaosOrbit initial[4] = {
    { 6.5553384f, 11.3170481f, 14.1720324f },
    { -11.7677193f, -3.4020793f, 38.6630669f },
    { 15.8954954f, 22.8469524f, 29.4823856f },
    { -3.0408432f, -4.2264490f, 17.2271671f },
  };
  for (int i = 0; i < 4; ++i) {
    orbit_[i] = initial[i];
  }
  random_state_ = 0x9e3779b9;
  Trigger();
}

void ParameterRandomizer::Trigger() {
  held_[0] = NextBipolar();
  held_[1] = NextBipolar();
}

void ParameterRandomizer::Advance(ChaosOrbit* orbit, float dt) {
  const float x = orbit->x;
  const float y = orbit->y;
  const float z = orbit->z;
  orbit->x = x + 10.0f * (y - x) * dt;
  orbit->y = y + (x * (28.0f - z) - y) * dt;
  orbit->z = z + (x * y - 2.6666667f * z) * dt;
}

float ParameterRandomizer::Position(float amount) {
  const float magnitude = Absolute(amount);
  return magnitude <= kDeadZone
      ? 0.0f
      : (magnitude - kDeadZone) / (1.0f - kDeadZone);
}

float ParameterRandomizer::ApplyVoltage(
    float base,
    float amount,
    float voltage,
    const ParameterRandomizerProfile& profile) {
  const float position = Position(amount);
  const float excursion = position * position;
  if (excursion <= 0.0f) {
    return base;
  }

  const bool near = amount < 0.0f;
  if (near) {
    voltage *= 0.55f;
  }
  const float span = near ? profile.near_span : profile.far_span;
  const float symmetric_headroom = base < 1.0f - base ? base : 1.0f - base;
  const float directional_headroom = voltage < 0.0f ? base : 1.0f - base;
  const float release = near
      ? 0.0f
      : profile.far_center_release * excursion;
  const float headroom = symmetric_headroom +
      (directional_headroom - symmetric_headroom) * release;
  float value = base + voltage * headroom * span * excursion;
  CONSTRAIN(value, 0.0f, 1.0f);
  return value;
}

float ParameterRandomizer::NextBipolar() {
  uint32_t value = random_state_;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  random_state_ = value;
  return (static_cast<int32_t>(value >> 8) - 8388608) *
      (1.0f / 8388608.0f);
}

float ParameterRandomizer::DriftVoltage(int parameter, float amount) const {
  const int offset = parameter * 2;
  return amount < 0.0f
      ? SoftBipolar(orbit_[offset].z - 25.0f, 0.05f) / 0.55f
      : SoftBipolar(orbit_[offset + 1].x, 0.08f);
}

void ParameterRandomizer::Process(
    AttenuverterMode mode,
    bool timbre_enabled,
    bool morph_enabled,
    float timbre_base,
    float morph_base,
    float timbre_amount,
    float morph_amount,
    const ParameterRandomizerProfile& timbre_profile,
    const ParameterRandomizerProfile& morph_profile,
    float* timbre,
    float* morph) {
  const bool enabled[2] = { timbre_enabled, morph_enabled };
  const float base[2] = { timbre_base, morph_base };
  const float amount[2] = { timbre_amount, morph_amount };
  const ParameterRandomizerProfile* profile[2] = {
    &timbre_profile, &morph_profile
  };
  float* destination[2] = { timbre, morph };

  for (int parameter = 0; parameter < 2; ++parameter) {
    if (!enabled[parameter]) {
      continue;
    }
    float voltage = held_[parameter];
    if (mode == ATTENUVERTER_MODE_DRIFT) {
      const float rate = 0.65f + 1.35f * Position(amount[parameter]);
      const int offset = parameter * 2;
      Advance(&orbit_[offset], profile[parameter]->near_rate * rate);
      Advance(&orbit_[offset + 1], profile[parameter]->far_rate * rate);
      voltage = DriftVoltage(parameter, amount[parameter]);
    }
    *destination[parameter] = ApplyVoltage(
        base[parameter], amount[parameter], voltage, *profile[parameter]);
  }
}

}  // namespace plaits
