// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Mean-field coupled phase-oscillator synthesis engine.

#include "plaits/dsp/engine2/phase_flock_engine.h"
#include "plaits/build_config.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/units.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const float kFlockDetuning[kNumPhaseFlockOscillators] = {
  -1.0f, -0.63f, -0.31f, -0.07f, 0.19f, 0.51f, 0.86f
};

const float kFlockInitialPhase[kNumPhaseFlockOscillators] = {
  0.031f, 0.647f, 0.287f, 0.903f, 0.491f, 0.154f, 0.778f
};

const size_t kFlockCouplingInterval = 8;

// Fixed stereo positions, spread evenly in detune order so that the
// oscillator with near-zero detuning — the one tracking the played pitch
// most closely — sits exactly at the centre of the field.
const float kFlockPan[kNumPhaseFlockOscillators] = {
  0.10f, 0.2333f, 0.3667f, 0.50f, 0.6333f, 0.7667f, 0.90f
};

inline float Wrap(float phase) {
  phase -= static_cast<int>(phase);
  if (phase < 0.0f) {
    phase += 1.0f;
  }
  return phase;
}

inline void SineCosineNoWrap(float phase, float* sine, float* cosine) {
  const float index = phase * kSineLUTSize;
  const int32_t integral = static_cast<int32_t>(index);
  const float fractional = index - static_cast<float>(integral);
  const float sine_a = lut_sine[integral];
  const float sine_b = lut_sine[integral + 1];
  const int32_t cosine_integral = integral + kSineLUTQuadrature;
  const float cosine_a = lut_sine[cosine_integral];
  const float cosine_b = lut_sine[cosine_integral + 1];
  *sine = sine_a + (sine_b - sine_a) * fractional;
  *cosine = cosine_a + (cosine_b - cosine_a) * fractional;
}

inline float LimitAudio(float sample) {
  // Gentle rational saturation keeps newly synchronized flocks from jumping
  // in level while preserving the quiet cancellations of dispersed phases.
  sample /= 1.0f + 0.30f * fabsf(sample);
  CONSTRAIN(sample, -1.0f, 1.0f);
  return sample;
}

}  // namespace

void PhaseFlockEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void PhaseFlockEngine::Reset() {
  scatter_count_ = 0;
  reset_pending_ = true;
  for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
    sine_[i] = 0.0f;
    cosine_[i] = 1.0f;
  }
}

void PhaseFlockEngine::Scatter() {
  ++scatter_count_;
  const float turn = 0.083f * static_cast<float>(scatter_count_ & 7);
  for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
    const float phase = Wrap(
        kFlockInitialPhase[i] + turn * static_cast<float>(i + 1));
    SineCosineNoWrap(phase, &sine_[i], &cosine_[i]);
  }
}

void PhaseFlockEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (reset_pending_ || (parameters.trigger & TRIGGER_RISING_EDGE)) {
    Scatter();
    reset_pending_ = false;
  }

  const float base_frequency = min(0.20f, NoteToFrequency(parameters.note));
  const float spread = 14.0f * parameters.harmonics * \
      parameters.harmonics;
  float natural_frequency[kNumPhaseFlockOscillators];
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  float natural_ratio[kNumPhaseFlockOscillators];
#endif
  for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
    natural_frequency[i] = min(
        0.23f,
        base_frequency * SemitonesToRatio(spread * kFlockDetuning[i]));
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    natural_ratio[i] = natural_frequency[i] /
        (base_frequency > 1.0e-9f ? base_frequency : 1.0e-9f);
