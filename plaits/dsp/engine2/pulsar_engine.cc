// Copyright 2026 Lyle Mills.
//
// Pulsar synthesis engine.

#include "plaits/dsp/engine2/pulsar_engine.h"

#include <algorithm>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

inline float FractionalPart(float value) {
  return value - static_cast<int>(value);
}

// A triangular pulse whose peak can move from an early pluck to a late swell.
// The final curve rounds the peak without calling an expensive transcendental.
inline float PulseWindow(float phase, float duty, float peak) {
  if (phase >= duty) {
    return 0.0f;
  }
  const float position = phase / duty;
  float envelope = position < peak
      ? position / peak
      : (1.0f - position) / (1.0f - peak);
  return envelope * (2.0f - envelope);
}

inline float ClusterWindow(
    float phase,
    float count,
    float duty,
    float peak,
    float offset) {
  const int lower_count = static_cast<int>(count);
  const float blend = count - static_cast<float>(lower_count);
  const float lower_phase = FractionalPart(
      phase * static_cast<float>(lower_count) + offset);
  const float upper_phase = FractionalPart(
      phase * static_cast<float>(lower_count + 1) + offset);
  const float lower = PulseWindow(lower_phase, duty, peak);
  const float upper = PulseWindow(upper_phase, duty, peak);
  return lower + (upper - lower) * blend;
}

}  // namespace

void PulsarEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void PulsarEngine::Reset() {
  phase_ = 0.0f;
  formant_ = 1.0f;
  duty_ = 0.5f;
  cluster_ = 1.0f;
  skew_ = 0.5f;
}

void PulsarEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
  }

  const float frequency = min(0.24f, NoteToFrequency(parameters.note));
  const float maximum_formant = max(1.0f, 0.225f / frequency);
  const float formant = min(
      maximum_formant,
      1.0f + 31.0f * parameters.harmonics * parameters.harmonics);
  const float duty = 0.055f + 0.89f * parameters.timbre * parameters.timbre;
  const float cluster = 1.0f + 3.0f * parameters.morph;
  const float skew = 0.12f + 0.76f * parameters.macro;

  ParameterInterpolator formant_modulation(&formant_, formant, size);
  ParameterInterpolator duty_modulation(&duty_, duty, size);
  ParameterInterpolator cluster_modulation(&cluster_, cluster, size);
  ParameterInterpolator skew_modulation(&skew_, skew, size);

  for (size_t i = 0; i < size; ++i) {
    const float current_formant = formant_modulation.Next();
    const float current_duty = duty_modulation.Next();
    const float current_cluster = cluster_modulation.Next();
    const float current_skew = skew_modulation.Next();

    phase_ += frequency;
    phase_ -= static_cast<int>(phase_);

    const float main_window = ClusterWindow(
        phase_, current_cluster, current_duty, current_skew, 0.0f);
    const float aux_window = ClusterWindow(
        phase_, current_cluster, current_duty, 1.0f - current_skew, 0.5f);

    const float carrier_phase = FractionalPart(phase_ * current_formant);
    const float aux_formant = min(maximum_formant, current_formant + 0.5f);
    const float aux_carrier_phase = FractionalPart(phase_ * aux_formant);
    out[i] = 0.78f * main_window * SineNoWrap(carrier_phase);
    aux[i] = 0.78f * aux_window * SineNoWrap(aux_carrier_phase);
  }
}

}  // namespace plaits
