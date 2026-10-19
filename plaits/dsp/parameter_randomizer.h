// Copyright 2026 Rubato Audio.
//
// Slow control-voltage processes for unpatched TIMBRE and MORPH inputs.

#ifndef PLAITS_DSP_PARAMETER_RANDOMIZER_H_
#define PLAITS_DSP_PARAMETER_RANDOMIZER_H_

#include "stmlib/stmlib.h"

namespace plaits {

enum AttenuverterMode {
  ATTENUVERTER_MODE_STOCK = 0,
  ATTENUVERTER_MODE_DRIFT,
  ATTENUVERTER_MODE_STEP,
  ATTENUVERTER_MODE_LAST
};

// Per-model Drift speeds. The range law is the same for every model (see
// ApplyVoltage); only how fast each side's orbit moves is tuned per model.
struct ParameterRandomizerProfile {
  float near_rate;
  float far_rate;
};

struct EngineRandomizerProfile {
  uint8_t timbre;
  uint8_t morph;
};

class ParameterRandomizer {
 public:
  ParameterRandomizer() { }
  ~ParameterRandomizer() { }

  void Init();
  void Trigger();

  void Process(
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
      float* morph);

 private:
  struct ChaosOrbit {
    float x;
    float y;
    float z;
  };

  static void Advance(ChaosOrbit* orbit, float dt);
  static float Position(float amount);
  static float ApplyVoltage(float base, float amount, float voltage);
  static float NearEven(float source);
  float NextBipolar();
  float DriftVoltage(int parameter, float amount) const;

  ChaosOrbit orbit_[4];
  float held_[2];
  uint32_t random_state_;

  DISALLOW_COPY_AND_ASSIGN(ParameterRandomizer);
};

}  // namespace plaits

#endif  // PLAITS_DSP_PARAMETER_RANDOMIZER_H_
