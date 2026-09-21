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
  secondary_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
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
  //
  // ApplyMacro, not `timbre + (macro - 0.5) * 2` clamped into range. The
  // offset form wastes HALF the knob: with TIMBRE at noon it clamps below
  // MACRO 0.25 and above 0.75, and the live window slides with TIMBRE rather
  // than staying put. Measured at 10 of 20 steps live at every TIMBRE
  // setting. ApplyMacro anchors the midpoint on TIMBRE and interpolates to
  // each end of the shape range instead, so the whole travel is live wherever
  // TIMBRE sits, and noon is still exactly matched.
  const float secondary_control = ApplyMacro(
      parameters.timbre, 0.0f, 1.0f, parameters.macro);
#endif

  // Crossfade's trajectory, unchanged: below noon the detuned secondary fades
  // in against the primary, above it the primary crossfades into hard sync.
  // The two halves are mutually exclusive, which the render below exploits.
  float auxiliary_amount;
  float xmod_amount;
  if (remedy_ == VARIANT_REMEDY_NO_DRY) {
    // The primary never leaves, so these stop being "how much secondary" and
    // become "which secondary": the whole travel crossfades detuned to synced.
    auxiliary_amount = 0.5f;
    xmod_amount = trajectory;
  } else {
    auxiliary_amount = max(0.5f - trajectory, 0.0f) * 2.0f;
    if (remedy_ == VARIANT_REMEDY_LINEAR_FADE) {
      auxiliary_amount *= 0.5f;
    } else {
      auxiliary_amount *= auxiliary_amount * 0.5f;
    }
    xmod_amount = max(trajectory - 0.5f, 0.0f) * 2.0f;
  }
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

  // SHAPE_SPREAD mirrors the secondary's departure onto the primary, so TWIST
  // still does something where only the primary is sounding.
  // How far the PRIMARY follows TWIST, as a fraction of how far the secondary
  // moves. At 1.0 the two mirror exactly, which makes TWIST a full shape swap
  // rather than a spread -- the primary travels the whole range too, so TWIST
  // and TIMBRE end up fighting over the same sound. Lower values keep TWIST
  // audible where only the primary sounds while leaving TIMBRE in charge of it.
#ifndef PLAITS_VA_VARIANT_SPREAD_PRIMARY_RATIO
#define PLAITS_VA_VARIANT_SPREAD_PRIMARY_RATIO 1.0f
#endif
  const float primary_control = remedy_ == VARIANT_REMEDY_SHAPE_SPREAD
      ? ApplyMacro(parameters.timbre, 0.0f, 1.0f,
            0.5f + (0.5f - parameters.macro) *
                PLAITS_VA_VARIANT_SPREAD_PRIMARY_RATIO)
      : parameters.timbre;
  float primary_shape, primary_pw;
  ShapeAndPulseWidth(primary_control, &primary_shape, &primary_pw);
  float secondary_shape, secondary_pw;
  ShapeAndPulseWidth(secondary_control, &secondary_shape, &secondary_pw);

  const bool stereo =
      PLAITS_STEREO_VIRTUAL_ANALOG_VARIANT && parameters.stereo;

#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  // Every oscillator is a fixed ratio of the primary, so one signed offset
  // stream scales into four. Scaling rather than adding is what keeps the
  // intervals intact under modulation -- an offset added equally would
  // collapse them toward unison as it grew.
  // Filled only when there is modulation. RenderLinearFm falls back to the
  // plain Render() path only when BOTH of its pointers are null, so every one
  // of these must be passed as NULL when unfilled -- handing it an untouched
  // buffer makes it read uninitialised stack instead.
  float auxiliary_frequency_offset[kMaxBlockSize];
  float sync_frequency_offset[kMaxBlockSize];
  float aux_sync_frequency_offset[kMaxBlockSize];
  const float* frequency_offset = parameters.frequency_offset;
  if (frequency_offset) {
    const float inverse_primary =
        1.0f / (primary_f > 1.0e-9f ? primary_f : 1.0e-9f);
    const float auxiliary_ratio = auxiliary_f * inverse_primary;
    const float sync_ratio = sync_f * inverse_primary;
    const float aux_sync_ratio = aux_sync_f * inverse_primary;
    for (size_t i = 0; i < size; ++i) {
      const float offset = frequency_offset[i];
      auxiliary_frequency_offset[i] = offset * auxiliary_ratio;
      sync_frequency_offset[i] = offset * sync_ratio;
      aux_sync_frequency_offset[i] = offset * aux_sync_ratio;
    }
  }
  primary_.RenderLinearFm(
      primary_f, primary_pw, primary_shape, frequency_offset, out, size,
      PLAITS_HARD_SYNC_EVENTS(parameters));
