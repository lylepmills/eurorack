// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SAW_SQUARE: a variable saw and a variable square, driven from the
// same TIMBRE knob and crossfaded by COLOR.

#include "plaits/dsp/engine2/saw_square_engine.h"

#include "plaits/build_config.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void SawSquareEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void SawSquareEngine::Reset() {
  phase_ = 0.0f;
  frequency_ = 0.01f;
  pw_saw_ = 0.25f;
  previous_pw_saw_ = 0.25f;
  high_saw_ = false;
  next_sample_saw_ = 0.0f;

  phase_square_ = 0.0f;
  pw_square_ = 0.25f;
  previous_pw_square_ = 0.25f;
  high_square_ = false;
  next_sample_square_ = 0.0f;

  offset_ = 0.0f;
  atten_ = kSawSquareAttenuationStock;
  blend_ = 0.0f;
}

void SawSquareEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  // MORPH's phase offset. Applied as a one-time nudge to the square's own
  // running phase -- a constant offset then holds forever (both phases
  // accumulate the same frequency every sample, so a fixed difference
  // between them never drifts), and moving MORPH nudges it again.
  const float target_offset =
      (parameters.morph - 0.5f) * kSawSquareMaxPhaseOffset;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
    phase_square_ = target_offset;
    if (phase_square_ < 0.0f) {
      phase_square_ += 1.0f;
    } else if (phase_square_ >= 1.0f) {
      phase_square_ -= 1.0f;
    }
    high_saw_ = false;
    high_square_ = false;
    next_sample_saw_ = 0.0f;
    next_sample_square_ = 0.0f;
  } else {
    phase_square_ += (target_offset - offset_);
    if (phase_square_ >= 1.0f) {
      phase_square_ -= 1.0f;
    } else if (phase_square_ < 0.0f) {
      phase_square_ += 1.0f;
    }
  }
  offset_ = target_offset;

  const bool stereo = PLAITS_STEREO_SAW_SQUARE && parameters.stereo;

  const float frequency = NoteToFrequency(parameters.note);

  // TIMBRE opens the saw's pw as it closes the square's -- one shared knob,
  // opposite directions (analog_oscillator.cc:354 and :206).
  float target_pw_saw = 0.5f * parameters.timbre;
  float target_pw_square = 0.5f * (1.0f - parameters.timbre);
  CONSTRAIN(target_pw_saw, kSawSquareSawPwMin, 0.5f);
  CONSTRAIN(target_pw_square, kSawSquareSquarePwMin, 0.5f);

  // Anti-aliasing floor beyond Braids' own fixed one (see the header):
  // guarantees the BLEP crossing time stays inside one sample even past the
  // pitch range Braids' hardware ever plays.
  if (frequency >= 0.25f) {
    target_pw_saw = 0.5f;
    target_pw_square = 0.5f;
  } else {
    CONSTRAIN(target_pw_saw, 2.0f * frequency, 1.0f - 2.0f * frequency);
    CONSTRAIN(target_pw_square, 2.0f * frequency, 1.0f - 2.0f * frequency);
  }

  // MACRO reveals macro_oscillator.cc:149-150's fixed 148/256 attenuation as
  // a range: silent at 0.0, unattenuated at 1.0, stock at 0.578125.
  const float target_atten = ApplyMacro(
      kSawSquareAttenuationStock, 0.0f, 1.0f, parameters.macro);

  ParameterInterpolator fm(&frequency_, frequency, size);
  // Braids interpolates the crossfade across the block too
  // (macro_oscillator.cc:145-153 BEGIN/INTERPOLATE_PARAMETER_1); without this
  // a moving COLOR steps once per block, which is exactly the artefact
  // braids/parameter_interpolation.h exists to suppress. Same shape as
  // fold_engine.cc's blend_modulation.
  ParameterInterpolator blend_modulation(
      &blend_, parameters.harmonics, size);
  ParameterInterpolator pw_saw_modulation(&pw_saw_, target_pw_saw, size);
  ParameterInterpolator pw_square_modulation(
      &pw_square_, target_pw_square, size);
  ParameterInterpolator atten_modulation(&atten_, target_atten, size);

  float next_sample_saw = next_sample_saw_;
  float next_sample_square = next_sample_square_;

  for (size_t i = 0; i < size; ++i) {
    float this_sample_saw = next_sample_saw;
    float this_sample_square = next_sample_square;
    next_sample_saw = 0.0f;
    next_sample_square = 0.0f;

    float f = fm.Next();
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      f += parameters.frequency_offset[i];
      CONSTRAIN(f, 0.0f, 0.25f);
    }
