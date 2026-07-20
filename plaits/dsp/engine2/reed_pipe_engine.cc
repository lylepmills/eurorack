// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Lightweight nonlinear reed and fractional-delay bore engine.

#include "plaits/dsp/engine2/reed_pipe_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void ReedPipeEngine::Init(BufferAllocator* allocator) {
  // Half of the 16 KiB shared engine arena is enough for a bore down to about
  // 12 Hz while leaving comfortable headroom for allocator alignment changes.
  bore_.Init(allocator->Allocate<float>(kReedPipeDelaySize));
  Reset();
}

void ReedPipeEngine::Reset() {
  bore_.Reset();
  delay_ = 100.0f;
  return_filter_ = 0.0f;
  outgoing_ = 0.0f;
  breath_envelope_ = 0.0f;
  excitation_ = 0.0f;
  reflection_coefficient_ = 0.934f;
  reflection_brightness_ = 0.245f;
  reed_stiffness_ = 1.2f;
  breath_noise_ = 0.008f;
  pickup_position_ = 0.48f;
  pickup_amount_ = 0.15f;
  out_dc_input_ = 0.0f;
  out_dc_output_ = 0.0f;
  aux_dc_input_ = 0.0f;
  aux_dc_output_ = 0.0f;
  noise_state_ = 0x6d2b79f5;
}

void ReedPipeEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // A deterministic pressure tongue makes articulation repeatable and also
    // kicks very soft reed settings away from their static equilibrium.
    excitation_ = 0.22f + 0.28f * parameters.accent;
  }

  const float frequency = min(
      0.24f, max(0.000001f, NoteToFrequency(parameters.note)));
  const float target_reflection_coefficient = \
      0.88f + 0.108f * parameters.macro;
  // The reflection filter's corner is expressed in harmonics of the note so
  // the bore loses the same *relative* part of its spectrum at every pitch.
  // With a fixed coefficient the corner sat ~19 harmonics up at the bottom of
  // the keyboard, leaving the filter nearly transparent there -- which is why
  // MACRO, MORPH and TIMBRE all collapsed on low notes.
  const float brightness_harmonic = kReedPipeBrightnessMin + \
      kReedPipeBrightnessRange * parameters.macro * parameters.macro;
  float target_reflection_brightness = \
      6.2832f * brightness_harmonic * frequency;
  CONSTRAIN(target_reflection_brightness, 0.02f, 0.90f);
  // A negative bell reflection needs a half-period round trip. Compensating
  // the one-pole reflection filter keeps the perceived fundamental close to
  // the pitch anchor over the macro range.
  const float filter_delay = 0.88f * \
      (1.0f - target_reflection_brightness) / \
      target_reflection_brightness;
  float target_delay = 0.5f / frequency - filter_delay;
  CONSTRAIN(target_delay, 4.0f, kReedPipeDelaySize - 4.0f);
  ParameterInterpolator delay_modulation(&delay_, target_delay, size);

  const bool blowing = parameters.trigger & \
      (TRIGGER_HIGH | TRIGGER_UNPATCHED);
  const float breath_pressure = 0.52f + \
      0.46f * parameters.timbre * parameters.timbre;
  const float target_breath = blowing ? breath_pressure : 0.0f;
  const float target_reed_stiffness = kReedPipeStiffnessMin + \
      kReedPipeStiffnessRange * parameters.morph * parameters.morph;
  const float target_breath_noise = 0.003f + \
      0.010f * (1.0f - parameters.morph);

  // HARMONICS moves a pickup along the bore and increases its contribution at
  // the ends of the control. Keeping this tap out of the feedback path changes
  // the even/odd balance without pulling the pitch anchor.
  const float target_pickup_position = \
      0.14f + 0.68f * parameters.harmonics;
  const float target_pickup_amount = 0.15f + 0.35f * \
      fabsf(2.0f * parameters.harmonics - 1.0f);

  ParameterInterpolator reflection_coefficient_modulation(
      &reflection_coefficient_, target_reflection_coefficient, size);
  ParameterInterpolator reflection_brightness_modulation(
      &reflection_brightness_, target_reflection_brightness, size);
  ParameterInterpolator reed_stiffness_modulation(
      &reed_stiffness_, target_reed_stiffness, size);
  ParameterInterpolator breath_noise_modulation(
      &breath_noise_, target_breath_noise, size);
  ParameterInterpolator pickup_position_modulation(
      &pickup_position_, target_pickup_position, size);
  ParameterInterpolator pickup_amount_modulation(
      &pickup_amount_, target_pickup_amount, size);

  for (size_t i = 0; i < size; ++i) {
    const float delay = delay_modulation.Next();
    const float reflection_coefficient = \
        reflection_coefficient_modulation.Next();
    const float reflection_brightness = \
        reflection_brightness_modulation.Next();
    const float reed_stiffness = reed_stiffness_modulation.Next();
    const float breath_noise = breath_noise_modulation.Next();
    const float pickup_position = pickup_position_modulation.Next();
    const float pickup_amount = pickup_amount_modulation.Next();
    breath_envelope_ += 0.018f * (target_breath - breath_envelope_);

    noise_state_ = noise_state_ * 1664525u + 1013904223u;
    const float noise = static_cast<float>(noise_state_ >> 9) * \
        (1.0f / 8388608.0f) - 1.0f;
    const float mouth_pressure = breath_envelope_ + noise * breath_noise * \
        breath_envelope_;

    const float delayed = bore_.Read(delay);
    return_filter_ += reflection_brightness * (delayed - return_filter_);
    const float pickup_wave = bore_.Read(max(2.0f, delay * pickup_position));
    const float returning = -reflection_coefficient * return_filter_;

    const float bore_pressure = outgoing_ + returning;
    const float pressure_difference = mouth_pressure - bore_pressure;
    // Only the oscillating part of the pressure difference moves the reed.
    // Letting the static breath term set the operating point too drove the
    // reed against its closed stop under hard blowing at high stiffness,
    // where it has no slope left and the note stopped dead. Anchoring the
    // rest position instead keeps the reed on the sloped part of its curve
    // at every setting, and lets stiffness span a much wider range.
    const float reed_drive = pressure_difference - breath_envelope_;
    float opening = kReedPipeRestOpening - reed_stiffness * reed_drive;
    CONSTRAIN(opening, 0.0f, 1.0f);
    const float curvature = 1.0f - 0.18f * \
        min(1.5f, fabsf(pressure_difference));
    const float reed_flow = opening * pressure_difference * curvature;

    const float transient = excitation_ * 0.4f;
    excitation_ *= 0.982f;
    outgoing_ = SoftClip(returning + reed_flow + transient);
    bore_.Write(outgoing_);

    // OUT listens to the bore and its movable pickup; AUX exposes the reed
    // flow. Separate one-pole DC blockers remove the static mouth-pressure
    // solution without erasing low notes.
    const float pressure = (1.0f - pickup_amount) * \
        (outgoing_ + returning) + pickup_amount * pickup_wave;
    const float dc_out = pressure - out_dc_input_ + \
        0.9975f * out_dc_output_;
    out_dc_input_ = pressure;
    out_dc_output_ = dc_out;

    const float dc_aux = reed_flow - aux_dc_input_ + \
        0.9975f * aux_dc_output_;
    aux_dc_input_ = reed_flow;
    aux_dc_output_ = dc_aux;

    out[i] = 0.58f * SoftClip(dc_out);
    // Reed flow is naturally much smaller than bore pressure. Give AUX enough
    // make-up gain to serve as a useful alternate output without changing the
    // nonlinear feedback path itself.
    aux[i] = 0.9f * SoftClip(2.8f * dc_aux);
  }
}

}  // namespace plaits