#endif
  }

  const float coupling = min(
      0.045f,
      base_frequency * 1.8f * parameters.timbre * parameters.timbre);
  const float cluster_mix = parameters.morph;
  // A phase lag turns perfect attraction into a travelling/frustrated flock.
  // Its exactly neutral midpoint matches the firmware's fourth-macro idiom.
  const float lag = (parameters.macro - 0.5f) * 0.90f;
  const float lag_normalization = 1.0f / (1.0f + fabsf(lag));
  const float normalization = 1.0f / \
      static_cast<float>(kNumPhaseFlockOscillators);

  const bool stereo = (PLAITS_STEREO_PHASE_FLOCK && parameters.stereo);
  float pan_left[kNumPhaseFlockOscillators];
  float pan_right[kNumPhaseFlockOscillators];
  if (stereo) {
    for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
      StereoPanGains(kFlockPan[i], &pan_left[i], &pan_right[i]);
    }
  }

  float rotation_sine[kNumPhaseFlockOscillators];
  float rotation_cosine[kNumPhaseFlockOscillators];
  for (size_t sample = 0; sample < size; ++sample) {
    float sine[kNumPhaseFlockOscillators];
    float cosine[kNumPhaseFlockOscillators];
    float sine_2[kNumPhaseFlockOscillators];
    float cosine_2[kNumPhaseFlockOscillators];
    float mean_sine = 0.0f;
    float mean_cosine = 0.0f;
    float mean_sine_2 = 0.0f;
    float mean_cosine_2 = 0.0f;
    float left_sum = 0.0f;
    float right_sum = 0.0f;

    if (stereo) {
      for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
        const float s = sine_[i];
        const float c = cosine_[i];
        sine[i] = s;
        cosine[i] = c;
        sine_2[i] = 2.0f * s * c;
        cosine_2[i] = c * c - s * s;
        mean_sine += s;
        mean_cosine += c;
        mean_sine_2 += sine_2[i];
        mean_cosine_2 += cosine_2[i];
        left_sum += pan_left[i] * s;
        right_sum += pan_right[i] * s;
      }
    } else {
      for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
        const float s = sine_[i];
        const float c = cosine_[i];
        sine[i] = s;
        cosine[i] = c;
        sine_2[i] = 2.0f * s * c;
        cosine_2[i] = c * c - s * s;
        mean_sine += s;
        mean_cosine += c;
        mean_sine_2 += sine_2[i];
        mean_cosine_2 += cosine_2[i];
      }
    }
    mean_sine *= normalization;
    mean_cosine *= normalization;
    mean_sine_2 *= normalization;
    mean_cosine_2 *= normalization;

    if (stereo) {
      // OUT/AUX become L/R: the same normalization and drive as the mono
      // mean, so per-channel loudness tracks the mono render. The second
      // circular moment still steers the coupling, but its output is skipped.
      out[sample] = LimitAudio(left_sum * normalization * 1.55f);
      aux[sample] = LimitAudio(right_sum * normalization * 1.55f);
    } else {
      out[sample] = LimitAudio(mean_sine * 1.55f);
      // Two-cluster states can cancel at OUT while remaining loud in the
      // second circular moment, making AUX a genuinely complementary octave
      // projection.
      aux[sample] = LimitAudio(mean_sine_2 * 1.35f);
    }

    const bool update_coupling = \
        (sample & (kFlockCouplingInterval - 1)) == 0;
    if (update_coupling) {
      for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
        // The first circular moment attracts all phases into one flock. The
        // second attracts two antipodal clusters at no extra trigonometric
        // lookup cost. Coupling is a control-rate process; refreshing it at
        // 6kHz leaves its motion smooth while the oscillators still advance
        // and produce output at the full audio rate.
        const float attraction_1 = mean_sine * cosine[i] - \
            mean_cosine * sine[i];
        const float quadrature_1 = mean_cosine * cosine[i] + \
            mean_sine * sine[i];
        const float attraction_2 = mean_sine_2 * cosine_2[i] - \
            mean_cosine_2 * sine_2[i];
        const float quadrature_2 = mean_cosine_2 * cosine_2[i] + \
            mean_sine_2 * sine_2[i];
        const float attraction = attraction_1 + \
            (0.5f * attraction_2 - attraction_1) * cluster_mix;
        const float quadrature = quadrature_1 + \
            (0.5f * quadrature_2 - quadrature_1) * cluster_mix;
        const float correction = (attraction - lag * quadrature) * \
            lag_normalization;
        float increment = natural_frequency[i] + coupling * correction;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
        if (parameters.frequency_offset) {
          increment += parameters.frequency_offset[sample] * natural_ratio[i];
          CONSTRAIN(increment, -0.24f, 0.24f);
        } else {
          CONSTRAIN(increment, 0.0f, 0.24f);
        }
#else
        CONSTRAIN(increment, 0.0f, 0.24f);
#endif
        const float rotation_phase = fabsf(increment);
        SineCosineNoWrap(
            rotation_phase, &rotation_sine[i], &rotation_cosine[i]);
        if (increment < 0.0f) {
          rotation_sine[i] = -rotation_sine[i];
        }
        const float rotation_norm = 1.5f - 0.5f * (
            rotation_sine[i] * rotation_sine[i] + \
            rotation_cosine[i] * rotation_cosine[i]);
        rotation_sine[i] *= rotation_norm;
        rotation_cosine[i] *= rotation_norm;
        const float s = sine_[i];
        const float c = cosine_[i];
        sine_[i] = s * rotation_cosine[i] + c * rotation_sine[i];
        cosine_[i] = c * rotation_cosine[i] - s * rotation_sine[i];
        if (sample == 0) {
          const float state_norm = 1.5f - 0.5f * (
              sine_[i] * sine_[i] + cosine_[i] * cosine_[i]);
          sine_[i] *= state_norm;
          cosine_[i] *= state_norm;
        }
      }
    } else {
      for (int i = 0; i < kNumPhaseFlockOscillators; ++i) {
        const float s = sine_[i];
        const float c = cosine_[i];
        sine_[i] = s * rotation_cosine[i] + c * rotation_sine[i];
        cosine_[i] = c * rotation_cosine[i] - s * rotation_sine[i];
      }
    }
  }
}

}  // namespace plaits
