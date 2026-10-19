// Copyright 2026 Rubato Audio.

#include "plaits/dsp/parameter_randomizer.h"

namespace plaits {

namespace {

const float kDeadZone = 0.07f;

// Near Drift reads Lorenz z and Far Drift reads Lorenz x, whose natural
// amplitudes differ: the rms of SoftBipolar(z - 25, 0.05) is 0.876 of
// SoftBipolar(x, 0.08)'s. Both then peak near 0.8 (Lorenz x stays within
// about +/-20, and SoftBipolar(20, 0.08) is 0.8), so dividing by that bound
// lets Drift reach as far as Step's held values do.
const float kNearDriftNorm = 0.876f;
const float kDriftPeak = 0.8f;

// Quantiles of the Near Drift source above (clamped to +/-1) at 0, 1/32 ...
// 1, measured over 64 seeded orbits and 6.3M samples. Mapping the source
// through them spreads it evenly over -1..1, as Step's random value already
// is, so Near Step and Near Drift share one distribution. The map is
// monotonic, so Drift keeps its motion.
const float kNearDriftQuantiles[33] = {
  -1.0000f, -0.9386f, -0.8955f, -0.8602f, -0.8277f, -0.7964f, -0.7652f,
  -0.7336f, -0.7007f, -0.6660f, -0.6289f, -0.5885f, -0.5441f, -0.4941f,
  -0.4371f, -0.3706f, -0.2911f, -0.1935f, -0.0695f, 0.0824f, 0.2183f,
  0.3345f, 0.4328f, 0.5159f, 0.5858f, 0.6452f, 0.6969f, 0.7432f, 0.7860f,
  0.8272f, 0.8696f, 0.9188f, 1.0000f,
};

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

// One range law for both sides. Depth (the squared distance from the dead
// zone) scales the reach, and the reach opens with depth toward whichever end
// the offset heads for, so at full depth either side can travel from the knob
// setting to either end. What tells the sides apart is how often a value goes
// far: Near cubes its evenly spread source, so 46% of values land within a
// tenth of the reach and 9% beyond three quarters, while Far spreads them
// across the whole reach.
float ParameterRandomizer::ApplyVoltage(
    float base,
    float amount,
    float voltage) {
  const float position = Position(amount);
  const float excursion = position * position;
  if (excursion <= 0.0f) {
    return base;
  }
  if (amount < 0.0f) {
    voltage = voltage * voltage * voltage;
  }
  const float symmetric_headroom = base < 1.0f - base ? base : 1.0f - base;
  const float directional_headroom = voltage < 0.0f ? base : 1.0f - base;
  const float headroom = symmetric_headroom +
      (directional_headroom - symmetric_headroom) * excursion;
  float value = base + voltage * headroom * excursion;
  CONSTRAIN(value, 0.0f, 1.0f);
  return value;
}

float ParameterRandomizer::NearEven(float source) {
  const float* q = kNearDriftQuantiles;
  if (source <= q[0]) {
    return -1.0f;
  }
  if (source >= q[32]) {
    return 1.0f;
  }
  int low = 0;
  int high = 32;
  while (high - low > 1) {
    const int middle = (low + high) >> 1;
    if (q[middle] <= source) {
      low = middle;
    } else {
      high = middle;
    }
  }
  const float fraction = (source - q[low]) / (q[low + 1] - q[low]);
  return -1.0f + 2.0f * (static_cast<float>(low) + fraction) / 32.0f;
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
  float voltage = amount < 0.0f
      ? SoftBipolar(orbit_[offset].z - 25.0f, 0.05f) / kNearDriftNorm
      : SoftBipolar(orbit_[offset + 1].x, 0.08f);
  voltage /= kDriftPeak;
  CONSTRAIN(voltage, -1.0f, 1.0f);
  return amount < 0.0f ? NearEven(voltage) : voltage;
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
        base[parameter], amount[parameter], voltage);
  }
}

}  // namespace plaits
