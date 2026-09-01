// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Paired inharmonic modal banks for anvil, agogo, plate and gong families.

#include "plaits/dsp/engine2/metalwork_engine.h"

#include <algorithm>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const float kMetalRatios[4][kMetalworkNumModes] = {
  // Anvil: a hard, upper-heavy spectrum without the paired-oscillator ratios
  // of an analog cowbell.
  { 1.000f, 1.711f, 2.948f, 4.372f, 5.934f, 7.613f },  // Anvil.
  { 1.000f, 1.410f, 2.760f, 3.180f, 4.120f, 5.430f },  // Agogo.
  { 1.000f, 1.630f, 2.140f, 2.910f, 3.870f, 5.120f },  // Plate.
  { 1.000f, 1.315f, 1.765f, 2.420f, 3.160f, 4.510f },  // Gong.
};

const float kMetalLevels[4][kMetalworkNumModes] = {
  { 0.62f, 1.00f, 0.76f, 0.58f, 0.42f, 0.30f },
  { 1.00f, 0.58f, 0.52f, 0.26f, 0.14f, 0.08f },
  { 0.72f, 0.80f, 0.62f, 0.45f, 0.28f, 0.18f },
  { 1.00f, 0.72f, 0.61f, 0.48f, 0.35f, 0.22f },
};

inline float Noise() {
  return 2.0f * Random::GetFloat() - 1.0f;
}

inline float DecayCoefficient(float seconds) {
  return 1.0f - 1.0f / max(2.0f, seconds * kSampleRate);
}

}  // namespace

void MetalworkEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void MetalworkEngine::Reset() {
  object_ = 0;
  strike_envelope_ = 0.0f;
  strike_lowpass_ = 0.0f;
  for (int i = 0; i < kMetalworkNumModes; ++i) {
    phase_a_[i] = 0.0f;
    phase_b_[i] = 0.0f;
    amplitude_[i] = 0.0f;
  }
}

void MetalworkEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  const bool stereo = parameters.stereo;
  const float hardness = parameters.timbre;
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    object_ = static_cast<int>(parameters.harmonics * 4.0f);
    CONSTRAIN(object_, 0, 3);
    const float accent = 0.30f + 0.70f * parameters.accent;
    for (int i = 0; i < kMetalworkNumModes; ++i) {
      const float height = static_cast<float>(i) /
          static_cast<float>(kMetalworkNumModes - 1);
      const float mallet = 1.0f - 0.78f * height;
      const float stick = 0.25f + 0.95f * height;
      amplitude_[i] = accent * kMetalLevels[object_][i] *
          (mallet + (stick - mallet) * hardness);
      phase_a_[i] = 0.0f;
      phase_b_[i] = 0.25f * Random::GetFloat();
    }
    strike_envelope_ = accent;
  }

  const float base = min(0.065f, NoteToFrequency(parameters.note));
  const float ring_time = 0.08f + 4.2f * parameters.morph *
      parameters.morph;
  const float beating = 0.00015f + 0.012f * parameters.macro *
      parameters.macro;
  const float strike_decay = DecayCoefficient(
      0.0005f + 0.005f * hardness);
  float frequency_a[kMetalworkNumModes];
  float frequency_b[kMetalworkNumModes];
  float mode_decay[kMetalworkNumModes];
  for (int i = 0; i < kMetalworkNumModes; ++i) {
    const float ratio = kMetalRatios[object_][i];
    frequency_a[i] = min(0.235f, base * ratio);
    frequency_b[i] = min(0.235f, base * ratio *
        (1.0f + beating * (1.0f + 0.3f * i)));
    const float high_loss = object_ == 3 ? 0.32f : 0.72f;
    mode_decay[i] = DecayCoefficient(
        ring_time / (1.0f + high_loss * i));
  }

  for (size_t i = 0; i < size; ++i) {
    float full = 0.0f;
    float upper = 0.0f;
    for (int j = 0; j < kMetalworkNumModes; ++j) {
      phase_a_[j] += frequency_a[j];
      phase_a_[j] -= static_cast<int>(phase_a_[j]);
      phase_b_[j] += frequency_b[j];
      phase_b_[j] -= static_cast<int>(phase_b_[j]);
      const float pair = 0.5f * amplitude_[j] *
          (SineNoWrap(phase_a_[j]) + SineNoWrap(phase_b_[j]));
      full += pair;
      if (j >= 2) {
        upper += (j & 1) ? -pair : pair;
      }
      amplitude_[j] *= mode_decay[j];
    }

    const float noise = Noise();
    strike_lowpass_ += 0.06f * (noise - strike_lowpass_);
    const float strike = (noise - strike_lowpass_) * strike_envelope_;
    strike_envelope_ *= strike_decay;
    if (stereo) {
      // Centre the low body and oppose the existing upper-mode pickup. This
      // provides width without adding another bank or per-mode multipliers.
      out[i] = 0.18f * full + 0.12f * upper + 0.10f * strike;
      aux[i] = 0.18f * full - 0.12f * upper + 0.10f * strike;
    } else {
      out[i] = 0.20f * full + 0.10f * strike;
      aux[i] = 0.29f * upper + 0.40f * strike;
    }
  }
}

}  // namespace plaits
