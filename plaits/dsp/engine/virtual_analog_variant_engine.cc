// Copyright 2016 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// See virtual_analog_variant_engine.h for what this unifies and why.

#include "plaits/dsp/engine/virtual_analog_variant_engine.h"
#include "plaits/build_config.h"

#include <algorithm>

#include "stmlib/dsp/parameter_interpolator.h"

#if PLAITS_BUILD_ENABLE_SYNC_INPUT
#define PLAITS_HARD_SYNC_EVENTS(parameters) ((parameters).hard_sync)
#else
#define PLAITS_HARD_SYNC_EVENTS(parameters) 0u
#endif

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Verbatim from both parents, which take them from stock Virtual Analog:
// unison, fifth, octave, octave+fifth, two octaves, with the double smoothstep
// putting a plateau on each so HARMONICS locks onto them rather than sliding
// between them.
const float kIntervals[5] = {
  0.0f, 7.01f, 12.01f, 19.01f, 24.01f
};

inline float Squash(float x) {
  return x * x * (3.0f - 2.0f * x);
}

// Crossfade's width, kept: the two channels are nearby points on the engine's
// own crossfade, so the coefficients stay bounded and stereo cannot overshoot.
const float kStereoWidth = 0.16f;

// Braids-style shape/pulse-width pair from one 0..1 control, exactly as both
// parents derive them.
inline void ShapeAndPulseWidth(float control, float* shape, float* pw) {
  float s = control * 1.5f;
  CONSTRAIN(s, 0.0f, 1.0f);
  *shape = s;
  float p = 0.5f + (control - 0.66f) * 1.4f;
  CONSTRAIN(p, 0.5f, 0.99f);
  *pw = p;
}

}  // namespace

void VirtualAnalogVariantEngine::Init(BufferAllocator* allocator) {
  primary_.Init();
  auxiliary_.Init();
  // Crossfade offsets the secondary so the detuned pair does not start in
  // phase with the primary; keep it.
  auxiliary_.set_master_phase(0.25f);
  sync_.Init();
  aux_sync_.Init();

  auxiliary_amount_ = 0.0f;
  xmod_amount_ = 0.0f;
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
}

void VirtualAnalogVariantEngine::Reset() {
}

float VirtualAnalogVariantEngine::ComputeDetuning(float detune) const {
  detune = 2.05f * detune - 1.025f;
  CONSTRAIN(detune, -1.0f, 1.0f);

  float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 3.9999f;
  MAKE_INTEGRAL_FRACTIONAL(detune);

  const float a = kIntervals[detune_integral];
  const float b = kIntervals[detune_integral + 1];
  return (a + (b - a) * Squash(Squash(detune_fractional))) * sign;
}

void VirtualAnalogVariantEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  (void) already_enveloped;

  // The control map. See the header: the trajectory belongs on a panel knob,
  // not on TWIST, because macro sits at exactly 0.5 for every player who has
  // not given up FREQUENCY or a CV input to reach it.
#if PLAITS_VA_VARIANT_TRAJECTORY_ON_TWIST
  const float trajectory = parameters.macro;
  const float secondary_control = parameters.morph;
#else
  const float trajectory = parameters.morph;
  // TWIST spreads the secondary's shape away from the primary's. At exactly
  // noon the two match, which is Crossfade's shared-shape behaviour and makes
  // the unassigned default a complete engine rather than a slice of one.
  float secondary_control =
      parameters.timbre + (parameters.macro - 0.5f) * 2.0f;
  CONSTRAIN(secondary_control, 0.0f, 1.0f);
