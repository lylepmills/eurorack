// Copyright 2026 Lyle Mills.
//
// Scanned synthesis engine.

#include "plaits/dsp/engine2/scanned_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void ScannedEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void ScannedEngine::Reset() {
  fill(&position_[0], &position_[kScannedMasses], 0.0f);
  fill(&velocity_[0], &velocity_[kScannedMasses], 0.0f);
  scan_phase_ = 0.0f;
  physics_phase_ = 0.0f;
  reset_pending_ = true;
}

void ScannedEngine::Excite(float position, float width, float amount) {
  const float centre = position * static_cast<float>(kScannedMasses);
  float mean = 0.0f;
  for (int i = 0; i < kScannedMasses; ++i) {
    float distance = fabsf(static_cast<float>(i) - centre);
    distance = min(distance, static_cast<float>(kScannedMasses) - distance);
    float pulse = 0.0f;
    if (distance < width) {
      pulse = 0.5f + 0.5f * Sine(0.25f + 0.5f * distance / width);
    }
    velocity_[i] += pulse * amount;
    mean += pulse * amount;
  }
  mean /= static_cast<float>(kScannedMasses);
  for (int i = 0; i < kScannedMasses; ++i) {
    velocity_[i] -= mean;
  }
}

void ScannedEngine::Step(
    float inharmonicity,
    float damping,
    bool driven,
    int drive_index) {
  float acceleration[kScannedMasses];
  for (int i = 0; i < kScannedMasses; ++i) {
    const int l1 = (i + kScannedMasses - 1) % kScannedMasses;
    const int r1 = (i + 1) % kScannedMasses;
    const int l2 = (i + kScannedMasses - 2) % kScannedMasses;
    const int r2 = (i + 2) % kScannedMasses;
    const float laplacian = position_[l1] + position_[r1] - \
        2.0f * position_[i];
    const float biharmonic = position_[l2] - 4.0f * position_[l1] + \
        6.0f * position_[i] - 4.0f * position_[r1] + position_[r2];
    const float mass = 1.0f + inharmonicity * 0.6f * (i & 1);
    acceleration[i] = (0.22f * laplacian - \
        0.018f * inharmonicity * biharmonic) / mass;
  }
  if (driven) {
    acceleration[drive_index] += \
        (Random::GetFloat() - 0.5f) * 0.002f;
  }
  float acceleration_mean = 0.0f;
  for (int i = 0; i < kScannedMasses; ++i) {
    acceleration_mean += acceleration[i];
  }
  acceleration_mean /= static_cast<float>(kScannedMasses);

  float position_mean = 0.0f;
  float velocity_mean = 0.0f;
  for (int i = 0; i < kScannedMasses; ++i) {
    velocity_[i] = (velocity_[i] + acceleration[i] - acceleration_mean) * \
        damping;
    position_[i] += velocity_[i];
    position_mean += position_[i];
    velocity_mean += velocity_[i];
  }
  position_mean /= static_cast<float>(kScannedMasses);
  velocity_mean /= static_cast<float>(kScannedMasses);

  float peak = 0.0f;
  for (int i = 0; i < kScannedMasses; ++i) {
    position_[i] -= position_mean;
    velocity_[i] -= velocity_mean;
    peak = max(peak, fabsf(position_[i]));
  }
  if (peak > 1.0f) {
    const float gain = 1.0f / peak;
    for (int i = 0; i < kScannedMasses; ++i) {
      position_[i] *= gain;
      velocity_[i] *= gain;
    }
  }
}

float ScannedEngine::Scan(const float* data, float phase) const {
  const float index = phase * static_cast<float>(kScannedMasses);
  const int integral = static_cast<int>(index);
  const float fractional = index - static_cast<float>(integral);
  const int next = (integral + 1) % kScannedMasses;
  return data[integral] + (data[next] - data[integral]) * fractional;
}

void ScannedEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float strike_width = 1.25f + 5.0f * (1.0f - parameters.harmonics);
  if (reset_pending_ || (parameters.trigger & TRIGGER_RISING_EDGE)) {
    Excite(parameters.timbre, strike_width, 0.3f + 0.5f * parameters.accent);
    reset_pending_ = false;
  }

  const float frequency = min(0.24f, NoteToFrequency(parameters.note));
  const float physics_rate = 20.0f * SemitonesToRatio(parameters.macro * 72.0f) / \
      kCorrectedSampleRate;
  const float damping = 0.9997f - 0.018f * parameters.morph * \
      parameters.morph;
  const bool driven = parameters.trigger & TRIGGER_UNPATCHED;
  int drive_index = static_cast<int>(
      parameters.timbre * static_cast<float>(kScannedMasses));
  if (drive_index >= kScannedMasses) {
    drive_index = kScannedMasses - 1;
  }

  for (size_t i = 0; i < size; ++i) {
    physics_phase_ += physics_rate;
    while (physics_phase_ >= 1.0f) {
      physics_phase_ -= 1.0f;
      Step(parameters.harmonics, damping, driven, drive_index);
    }

    scan_phase_ += frequency;
    scan_phase_ -= static_cast<int>(scan_phase_);
    const float sample = Scan(position_, scan_phase_);
    float derivative_phase = scan_phase_ + \
        1.0f / static_cast<float>(kScannedMasses);
    if (derivative_phase >= 1.0f) {
      derivative_phase -= 1.0f;
    }
    const float derivative = Scan(position_, derivative_phase) - sample;
    out[i] = sample * 0.8f;
    aux[i] = derivative * 2.4f;
  }
}

}  // namespace plaits
