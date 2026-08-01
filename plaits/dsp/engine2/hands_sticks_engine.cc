// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Clustered impacts: handclap, finger snap, rimshot and struck wood.

#include "plaits/dsp/engine2/hands_sticks_engine.h"

#include <algorithm>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

inline float Noise() {
  return 2.0f * Random::GetFloat() - 1.0f;
}

inline float DecayCoefficient(float seconds) {
  return 1.0f - 1.0f / max(2.0f, seconds * kSampleRate);
}

}  // namespace

void HandsSticksEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void HandsSticksEngine::Reset() {
  instrument_ = 0;
  bursts_remaining_ = 0;
  burst_timer_ = 0;
  burst_spacing_ = 1;
  burst_envelope_ = 0.0f;
  tail_envelope_ = 0.0f;
  click_envelope_ = 0.0f;
  noise_lowpass_ = 0.0f;
  for (int i = 0; i < kHandsSticksNumModes; ++i) {
    phase_[i] = 0.0f;
    amplitude_[i] = 0.0f;
  }
}

void HandsSticksEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    instrument_ = static_cast<int>(parameters.harmonics * 4.0f);
    CONSTRAIN(instrument_, 0, 3);
    const float spread = parameters.macro * parameters.macro;
    const int extra = static_cast<int>(spread * 4.0f);
    bursts_remaining_ = instrument_ == 0 ? 3 + extra : 1 + extra / 2;
    burst_spacing_ = static_cast<int>(
        kSampleRate * (0.0025f + 0.014f * spread));
    burst_timer_ = 0;
    tail_envelope_ = 0.4f + 0.6f * parameters.accent;
    for (int i = 0; i < kHandsSticksNumModes; ++i) {
      phase_[i] = 0.0f;
      amplitude_[i] = 0.0f;
    }
  }

  const float base = min(0.11f, NoteToFrequency(parameters.note));
  const float tone = 0.65f + 1.7f * parameters.timbre;
  float ratios[kHandsSticksNumModes];
  float mode_level[kHandsSticksNumModes];
  switch (instrument_) {
    case 0:  // Handclap: broad body under the clustered noise.
      ratios[0] = 2.7f; ratios[1] = 4.1f; ratios[2] = 6.3f;
      mode_level[0] = 0.30f; mode_level[1] = 0.18f; mode_level[2] = 0.10f;
      break;
    case 1:  // Finger snap: one small cavity and a hard high crack.
      ratios[0] = 1.8f; ratios[1] = 5.6f; ratios[2] = 8.4f;
      mode_level[0] = 0.22f; mode_level[1] = 0.38f; mode_level[2] = 0.24f;
      break;
    case 2:  // Rimshot: a low shell mode against two stick modes.
      ratios[0] = 1.0f; ratios[1] = 3.35f; ratios[2] = 5.8f;
      mode_level[0] = 0.46f; mode_level[1] = 0.34f; mode_level[2] = 0.20f;
      break;
    default:  // Clave / woodblock.
      ratios[0] = 1.0f; ratios[1] = 2.74f; ratios[2] = 5.02f;
      mode_level[0] = 0.58f; mode_level[1] = 0.32f; mode_level[2] = 0.18f;
      break;
  }

  float frequency[kHandsSticksNumModes];
  float mode_decay[kHandsSticksNumModes];
  const float body_time = (instrument_ == 0 ? 0.035f : 0.018f) +
      (instrument_ == 3 ? 0.32f : 0.14f) * parameters.morph *
      parameters.morph;
  for (int i = 0; i < kHandsSticksNumModes; ++i) {
    frequency[i] = min(0.235f, base * ratios[i] * tone);
    mode_decay[i] = DecayCoefficient(body_time / (1.0f + 0.7f * i));
  }
  const float burst_decay = DecayCoefficient(
      instrument_ == 0 ? 0.0018f : 0.00065f);
  const float tail_decay = DecayCoefficient(
      0.018f + (instrument_ == 0 ? 0.42f : 0.10f) *
      parameters.morph * parameters.morph);
  const float click_decay = DecayCoefficient(0.00035f);

  for (size_t i = 0; i < size; ++i) {
    if (bursts_remaining_ > 0 && burst_timer_-- <= 0) {
      burst_envelope_ = 0.45f + 0.55f * parameters.accent;
      click_envelope_ = burst_envelope_;
      for (int j = 0; j < kHandsSticksNumModes; ++j) {
        amplitude_[j] += mode_level[j] * burst_envelope_;
      }
      --bursts_remaining_;
      burst_timer_ = burst_spacing_;
    }

    const float noise = Noise();
    const float cutoff = 0.025f + 0.19f * parameters.timbre;
    noise_lowpass_ += cutoff * (noise - noise_lowpass_);
    const float crack = noise - noise_lowpass_;
    float body = 0.0f;
    for (int j = 0; j < kHandsSticksNumModes; ++j) {
      phase_[j] += frequency[j];
      phase_[j] -= static_cast<int>(phase_[j]);
      body += amplitude_[j] * SineNoWrap(phase_[j]);
      amplitude_[j] *= mode_decay[j];
    }

    const float clustered = crack * burst_envelope_;
    const float tail = crack * tail_envelope_;
    const float click = noise * click_envelope_;
    burst_envelope_ *= burst_decay;
    tail_envelope_ *= tail_decay;
    click_envelope_ *= click_decay;

    const float noise_mix = instrument_ == 0 ? 0.42f :
        (instrument_ == 1 ? 0.30f : 0.10f);
    out[i] = 0.48f * body + noise_mix * (clustered + 0.34f * tail) +
        0.08f * click;
    aux[i] = 0.68f * clustered + 0.32f * click;
  }
}

}  // namespace plaits
