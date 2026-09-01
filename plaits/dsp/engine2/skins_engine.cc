// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Modal membrane percussion with a position-dependent strike.

#include "plaits/dsp/engine2/skins_engine.h"

#include <algorithm>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// The first axis begins with the measured modes of a circular membrane. The
// second pulls them toward a loaded, tensioned head: less regular than a
// harmonic oscillator, but more focused than the open membrane.
const float kOpenRatios[kSkinsNumModes] = {
  1.000f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.155f, 3.501f
};
const float kLoadedRatios[kSkinsNumModes] = {
  1.000f, 1.420f, 1.865f, 2.235f, 2.570f, 2.885f, 3.270f, 3.630f
};

inline float Noise() {
  return 2.0f * Random::GetFloat() - 1.0f;
}

inline float DecayCoefficient(float seconds) {
  const float samples = max(2.0f, seconds * kSampleRate);
  return 1.0f - 1.0f / samples;
}

}  // namespace

void SkinsEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void SkinsEngine::Reset() {
  for (int i = 0; i < kSkinsNumModes; ++i) {
    phase_[i] = 0.0f;
    amplitude_[i] = 0.0f;
  }
  bend_ratio_ = 1.0f;
  strike_envelope_ = 0.0f;
  strike_lowpass_ = 0.0f;
}

void SkinsEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  const bool stereo = parameters.stereo;
  const float accent = 0.35f + 0.65f * parameters.accent;
  const float position = parameters.timbre;
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    for (int i = 0; i < kSkinsNumModes; ++i) {
      const float mode = static_cast<float>(i) /
          static_cast<float>(kSkinsNumModes - 1);
      // A centre strike strongly favours the fundamental. Moving toward the
      // rim trades body for upper modes and alternates their initial polarity,
      // approximating the nodal sign changes seen by an edge pickup.
      const float centre_weight = 1.0f / (1.0f + 0.55f * i);
      const float edge_weight = 0.16f + 0.84f * mode;
      amplitude_[i] = accent *
          (centre_weight + (edge_weight - centre_weight) * position);
      phase_[i] = ((i & 1) && position > 0.55f) ? 0.5f : 0.0f;
    }
    const float pressure = parameters.macro * parameters.macro;
    bend_ratio_ = SemitonesToRatio(1.0f + 11.0f * pressure);
    strike_envelope_ = accent;
  }

  const float shape = parameters.harmonics;
  const float base_frequency = min(0.075f, NoteToFrequency(parameters.note));
  const float open_time = 0.09f + 1.75f * parameters.morph *
      parameters.morph;
  const float pressure = parameters.macro * parameters.macro;
  const float strike_decay = DecayCoefficient(0.0015f + 0.006f * position);

  float mode_frequency[kSkinsNumModes];
  float mode_decay[kSkinsNumModes];
  for (int i = 0; i < kSkinsNumModes; ++i) {
    const float ratio = kOpenRatios[i] +
        (kLoadedRatios[i] - kOpenRatios[i]) * shape;
    mode_frequency[i] = min(0.235f, base_frequency * ratio);
    const float mode = static_cast<float>(i) /
        static_cast<float>(kSkinsNumModes - 1);
    const float pressure_loss = 1.0f + pressure * (2.0f + 12.0f * mode);
    mode_decay[i] = DecayCoefficient(
        open_time / ((1.0f + 1.8f * mode) * pressure_loss));
  }

  for (size_t i = 0; i < size; ++i) {
    float body = 0.0f;
    float edge = 0.0f;
    for (int j = 0; j < kSkinsNumModes; ++j) {
      phase_[j] += min(0.245f, mode_frequency[j] * bend_ratio_);
      phase_[j] -= static_cast<int>(phase_[j]);
      const float mode_sample = amplitude_[j] * SineNoWrap(phase_[j]);
      body += mode_sample;
      if (j >= 2) {
        edge += (j & 1) ? -mode_sample : mode_sample;
      }
      amplitude_[j] *= mode_decay[j];
    }

    const float noise = Noise();
    strike_lowpass_ += 0.08f * (noise - strike_lowpass_);
    const float strike = (noise - strike_lowpass_) * strike_envelope_;
    strike_envelope_ *= strike_decay;
    bend_ratio_ += 0.0035f * (1.0f - bend_ratio_);

    if (stereo) {
      // Keep the fundamental/body and dry strike centred, then oppose the
      // already-computed upper-mode pickup for inexpensive acoustic width.
      out[i] = 0.17f * body + 0.10f * edge + 0.12f * strike;
      aux[i] = 0.17f * body - 0.10f * edge + 0.12f * strike;
    } else {
      out[i] = 0.18f * body + 0.12f * strike;
      aux[i] = 0.22f * edge + 0.32f * strike;
    }
  }
}

}  // namespace plaits