#endif
    const float pw_saw = pw_saw_modulation.Next();
    const float pw_square = pw_square_modulation.Next();
    const float atten = atten_modulation.Next();
    const float blend = blend_modulation.Next();

    // The saw: two ramps of slope 2, each cut by a unit downward jump -- the
    // exact naive waveform RenderVariableSaw's `phase_ >> 18` and
    // `(phase_ - pw) >> 18` terms sum to (see the header comment; the closed
    // form is independent of Braids' internal BLEP scaling).
    phase_ += f;
    while (true) {
      if (!high_saw_) {
        if (phase_ < pw_saw) {
          break;
        }
        float t = (phase_ - pw_saw) / (previous_pw_saw_ - pw_saw + f);
        CONSTRAIN(t, 0.0f, 1.0f);
        this_sample_saw -= ThisBlepSample(t);
        next_sample_saw -= NextBlepSample(t);
        high_saw_ = true;
      }
      if (high_saw_) {
        if (phase_ < 1.0f) {
          break;
        }
        phase_ -= 1.0f;
        float t = phase_ / f;
        CONSTRAIN(t, 0.0f, 1.0f);
        this_sample_saw -= ThisBlepSample(t);
        next_sample_saw -= NextBlepSample(t);
        high_saw_ = false;
      }
    }
    next_sample_saw += (phase_ < pw_saw)
        ? (2.0f * phase_ - pw_saw)
        : (2.0f * phase_ - pw_saw - 1.0f);
    previous_pw_saw_ = pw_saw;

    // The square: the plain two-level pulse (analog_oscillator.cc:188-272),
    // accumulated in a 0..1 internal domain and mapped to +-1 below, matching
    // the convention VariableShapeOscillator's own square mode uses.
    phase_square_ += f;
    while (true) {
      if (!high_square_) {
        if (phase_square_ < pw_square) {
          break;
        }
        float t = (phase_square_ - pw_square) /
            (previous_pw_square_ - pw_square + f);
        CONSTRAIN(t, 0.0f, 1.0f);
        this_sample_square += ThisBlepSample(t);
        next_sample_square += NextBlepSample(t);
        high_square_ = true;
      }
      if (high_square_) {
        if (phase_square_ < 1.0f) {
          break;
        }
        phase_square_ -= 1.0f;
        float t = phase_square_ / f;
        CONSTRAIN(t, 0.0f, 1.0f);
        this_sample_square -= ThisBlepSample(t);
        next_sample_square -= NextBlepSample(t);
        high_square_ = false;
      }
    }
    next_sample_square += (phase_square_ < pw_square) ? 0.0f : 1.0f;
    previous_pw_square_ = pw_square;

    const float saw = this_sample_saw;
    const float square = 2.0f * this_sample_square - 1.0f;
    const float attenuated_square = square * atten;

    // macro_oscillator.cc:151 Mix(saw, attenuated_square, balance): COLOR = 0
    // is pure saw, COLOR = 1 is (almost exactly) pure attenuated square.
    float blend_main = blend;
    float blend_aux = 1.0f - blend;
    if (stereo) {
      blend_main = blend - kSawSquareStereoBlend;
      blend_aux = blend + kSawSquareStereoBlend;
      CONSTRAIN(blend_main, 0.0f, 1.0f);
      CONSTRAIN(blend_aux, 0.0f, 1.0f);
    }

    out[i] = saw + (attenuated_square - saw) * blend_main;
    aux[i] = saw + (attenuated_square - saw) * blend_aux;
  }

  next_sample_saw_ = next_sample_saw;
  next_sample_square_ = next_sample_square;
}

}  // namespace plaits
