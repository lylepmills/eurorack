// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' CSAW: a sawtooth with a notch cut into the start of every cycle.

#include "plaits/dsp/engine2/csaw_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// The sawtooth segment, reparameterised by MORPH. `u` runs 0..1 across the
// segment and the endpoints are pinned, so bend = 0 returns Braids' straight
// ramp exactly. Above zero the segment leaves at slope 0 and arrives at 2
// (convex); below zero the two are swapped (concave).
// One expression covers both signs: at bend = +1 it is u*u (leaves at slope
// 0, arrives at 2), at -1 it is 2u - u*u (the mirror), at 0 it is u.
inline float BendSegment(float u, float bend) {
  return u * (1.0f - bend) + bend * u * u;
}

// d/du of the above, needed at both segment ends for the integrated BLEP.
inline float BendSegmentSlope(float u, float bend) {
  return (1.0f - bend) + 2.0f * bend * u;
}

}  // namespace

void CSawEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void CSawEngine::Reset() {
  phase_ = 0.0f;
  frequency_ = 0.01f;
  pw_ = 0.25f;
  bend_ = 0.0f;
  tilt_ = 0.0f;
  previous_pw_ = 0.25f;
  high_ = false;
  depth_ = 0.0f;
  depth_aux_ = 0.0f;
  next_sample_ = 0.0f;
  next_sample_aux_ = 0.0f;
}

void CSawEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
    high_ = false;
    next_sample_ = 0.0f;
    next_sample_aux_ = 0.0f;
  }

  const float frequency = NoteToFrequency(parameters.note);

  const bool stereo = PLAITS_STEREO_CSAW && parameters.stereo;

  // HARMONICS is Braids' aux_parameter_, the notch depth. In STEREO the right
  // channel takes the mirrored knob position, so the two sides are never the
  // same waveform -- except at the knob centre, where the mirror is its own
  // fixed point. That collapse is why the mono AUX is a different waveform
  // rather than the same one moved.
  const float target_depth = kCSawDepthOffset + \
      kCSawDepthRange * parameters.harmonics;
  const float target_depth_aux = kCSawDepthOffset + \
      kCSawDepthRange * (1.0f - parameters.harmonics);

  // The DC term travels with the depth, so the mirrored channel mirrors it too
  // and both outputs stay inside the same bounds.
  const float dc = kCSawDcShift * (1.0f - parameters.harmonics);
  const float dc_aux = kCSawDcShift * parameters.harmonics;

  // TIMBRE is Braids' pulse width, floored at eight increments so the notch
  // never collapses below the BLEP's resolution.
  float pw_min = 8.0f * frequency;
  if (pw_min > 0.3f) {
    pw_min = 0.3f;
  }
  float target_pw = parameters.timbre * kCSawMaxPulseWidth;
  CONSTRAIN(target_pw, pw_min, kCSawMaxPulseWidth);

  const float target_bend = 2.0f * parameters.morph - 1.0f;
  const float target_tilt = ApplyMacro(0.0f, -1.0f, 1.0f, parameters.macro);

  ParameterInterpolator fm(&frequency_, frequency, size);
  ParameterInterpolator pwm(&pw_, target_pw, size);
  ParameterInterpolator bend_modulation(&bend_, target_bend, size);
  ParameterInterpolator tilt_modulation(&tilt_, target_tilt, size);

  float next_sample = next_sample_;
  float next_sample_aux = next_sample_aux_;

  for (size_t i = 0; i < size; ++i) {
    float this_sample = next_sample;
    float this_sample_aux = next_sample_aux;
    next_sample = 0.0f;
    next_sample_aux = 0.0f;

    const float f = fm.Next();
    const float pw = pwm.Next();
    const float bend = bend_modulation.Next();
    const float tilt = tilt_modulation.Next();

    // Slope of the tilted plateau, and of the segment at both of its ends.
    const float plateau_slope = tilt * (pw - depth_) / pw;
    const float slope_in = BendSegmentSlope(0.0f, bend);
    const float slope_out = BendSegmentSlope(1.0f, bend);

    phase_ += f;

    if (!high_ && phase_ >= pw) {
      // Leaving the plateau. The value step is what MACRO cancels at +1 and
      // doubles at -1, which is precisely why the slope term below matters.
      float t = (phase_ - pw) / (previous_pw_ - pw + f);
      CONSTRAIN(t, 0.0f, 1.0f);
      const float this_blep = ThisBlepSample(t);
      const float next_blep = NextBlepSample(t);
      const float this_int = ThisIntegratedBlepSample(t);
      const float next_int = NextIntegratedBlepSample(t);

      const float step = (pw - depth_) * (1.0f - tilt);
      this_sample += step * this_blep;
      next_sample += step * next_blep;

      const float slope_step = (slope_in - plateau_slope) * f;
      this_sample += slope_step * this_int;
      next_sample += slope_step * next_int;

      if (stereo) {
        const float step_aux = (pw - depth_aux_) * (1.0f - tilt);
        this_sample_aux += step_aux * this_blep;
        next_sample_aux += step_aux * next_blep;

        const float plateau_slope_aux = tilt * (pw - depth_aux_) / pw;
        const float slope_step_aux = (slope_in - plateau_slope_aux) * f;
        this_sample_aux += slope_step_aux * this_int;
        next_sample_aux += slope_step_aux * next_int;
      } else {
        // The pulse rises here. It is piecewise constant, so it carries VALUE
        // blep only -- no slope discontinuity, hence no integrated term.
        this_sample_aux += kCSawPulseStep * this_blep;
        next_sample_aux += kCSawPulseStep * next_blep;
      }

      high_ = true;
    } else if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
      float t = phase_ / f;
      CONSTRAIN(t, 0.0f, 1.0f);
      const float this_blep = ThisBlepSample(t);
      const float next_blep = NextBlepSample(t);
      const float this_int = ThisIntegratedBlepSample(t);
      const float next_int = NextIntegratedBlepSample(t);

      // Braids latches the depth here, inside the wrap, and uses the NEW
      // value as the step's destination.
      depth_ = target_depth;

      const float step = depth_ - 1.0f;
      this_sample += step * this_blep;
      next_sample += step * next_blep;

      const float new_plateau_slope = tilt * (pw - depth_) / pw;
      const float slope_step = (new_plateau_slope - slope_out) * f;
      this_sample += slope_step * this_int;
      next_sample += slope_step * next_int;

      if (stereo) {
        depth_aux_ = target_depth_aux;
        const float step_aux = depth_aux_ - 1.0f;
        this_sample_aux += step_aux * this_blep;
        next_sample_aux += step_aux * next_blep;

        const float new_plateau_slope_aux = tilt * (pw - depth_aux_) / pw;
        const float slope_step_aux = (new_plateau_slope_aux - slope_out) * f;
        this_sample_aux += slope_step_aux * this_int;
        next_sample_aux += slope_step_aux * next_int;
      } else {
        this_sample_aux -= kCSawPulseStep * this_blep;
        next_sample_aux -= kCSawPulseStep * next_blep;
      }

      high_ = false;
    }

    // Naive waveform: a tilted plateau up to pw, then the bent segment.
    float naive;
    float naive_aux;
    if (phase_ < pw) {
      const float ramp = phase_ / pw;
      naive = depth_ + tilt * (pw - depth_) * ramp;
      naive_aux = stereo
          ? depth_aux_ + tilt * (pw - depth_aux_) * ramp
          : -1.0f;
    } else {
      const float u = (phase_ - pw) / (1.0f - pw);
      naive = pw + (1.0f - pw) * BendSegment(u, bend);
      naive_aux = stereo ? naive : 1.0f;
    }
    next_sample += naive;
    next_sample_aux += naive_aux;
    previous_pw_ = pw;

    out[i] = kCSawMakeUp * (this_sample - 0.5f + dc);
    aux[i] = stereo
        ? kCSawMakeUp * (this_sample_aux - 0.5f + dc_aux)
        // The pulse's mean travels with its duty, so it comes off here rather
        // than through a blocker: -1 for the plateau's `pw` of the cycle and
        // +1 for the rest averages to exactly 1 - 2*pw, and TIMBRE moves pw
        // far too fast for a 7 Hz one-pole to follow without pumping.
        : kCSawPulseGain * (this_sample_aux - (1.0f - 2.0f * pw));
  }

  next_sample_ = next_sample;
  next_sample_aux_ = next_sample_aux;
}

}  // namespace plaits