#endif

  // Crossfade's trajectory, unchanged: below noon the detuned secondary fades
  // in against the primary, above it the primary crossfades into hard sync.
  // The two halves are mutually exclusive, which the render below exploits.
  float auxiliary_amount = max(0.5f - trajectory, 0.0f) * 2.0f;
  auxiliary_amount *= auxiliary_amount * 0.5f;

  const float xmod_amount = max(trajectory - 0.5f, 0.0f) * 2.0f;
  const float squashed_xmod_amount = xmod_amount * (2.0f - xmod_amount);

  // HARMONICS alone sets the interval. Neither parent's MACRO scaling survives:
  // scaling a quantized interval is what made TWIST a second, worse detune
  // control, which is the whole reason this engine exists.
  const float auxiliary_detune = ComputeDetuning(parameters.harmonics);
  const float primary_f = NoteToFrequency(parameters.note);
  const float auxiliary_f = NoteToFrequency(
      parameters.note + auxiliary_detune);
  const float sync_f = primary_f * SemitonesToRatio(
      xmod_amount * (auxiliary_detune + 36.0f));
  // Dual's AUX ratio: a separate, continuous 48-semitone reading of HARMONICS,
  // independent of the trajectory so AUX stays a real second voice throughout.
  const float aux_sync_f = NoteToFrequency(
      parameters.note + parameters.harmonics * 48.0f);

  float primary_shape, primary_pw;
  ShapeAndPulseWidth(parameters.timbre, &primary_shape, &primary_pw);
  float secondary_shape, secondary_pw;
  ShapeAndPulseWidth(secondary_control, &secondary_shape, &secondary_pw);

  const bool stereo =
      PLAITS_STEREO_VIRTUAL_ANALOG_VARIANT && parameters.stereo;

  primary_.Render(
      primary_f, primary_pw, primary_shape, out, size,
      PLAITS_HARD_SYNC_EVENTS(parameters));

  // Only one secondary treatment can be audible at a time, so only one of
  // these two oscillators is ever worth rendering. That keeps the engine at
  // the same three-oscillator cost as either parent despite carrying four.
  ParameterInterpolator xmod_amount_modulation(
      &xmod_amount_,
      squashed_xmod_amount * (2.0f - squashed_xmod_amount),
      size);
  if (xmod_amount > 0.0f) {
    sync_.Render(
        primary_f, sync_f, secondary_pw, secondary_shape, temp_buffer_, size,
        PLAITS_HARD_SYNC_EVENTS(parameters));
    for (size_t i = 0; i < size; ++i) {
      out[i] += (temp_buffer_[i] - out[i]) * xmod_amount_modulation.Next();
    }
  } else {
    // Keep the interpolator's state marching even when the branch is skipped,
    // or re-entering sync would jump from a stale value.
    for (size_t i = 0; i < size; ++i) {
      xmod_amount_modulation.Next();
    }
  }

  ParameterInterpolator auxiliary_amount_modulation(
      &auxiliary_amount_, auxiliary_amount, size);
  const bool need_auxiliary = auxiliary_amount > 0.0f || auxiliary_amount_ > 0.0f;
  if (need_auxiliary || stereo) {
    auxiliary_.Render(
        auxiliary_f, secondary_pw, secondary_shape, temp_buffer_, size,
        PLAITS_HARD_SYNC_EVENTS(parameters));
  }

  if (stereo) {
    // Crossfade's rule, generalised: two nearby points either side of wherever
    // the trajectory sits. The mono sum is unchanged.
    for (size_t i = 0; i < size; ++i) {
      const float primary = out[i];
      const float secondary = temp_buffer_[i];
      const float amount = auxiliary_amount_modulation.Next();
      const float left_amount = max(amount - kStereoWidth, 0.0f);
      const float right_amount = min(amount + kStereoWidth, 1.0f);
      out[i] = primary + (secondary - primary) * left_amount;
      aux[i] = primary + (secondary - primary) * right_amount;
    }
    return;
  }

  // Mono AUX is Dual's: the primary against a hard-synced secondary, 50/50.
  aux_sync_.Render(
      primary_f, aux_sync_f, secondary_pw, secondary_shape, aux, size,
      PLAITS_HARD_SYNC_EVENTS(parameters));

  for (size_t i = 0; i < size; ++i) {
    const float primary = out[i];
    const float blended =
        primary + (temp_buffer_[i] - primary) * auxiliary_amount_modulation.Next();
    aux[i] = (aux[i] + primary) * 0.5f;
    out[i] = blended;
  }
}

}  // namespace plaits

#undef PLAITS_HARD_SYNC_EVENTS
