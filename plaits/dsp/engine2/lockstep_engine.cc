// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Internal phase-locked-loop synthesis engine.

#include "plaits/dsp/engine2/lockstep_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/units.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

struct LockRatio {
  int numerator;
  int denominator;
};

// The ratios deliberately mix subharmonics, simple consonances, and wide
// harmonic locks. Comparing divided phases makes every entry an actual PLL
// equilibrium rather than merely a pitch transposition.
const LockRatio kLockRatios[] = {
  { 1, 4 }, { 1, 3 }, { 1, 2 }, { 2, 3 }, { 3, 4 },
  { 1, 1 }, { 4, 3 }, { 3, 2 }, { 2, 1 }, { 5, 2 },
  { 3, 1 }, { 4, 1 }, { 5, 1 }, { 6, 1 }, { 8, 1 }
};

const int kNumLockRatios = sizeof(kLockRatios) / sizeof(kLockRatios[0]);

inline float Wrap(float phase) {
  phase -= static_cast<int>(phase);
  if (phase < 0.0f) {
    phase += 1.0f;
  }
  return phase;
}

inline float WrapBipolar(float phase) {
  phase = Wrap(phase + 0.5f);
  return phase - 0.5f;
}

inline float LimitAudio(float sample) {
  CONSTRAIN(sample, -1.0f, 1.0f);
  return sample;
}

}  // namespace

void LockstepEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void LockstepEngine::Reset() {
  reference_phase_ = 0.0f;
  follower_phase_ = 0.0f;
  follower_frequency_ = 0.0f;
  detector_lp_ = 0.0f;
  reset_pending_ = true;
}

void LockstepEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  int ratio_index = static_cast<int>(
      parameters.harmonics * static_cast<float>(kNumLockRatios));
  if (ratio_index >= kNumLockRatios) {
    ratio_index = kNumLockRatios - 1;
  }
  const LockRatio& lock_ratio = kLockRatios[ratio_index];
  const float numerator = static_cast<float>(lock_ratio.numerator);
  const float denominator = static_cast<float>(lock_ratio.denominator);
  const float ratio = numerator / denominator;

  // Preserve the selected ratio at the top of the keyboard rather than
  // letting only the follower hit Nyquist and slip continuously.
  const float reference_frequency = min(
      NoteToFrequency(parameters.note), 0.22f / max(1.0f, ratio));
  const float target_frequency = reference_frequency * ratio;

  if (reset_pending_) {
    follower_frequency_ = target_frequency * 0.82f;
    reset_pending_ = false;
  }
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // A trigger deliberately knocks the follower out of lock. The loop's
    // capture transient is part of the sound rather than a hidden reset.
    follower_phase_ = Wrap(
        follower_phase_ + 0.29f + 0.17f * parameters.accent);
    follower_frequency_ *= 0.70f + 0.16f * parameters.morph;
    detector_lp_ = 0.0f;
  }

  const float bandwidth = 0.00012f + 0.0075f * parameters.timbre * \
      parameters.timbre;
  const float damping = 0.35f + 1.45f * parameters.macro;
  const float proportional_gain = min(0.08f, 2.0f * damping * bandwidth);
  const float integral_gain = bandwidth * bandwidth;

  // MACRO simultaneously changes the range over which the loop is allowed to
  // hunt and its damping. Low values give long, fragile acquisition gestures;
  // high values pull decisively across almost two octaves.
  const float capture_octaves = 0.10f + 1.85f * parameters.macro;
  const float capture_min = target_frequency * SemitonesToRatio(
      -12.0f * capture_octaves);
  const float capture_max = target_frequency * SemitonesToRatio(
      12.0f * capture_octaves);
  const float detector_smoothing = 0.002f + \
      0.075f * parameters.timbre * parameters.timbre;

  for (size_t i = 0; i < size; ++i) {
    reference_phase_ = Wrap(reference_phase_ + reference_frequency);

    // A sinusoidal detector has a soft, ambiguous capture region; a wrapped
    // phase detector has a broad but buzzy one. MORPH traverses the two.
    const float reference_compare = Wrap(reference_phase_ * numerator);
    const float follower_compare = Wrap(follower_phase_ * denominator);
    const float phase_error = WrapBipolar(
        reference_compare - follower_compare);
    const float sine_detector = Sine(8.0f + phase_error);
    const float saw_detector = phase_error * 2.0f;
    const float detector = sine_detector + \
        (saw_detector - sine_detector) * parameters.morph;

    follower_frequency_ += integral_gain * detector;
    // A very weak centre pull prevents parameter jumps from leaving the
    // follower permanently on the wrong side of its new capture window.
    follower_frequency_ += bandwidth * 0.0015f * \
        (target_frequency - follower_frequency_);
    CONSTRAIN(follower_frequency_, capture_min, capture_max);

    follower_phase_ = Wrap(
        follower_phase_ + follower_frequency_ + \
        proportional_gain * detector);

    const float deadband = 0.012f + 0.055f * (1.0f - parameters.morph);
    const float charge = phase_error > deadband
        ? 1.0f
        : (phase_error < -deadband ? -1.0f : 0.0f);
    detector_lp_ += detector_smoothing * (charge - detector_lp_);

    const float carrier = SineNoWrap(follower_phase_);
    const float soft_square = carrier / \
        (0.22f + 0.78f * fabsf(carrier));
    const float error_shape = min(
        1.0f, 0.18f * parameters.morph + 4.0f * fabsf(phase_error));
    out[i] = (carrier + (soft_square - carrier) * error_shape) * 0.78f;

    // AUX exposes the charge-pump acquisition signal plus the audible
    // difference between reference and follower. It fades toward silence at
    // a clean 1:1 lock and remains active at harmonic/subharmonic ratios.
    const float reference = SineNoWrap(reference_phase_);
    aux[i] = LimitAudio(
        detector_lp_ * 0.58f + (reference - carrier) * 0.31f);
  }
}

}  // namespace plaits
