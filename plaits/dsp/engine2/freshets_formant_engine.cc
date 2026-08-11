// Copyright 2026 Dylan Bolink.
// SPDX-License-Identifier: MIT

#include "plaits/dsp/engine2/freshets_formant_engine.h"

#include <algorithm>

#include "plaits/build_config.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"
#include "stmlib/dsp/units.h"

#include "plaits/resources.h"

#include "plaits/dsp/engine2/freshets_formant_shapes.h"

namespace plaits {

using namespace stmlib;

namespace {

// Tides applies this to the SHIFT pot in cv_reader.h, outside the engine, so it
// has to be reproduced here or the knob loses its most important position: a
// 0.02-wide dead zone at noon that puts the formant ratio on exactly 1.0, with
// both halves rescaled to give back the width the detent consumes.
inline float CenterDetent(float x) {
  if (x < 0.49f) {
    x *= 1.02040816f;
  } else if (x > 0.51f) {
    x -= 0.02f;
    x *= 1.02040816f;
  } else {
    x = 0.5f;
  }
  return x;
}

inline void RetriggerSerge(float* env_phase, float pw) {
  if (*env_phase >= pw) {
    float current_level = 0.0f;
    if (*env_phase < 1.0f) {
      current_level = 1.0f - (*env_phase - pw) / (1.0f - pw);
    }
    *env_phase = current_level * pw;
  }
}

const float kOutputScale = 5.0f / 10.0f;

// Just short of the endpoint, so index 128 is the last pair read and the curve
// never runs off its own end.
const float kMaxShapeIndex = kFormantShapeSize - 0.0001f;

// Stereo detune
const float kStereoDetune = 0.15f;

}  // namespace

void FreshetsFormantEngine::Init(stmlib::BufferAllocator* allocator) {
  dc_blocker_[0].Init();
  dc_blocker_[1].Init();
  Reset();
}

void FreshetsFormantEngine::Reset() {
  driver_phase_ = 0.0f;
  env_phase_ = 1.0f;  // Idle.
  env_phase_r_ = 1.0f;
  prev_f0_ = 0.001f;
  prev_ratio_ = 1.0f;
  prev_pw_ = 0.5f;
  prev_shape_ = 2.0f;
  next_sample_hollow_ = 0.0f;
  next_sample_sub_ = 0.0f;
  sub_state_ = false;
  prev_hollow_state_ = 0;
  lp_1_ = 0.0f;
  lp_2_ = 0.0f;
  lp_1_r_ = 0.0f;
  lp_2_r_ = 0.0f;
}

void FreshetsFormantEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  const bool stereo = PLAITS_STEREO_FRESHETS_FORMANT && parameters.stereo;

  // HARMONICS -> formant ratio, +/-48 semitones around a detented unison.
  const float target_ratio = SemitonesToRatio(
      (CenterDetent(parameters.harmonics) - 0.5f) * 96.0f);

  // TIMBRE -> smoothness. Below noon it closes a lowpass on MAIN and attenuates
  // AUX; above noon it opens a wavefolder on MAIN and mixes a sub-octave square
  // into AUX.
  const float smoothness = parameters.timbre;
  const float fold = std::max(2.0f * (smoothness - 0.5f), 0.0f);

  // MORPH -> the attack/decay balance of the formant envelope.
  float pw = parameters.morph;
  CONSTRAIN(pw, 0.05f, 0.95f);

  // MACRO -> waveshape, sweeping Tides' five audio curves in Tides' order
  const float shape = ApplyMacro(2.0f, 0.0f, 3.9999f, parameters.macro);

  float f0 = NoteToFrequency(parameters.note);
  CONSTRAIN(f0, 0.0f, 0.25f);

  // Tides increments the driver and THEN zeroes it on the gate, so the synced
  // sample sits at phase 0. 
  const bool sync_driver = parameters.trigger & TRIGGER_RISING_EDGE;
  if (sync_driver) {
    sub_state_ = false;
    RetriggerSerge(&env_phase_, pw);
    RetriggerSerge(&env_phase_r_, pw);
  }

  ParameterInterpolator f0_mod(&prev_f0_, f0, size);
  ParameterInterpolator ratio_mod(&prev_ratio_, target_ratio, size);
  ParameterInterpolator pw_mod(&prev_pw_, pw, size);
  ParameterInterpolator shape_mod(&prev_shape_, shape, size);