#else
  primary_.Render(
      primary_f, primary_pw, primary_shape, out, size,
      PLAITS_HARD_SYNC_EVENTS(parameters));
#endif

  if (remedy_ == VARIANT_REMEDY_NO_DRY) {
    // Both secondaries every block -- the mutual exclusivity the other modes
    // rely on is exactly what this remedy removes. The synced voice uses
    // Dual's ratio rather than Crossfade's sweep, which is what makes the two
    // ends land on Dual's OUT and Dual's AUX.
    auxiliary_.Render(
        auxiliary_f, secondary_pw, secondary_shape, temp_buffer_, size,
        PLAITS_HARD_SYNC_EVENTS(parameters));
    aux_sync_.Render(
        primary_f, aux_sync_f, secondary_pw, secondary_shape,
        secondary_buffer_, size, PLAITS_HARD_SYNC_EVENTS(parameters));
    ParameterInterpolator blend(&xmod_amount_, trajectory, size);
    for (size_t i = 0; i < size; ++i) {
      const float primary = out[i];
      const float t = blend.Next();
      const float secondary =
          temp_buffer_[i] + (secondary_buffer_[i] - temp_buffer_[i]) * t;
      out[i] = (primary + secondary) * 0.5f;
      aux[i] = (secondary_buffer_[i] + primary) * 0.5f;
    }
    return;
  }

  // Only one secondary treatment can be audible at a time, so only one of
  // these two oscillators is ever worth rendering. That keeps the engine at
  // the same three-oscillator cost as either parent despite carrying four.
  ParameterInterpolator xmod_amount_modulation(
      &xmod_amount_,
      squashed_xmod_amount * (2.0f - squashed_xmod_amount),
      size);
  if (xmod_amount > 0.0f) {
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    sync_.RenderLinearFm<true, false>(
        primary_f, sync_f, secondary_pw, secondary_shape, 0.0f,
        frequency_offset,
        frequency_offset ? sync_frequency_offset : NULL, temp_buffer_, size,
        PLAITS_HARD_SYNC_EVENTS(parameters));
#else
    sync_.Render(
        primary_f, sync_f, secondary_pw, secondary_shape, temp_buffer_, size,
        PLAITS_HARD_SYNC_EVENTS(parameters));
#endif
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
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    auxiliary_.RenderLinearFm(
        auxiliary_f, secondary_pw, secondary_shape,
        frequency_offset ? auxiliary_frequency_offset : NULL,
        temp_buffer_, size, PLAITS_HARD_SYNC_EVENTS(parameters));
#else
    auxiliary_.Render(
        auxiliary_f, secondary_pw, secondary_shape, temp_buffer_, size,
        PLAITS_HARD_SYNC_EVENTS(parameters));
#endif
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
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  aux_sync_.RenderLinearFm<true, false>(
      primary_f, aux_sync_f, secondary_pw, secondary_shape, 0.0f,
      frequency_offset,
      frequency_offset ? aux_sync_frequency_offset : NULL, aux, size,
      PLAITS_HARD_SYNC_EVENTS(parameters));
#else
  aux_sync_.Render(
      primary_f, aux_sync_f, secondary_pw, secondary_shape, aux, size,
      PLAITS_HARD_SYNC_EVENTS(parameters));
#endif

  if (need_auxiliary) {
    for (size_t i = 0; i < size; ++i) {
      const float primary = out[i];
      out[i] = primary +
          (temp_buffer_[i] - primary) * auxiliary_amount_modulation.Next();
      aux[i] = (aux[i] + primary) * 0.5f;
    }
  } else {
    // Neither secondary was rendered this block, so temp_buffer_ holds nothing
    // this engine wrote -- uninitialised on the first block, another engine's
    // audio afterwards, since the allocator hands out a shared pool. It must
    // not be READ at all here. Multiplying it by a zero amount is NOT a safe
    // substitute: an Inf or NaN survives the multiply and poisons the output.
    // (This was a latent hazard found while chasing a nondeterministic audio
    // hash, not its cause -- that was the unfilled offset buffers above.)
    for (size_t i = 0; i < size; ++i) {
      auxiliary_amount_modulation.Next();
      aux[i] = (aux[i] + out[i]) * 0.5f;
    }
  }
}

}  // namespace plaits

#undef PLAITS_HARD_SYNC_EVENTS
