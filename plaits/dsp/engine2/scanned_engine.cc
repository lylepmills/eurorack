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
    float structure,
    float damping,
    float nonlinearity,
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
    // A uniform circular network is translationally symmetric, so moving the
    // excitation point only changes phase. This fixed irregular mass profile
    // breaks that symmetry and gives TIMBRE a meaningful spectral effect.
    const float mass_profile = static_cast<float>((i * 13) & 31) / 31.0f;
    const float mass = 1.0f + inharmonicity * 0.45f * (i & 1) + \
        structure * (0.25f + 1.35f * mass_profile);
    acceleration[i] = (0.22f * laplacian - \
        0.018f * inharmonicity * biharmonic) / mass;
    acceleration[i] -= nonlinearity * 0.12f * position_[i] * \
        fabsf(position_[i]);
  }
  if (driven) {
    acceleration[drive_index] += \
        (Random::GetFloat() - 0.5f) * (0.002f + 0.004f * nonlinearity);
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
  const float structure = parameters.timbre * parameters.timbre;
  // HARMONICS originally derived much of its audible range from moving the
  // excitation between a broad strike and a narrow, bright impulse. Keep that
  // role separate from TIMBRE's mass profile and scan-path transformations.
  const float strike_width = 1.25f + 5.0f * (1.0f - parameters.harmonics);
  if (reset_pending_ || (parameters.trigger & TRIGGER_RISING_EDGE)) {
    Excite(parameters.timbre, strike_width, 0.3f + 0.5f * parameters.accent);
    reset_pending_ = false;
  }

  const float frequency = min(0.24f, NoteToFrequency(parameters.note));
  const float physics_rate = 20.0f * SemitonesToRatio(parameters.macro * 72.0f) / \
      kCorrectedSampleRate;
  const float damping = 0.99985f - 0.065f * parameters.morph * \
      parameters.morph;
  const float nonlinearity = parameters.morph * parameters.morph;
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
      Step(
          parameters.harmonics,
          structure,
          damping,
          nonlinearity,
          driven,
          drive_index);
    }

    scan_phase_ += frequency;
    scan_phase_ -= static_cast<int>(scan_phase_);
    // Phase distortion of the scan path makes TIMBRE audible even between
    // excitations, while remaining continuous at the waveform boundary.
    float warped_phase = scan_phase_ + 0.22f * structure * Sine(scan_phase_);
    if (warped_phase < 0.0f) {
      warped_phase += 1.0f;
    } else if (warped_phase >= 1.0f) {
      warped_phase -= 1.0f;
    }
    const float sample = Scan(position_, warped_phase);
    float left_phase = warped_phase - \
        1.0f / static_cast<float>(kScannedMasses);
    if (left_phase < 0.0f) {
      left_phase += 1.0f;
    }
    float derivative_phase = warped_phase + \
        1.0f / static_cast<float>(kScannedMasses);
    if (derivative_phase >= 1.0f) {
      derivative_phase -= 1.0f;
    }
    const float left = Scan(position_, left_phase);
    const float right = Scan(position_, derivative_phase);
    const float derivative = right - left;
    const float gradient = derivative * 2.25f;
    const float scanned = sample + (gradient - sample) * parameters.timbre;

    // MORPH now travels from the raw scan into a clearly audible nonlinear
    // regime. The squared mapping leaves the lower half useful for traditional
    // damping changes, then introduces up to 1.5 cycles of sine wavefolding.
    const float fold_amount = parameters.morph * parameters.morph;
    // Sine() requires a non-negative phase. The offset is safely larger than
    // the maximum negative excursion of the normalized scanned waveform.
    const float folded = Sine(
        16.0f + scanned * (0.25f + 1.25f * fold_amount));
    const float shaped = scanned + (folded - scanned) * fold_amount;
    out[i] = shaped * 0.8f;
    aux[i] = derivative * 2.0f;
  }
}

}  // namespace plaits