  float lp_ratio = std::min(smoothness * 2.0f, 1.0f);
  lp_ratio *= lp_ratio * lp_ratio;
  float base_lp_coefficient = f0 * 0.5f;
  base_lp_coefficient += (1.0f - base_lp_coefficient) * lp_ratio;

  for (size_t i = 0; i < size; ++i) {
    float f0 = f0_mod.Next();
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      f0 += parameters.frequency_offset[i];
      CONSTRAIN(f0, 0.0f, 0.25f);
    }
#endif
    const float ratio = ratio_mod.Next();
    const float pw = pw_mod.Next();
    const float shape = shape_mod.Next();
    const float formant_freq = f0 * ratio;
    float lp_coefficient = base_lp_coefficient;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      lp_coefficient = f0 * 0.5f;
      lp_coefficient += (1.0f - lp_coefficient) * lp_ratio;
    }
#endif

    // Driver oscillator. Its wrap is the only thing that fires the envelope.
    const bool prev_sub = sub_state_;
    driver_phase_ += f0;
    if (i == 0 && sync_driver) {
      driver_phase_ = 0.0f;   // after the increment, as Tides does it
    }
    if (driver_phase_ >= 1.0f) {
      driver_phase_ -= 1.0f;
      sub_state_ = !sub_state_;
      RetriggerSerge(&env_phase_, pw);
      RetriggerSerge(&env_phase_r_, pw);
    }

    env_phase_ += formant_freq;
    if (env_phase_ > 1.0f) {
      env_phase_ = 1.0f;  // Clamp rather than wrap: this is an AD, not a cycle.
    }

    // Advanced unconditionally: one add and one compare is cheaper than a branch
    // in the unrolled hot loop, and in mono nothing reads it.
    env_phase_r_ += formant_freq * (1.0f + kStereoDetune);
    if (env_phase_r_ > 1.0f) {
      env_phase_r_ = 1.0f;
    }

    float raw_level;
    if (env_phase_ < pw) {
      raw_level = env_phase_ / pw;
    } else if (env_phase_ < 1.0f) {
      raw_level = 1.0f - (env_phase_ - pw) / (1.0f - pw);
    } else {
      raw_level = 0.0f;
    }

    // Read the envelope through two adjacent transfer curves and crossfade.
    MAKE_INTEGRAL_FRACTIONAL(shape);
    const int16_t* shape_1 = kFormantShape[shape_integral];
    const int16_t* shape_2 = kFormantShape[shape_integral + 1];

    float ws_index = kFormantShapeSize * raw_level;
    if (ws_index > kMaxShapeIndex) {
      ws_index = kMaxShapeIndex;
    }
    MAKE_INTEGRAL_FRACTIONAL(ws_index);

    const float x0 = static_cast<float>(shape_1[ws_index_integral]) / 32768.0f;
    const float x1 =
        static_cast<float>(shape_1[ws_index_integral + 1]) / 32768.0f;
    const float y0 = static_cast<float>(shape_2[ws_index_integral]) / 32768.0f;
    const float y1 =
        static_cast<float>(shape_2[ws_index_integral + 1]) / 32768.0f;
    const float x = x0 + (x1 - x0) * ws_index_fractional;
    const float y = y0 + (y1 - y0) * ws_index_fractional;
    const float shaped = x + (y - x) * shape_fractional;

    // MAIN: the formant waveform. The fold and the lowpass live on opposite
    // halves of TIMBRE and are never both active, so their order is moot.
    float bipolar = 2.0f * shaped - 1.0f;
    if (fold > 0.0f) {
      float index = 0.5f + bipolar * (0.03f + 0.46f * fold);
      CONSTRAIN(index, 0.0f, 1.0f);
      const float folded = InterpolateHermite(lut_fold + 1, index, 512.0f);
      bipolar = bipolar + (folded - bipolar) * fold;
    }
    ONE_POLE(lp_1_, bipolar, lp_coefficient);
    ONE_POLE(lp_2_, lp_1_, lp_coefficient);

    float formant = lp_2_ * kOutputScale;
    CONSTRAIN(formant, -1.0f, 1.0f);

    // AUX: the hollow output.
    float hollow = 0.0f;
    if (!stereo) {
      int hollow_state;
      if (env_phase_ >= 1.0f || raw_level < 0.001f) {
        hollow_state = 0;
      } else if (env_phase_ < pw) {
        hollow_state = 1;
      } else {
        hollow_state = -1;
      }

      float this_sample = next_sample_hollow_;
      float next_sample = 0.0f;
      if (hollow_state != prev_hollow_state_) {
        const float discontinuity =
            static_cast<float>(hollow_state - prev_hollow_state_);
        this_sample += ThisBlepSample(0.0f) * discontinuity * 0.5f;
        next_sample += NextBlepSample(0.0f) * discontinuity * 0.5f;
      }
      next_sample += static_cast<float>(hollow_state);
      next_sample_hollow_ = next_sample;
      prev_hollow_state_ = hollow_state;

      // The sub-octave square runs whatever TIMBRE is doing, so its state stays
      // coherent whether or not it is currently being mixed in.
      float sub_sample = next_sample_sub_;
      float sub_next = 0.0f;
      if (sub_state_ != prev_sub) {
        const float discontinuity = sub_state_ ? 1.0f : -1.0f;
        sub_sample += ThisBlepSample(0.0f) * discontinuity;
        sub_next += NextBlepSample(0.0f) * discontinuity;
      }
      sub_next += sub_state_ ? 1.0f : 0.0f;
      next_sample_sub_ = sub_next;

      hollow = this_sample;
      if (smoothness < 0.5f) {
        hollow *= smoothness * 2.0f;
      } else if (smoothness > 0.5f) {
        const float sub_bipolar = 2.0f * sub_sample - 1.0f;
        hollow += sub_bipolar * (smoothness - 0.5f) * 2.0f;
      }
      hollow *= kOutputScale;
      CONSTRAIN(hollow, -1.0f, 1.0f);
    }

    if (stereo) {
      float raw_level_r;
      if (env_phase_r_ < pw) {
        raw_level_r = env_phase_r_ / pw;
      } else if (env_phase_r_ < 1.0f) {
        raw_level_r = 1.0f - (env_phase_r_ - pw) / (1.0f - pw);
      } else {
        raw_level_r = 0.0f;
      }

      // Same curve pair and same crossfade position as the left channel: MACRO
      // selects one transfer curve for the voice, not one per side.
      float ws_r = kFormantShapeSize * raw_level_r;
      if (ws_r > kMaxShapeIndex) {
        ws_r = kMaxShapeIndex;
      }
      MAKE_INTEGRAL_FRACTIONAL(ws_r);
      const float rx0 = static_cast<float>(shape_1[ws_r_integral]) / 32768.0f;
      const float rx1 =
          static_cast<float>(shape_1[ws_r_integral + 1]) / 32768.0f;
      const float ry0 = static_cast<float>(shape_2[ws_r_integral]) / 32768.0f;
      const float ry1 =
          static_cast<float>(shape_2[ws_r_integral + 1]) / 32768.0f;
      const float rx = rx0 + (rx1 - rx0) * ws_r_fractional;
      const float ry = ry0 + (ry1 - ry0) * ws_r_fractional;
      const float shaped_r = rx + (ry - rx) * shape_fractional;

      float bipolar_r = 2.0f * shaped_r - 1.0f;
      if (fold > 0.0f) {
        float index_r = 0.5f + bipolar_r * (0.03f + 0.46f * fold);
        CONSTRAIN(index_r, 0.0f, 1.0f);
        const float folded_r = InterpolateHermite(lut_fold + 1, index_r, 512.0f);
        bipolar_r = bipolar_r + (folded_r - bipolar_r) * fold;
      }
      ONE_POLE(lp_1_r_, bipolar_r, lp_coefficient);
      ONE_POLE(lp_2_r_, lp_1_r_, lp_coefficient);
      float formant_r = lp_2_r_ * kOutputScale;
      CONSTRAIN(formant_r, -1.0f, 1.0f);

      out[i] = formant;
      aux[i] = formant_r;
    } else {
      out[i] = formant;
      aux[i] = hollow;
    }
  }

  dc_blocker_[0].set_f<FREQUENCY_DIRTY>(0.3f * f0);
  dc_blocker_[0].Process<FILTER_MODE_HIGH_PASS>(out, size);
  dc_blocker_[1].set_f<FREQUENCY_DIRTY>((stereo ? 0.3f : 0.15f) * f0);
  dc_blocker_[1].Process<FILTER_MODE_HIGH_PASS>(aux, size);
}

}  // namespace plaits
