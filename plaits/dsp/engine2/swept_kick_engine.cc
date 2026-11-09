// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// A 909-style kick.
//
// The drum this is aimed at builds its kick from three parts that Plaits'
// Analog Bass Drum does not separate: a VCO (a sawtooth core waveshaped toward
// a sine) whose frequency is driven down by its own fast pitch envelope, a VCA
// with a long DECAY, and an ATTACK layer -- a short pulse plus a burst of
// filtered noise -- mixed in on top. Plaits' synthetic kick lands near this
// sound by accident; this engine models the parts on purpose, so the sweep,
// the body and the attack can each be set without moving the others.
//
//   TIMBRE    -> TUNE: pitch-envelope depth and length, from a tight thud that
//                starts under an octave up to a longer boom that falls from
//                about five times the note.
//   MORPH     -> DECAY: amplitude decay, about 60 ms to 2 s.
//   HARMONICS -> ATTACK: level of the click and noise layer. Off at zero.
//   MACRO     -> Shape. Noon is the reference voicing (a waveshaped triangle,
//                close to the original VCO). Counter-clockwise cleans it to a
//                pure sine; clockwise drives it into soft saturation.
//
// OUT: the complete kick. AUX (mono): the attack layer alone, for processing
// it apart from the body. Stereo: the body stays centred, and each side gets
// its own independent attack noise.
//
// With TRIG unpatched the VCO drones at the note, with MORPH as its level, the
// way Plaits' own analog drum models behave.

#include "plaits/dsp/engine2/swept_kick_engine.h"

#include <algorithm>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

inline float DecayCoefficient(float seconds) {
  return 1.0f - 1.0f / max(2.0f, seconds * kSampleRate);
}

inline float SoftClip(float x) {
  return x / (1.0f + fabsf(x));
}

// Triangle that starts at a rising zero crossing, so it lines up with
// lut_sine and the VCO restarts without a step at the strike.
inline float RisingTriangle(float phase) {
  if (phase < 0.25f) {
    return 4.0f * phase;
  } else if (phase < 0.75f) {
    return 2.0f - 4.0f * phase;
  }
  return 4.0f * phase - 4.0f;
}

inline float Noise() {
  return 2.0f * Random::GetFloat() - 1.0f;
}

// Frequency of the attack's pitched click.
const float kClickFrequency = 1800.0f / kSampleRate;

// How long the amplitude envelope holds at full level before it decays.
const float kHoldSeconds = 0.0015f;

}  // namespace

void SweptKickEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  sustain_gain_ = 0.0f;
  Reset();
}

void SweptKickEngine::Reset() {
  phase_ = 0.0f;
  pitch_envelope_ = 0.0f;
  amplitude_envelope_ = 0.0f;
  amplitude_smoothed_ = 0.0f;
  hold_samples_ = 0;
  click_envelope_ = 0.0f;
  click_phase_ = 0.0f;
  noise_envelope_ = 0.0f;
  noise_lp_left_ = 0.0f;
  noise_hp_left_ = 0.0f;
  noise_lp_right_ = 0.0f;
  noise_hp_right_ = 0.0f;
  out_dc_ = 0.0f;
  aux_dc_ = 0.0f;
}

void SweptKickEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  const bool stereo = parameters.stereo;
  const bool sustain = parameters.trigger & TRIGGER_UNPATCHED;
  const float f0 = min(0.05f, NoteToFrequency(parameters.note));

  // MACRO: noon is the reference waveshaped-triangle VCO.
  const float macro = parameters.macro;
  float triangle_mix;
  float drive;
  if (macro <= 0.5f) {
    triangle_mix = 1.4f * macro;
    drive = 1.0f;
  } else {
    const float amount = 2.0f * (macro - 0.5f);
    triangle_mix = 0.7f + 0.3f * amount;
    drive = 1.0f + 6.0f * amount * amount;
  }
  const float drive_normalization = 1.0f / SoftClip(drive);

  // TIMBRE: sweep depth (9 to 28 semitones above the note, about 1.7x to 5x
  // its frequency) and length. Accent pushes it a little further, as a
  // harder hit on the original drum does.
  const float tune = parameters.timbre;
  const float sweep_semitones = 9.0f + 19.0f * tune +
      2.0f * parameters.accent;
  const float sweep_excess = SemitonesToRatio(sweep_semitones) - 1.0f;
  const float pitch_decay = DecayCoefficient(0.006f + 0.044f * tune * tune);

  // MORPH: amplitude decay, 60 ms to about 2 s.
  const float amplitude_decay = DecayCoefficient(
      0.06f * SemitonesToRatio(parameters.morph * 60.0f));

  // HARMONICS: attack layer. The click arrives first; the noise grows in more
  // slowly so low settings stay a clean tick.
  const float attack = parameters.harmonics;
  const float click_level = 0.6f * attack;
  const float noise_level = 0.55f * attack * (0.4f + 0.6f * attack);
  const float click_decay = DecayCoefficient(0.0011f);
  const float noise_decay = DecayCoefficient(0.0035f + 0.0050f * attack);

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    const float level = 0.3f + 0.7f * parameters.accent;
    phase_ = 0.0f;
    pitch_envelope_ = 1.0f;
    amplitude_envelope_ = level;
    hold_samples_ = static_cast<int>(kHoldSeconds * kSampleRate);
    click_envelope_ = level;
    click_phase_ = 0.0f;
    noise_envelope_ = level;
  }

  ParameterInterpolator sustain_gain(
      &sustain_gain_,
      sustain ? parameters.accent * parameters.morph : 0.0f,
      size);

  for (size_t i = 0; i < size; ++i) {
    float frequency;
    if (sustain) {
      frequency = f0;
    } else {
      frequency = f0 * (1.0f + sweep_excess * pitch_envelope_);
      pitch_envelope_ *= pitch_decay;
    }
    phase_ += min(frequency, 0.2f);
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }

    const float sine = SineNoWrap(phase_);
    const float triangle = RisingTriangle(phase_);
    const float shaped = 1.5f * triangle / (1.0f + 0.5f * fabsf(triangle));
    float body = sine + triangle_mix * (shaped - sine);
    if (drive > 1.0f) {
      body = SoftClip(body * drive) * drive_normalization;
    }

    float gain;
    if (sustain) {
      gain = sustain_gain.Next();
    } else {
      if (hold_samples_) {
        --hold_samples_;
      } else {
        amplitude_envelope_ *= amplitude_decay;
      }
      ONE_POLE(amplitude_smoothed_, amplitude_envelope_, 0.25f);
      gain = amplitude_smoothed_;
    }
    const float kick = body * gain;

    // Attack layer: a decaying 1.8 kHz click, plus band-limited noise.
    click_phase_ += kClickFrequency;
    if (click_phase_ >= 1.0f) {
      click_phase_ -= 1.0f;
    }
    const float click = SineNoWrap(click_phase_) * click_envelope_ *
        click_level;
    click_envelope_ *= click_decay;

    ONE_POLE(noise_lp_left_, Noise(), 0.30f);
    ONE_POLE(noise_hp_left_, noise_lp_left_, 0.03f);
    const float noise_gain = noise_envelope_ * noise_level;
    const float attack_left = click +
        (noise_lp_left_ - noise_hp_left_) * noise_gain;

    float left;
    float right;
    if (stereo) {
      ONE_POLE(noise_lp_right_, Noise(), 0.30f);
      ONE_POLE(noise_hp_right_, noise_lp_right_, 0.03f);
      const float attack_right = click +
          (noise_lp_right_ - noise_hp_right_) * noise_gain;
      left = kick + attack_left;
      right = kick + attack_right;
    } else {
      left = kick + attack_left;
      right = 1.6f * attack_left;
    }
    noise_envelope_ *= noise_decay;

    // Very low corner: the body is symmetric and should pass untouched; this
    // only removes the offset of a strike's first, unbalanced half-cycle.
    out_dc_ += 0.0004f * (left - out_dc_);
    aux_dc_ += 0.0004f * (right - aux_dc_);
    out[i] = 0.82f * (left - out_dc_);
    aux[i] = 0.82f * (right - aux_dc_);
  }
}

}  // namespace plaits
