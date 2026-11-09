// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Hi-hats, rides and crashes aimed at the 606 and 909.
//
// The two drums get their metal very differently. The 606, like the 808,
// builds it from a cluster of square-wave oscillators, band-passed and then
// high-passed. The 909's hats and cymbals were 6-bit recordings of real
// cymbals, played back from ROM through an analog VCA, which is where its
// grainy, lo-fi sheen comes from. A synthesis engine cannot reproduce those
// recordings, so the 909 end is an approximation of the recipe instead: a
// denser cluster (the 606 squares ring-modulated against six higher ones, plus
// noise), filtered, then resampled at 32 kHz and quantized to 6 bits BEFORE the
// envelope, so the quantization grain rides down with the decay just as it does
// when a VCA follows a DAC.
//
//   HARMONICS -> Model: 606 analog squares, crossfading to the 909-style
//                ring-modulated cluster and its 6-bit sample stage.
//   TIMBRE    -> Tone: filter cutoff, from dark and woody to bright and thin.
//   MORPH     -> Decay: closed hat (a 12 ms time constant), open hat, ride,
//                then crash (about 3 s). The strike wash grows with decay, as it does
//                from a hat to a crash.
//   MACRO     -> Spread: oscillator spread around their reference ratios.
//                Noon is the reference; counter-clockwise narrows toward a
//                cowbell-like cluster, clockwise opens it wider.
//
// OUT: the complete cymbal. AUX (mono): the band-passed body without the high
// pass or the sample stage, a darker ping under the sizzle. Stereo: OUT and AUX
// carry left and right channels with slightly offset filters, independent
// noise and staggered resampling clocks.
//
// With TRIG unpatched the cluster sounds continuously, with MORPH as its
// level, like Plaits' own analog hi-hat.

#include "plaits/dsp/engine2/hats_rides_engine.h"

#include <algorithm>

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

inline float Noise() {
  return 2.0f * Random::GetFloat() - 1.0f;
}

// Six square oscillators for the analog cluster, then six higher ones that are
// ring-modulated (XORed) against them for the dense cluster. Ratios are
// relative to the played note. The lower set is a 606-style spacing, close to
// but deliberately not the 808 set Plaits' Analog Hi-hat already uses.
const float kRatios[12] = {
    1.000f, 1.342f, 1.523f, 1.867f, 2.110f, 2.731f,
    2.830f, 3.170f, 3.710f, 4.290f, 4.940f, 5.720f
};

// The 909's cymbal ROMs: 6 bits, read back at about 32 kHz.
const float kSampleStageRate = 32000.0f / kSampleRate;
const float kSampleStageLevels = 31.0f;
const float kSampleStageHeadroom = 2.5f;

}  // namespace

void HatsRidesEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  sustain_gain_ = 0.0f;
  Reset();
}

void HatsRidesEngine::InitChannel(Channel* channel, float hold_phase) {
  channel->body.Init();
  channel->high_pass.Init();
  channel->hold_phase = hold_phase;
  channel->held = 0.0f;
}

void HatsRidesEngine::Reset() {
  // Spread the starting phases so the cluster does not open on a coherent
  // edge from all twelve oscillators at once.
  for (int i = 0; i < 12; ++i) {
    phase_[i] = static_cast<uint32_t>(i) * 0x1F3D5B79u;
  }
  envelope_ = 0.0f;
  strike_ = 0.0f;
  InitChannel(&left_, 0.0f);
  InitChannel(&right_, 0.5f);
}

float HatsRidesEngine::ProcessChannel(
    Channel* channel,
    float source,
    float crush,
    float* body) {
  const float band = channel->body.Process<FILTER_MODE_BAND_PASS>(source);
  *body = band;
  const float metal = channel->high_pass.Process<FILTER_MODE_HIGH_PASS>(
      0.6f * band + 0.4f * source);
  if (crush <= 0.0f) {
    return metal;
  }

  // The sample stage: resample at 32 kHz with a zero-order hold (keeping the
  // images, which are part of the sound), and quantize to 6 bits at full
  // scale.
  channel->hold_phase += kSampleStageRate;
  if (channel->hold_phase >= 1.0f) {
    channel->hold_phase -= 1.0f;
    float scaled = metal * kSampleStageHeadroom;
    CONSTRAIN(scaled, -1.0f, 1.0f);
    const float step = scaled * kSampleStageLevels;
    const float rounded = static_cast<float>(
        static_cast<int>(step + (step >= 0.0f ? 0.5f : -0.5f)));
    channel->held = rounded / (kSampleStageLevels * kSampleStageHeadroom);
  }
  return metal + crush * (channel->held - metal);
}

void HatsRidesEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  const bool stereo = parameters.stereo;
  const bool sustain = parameters.trigger & TRIGGER_UNPATCHED;
  const float f0 = NoteToFrequency(parameters.note);
  const float model = parameters.harmonics;
  const float spread = ApplyMacro(1.0f, 0.45f, 1.7f, parameters.macro);

  uint32_t increment[12];
  for (int i = 0; i < 12; ++i) {
    float f = f0 * (1.0f + (kRatios[i] - 1.0f) * spread);
    CONSTRAIN(f, 0.0f, 0.45f);
    increment[i] = static_cast<uint32_t>(f * 4294967296.0f);
  }

  // TIMBRE: a high pass from 1.8 kHz to about 14 kHz, with the body band-pass
  // a little below it. The 606 end rings more than the 909 end.
  float cutoff = 1800.0f / kSampleRate * SemitonesToRatio(
      parameters.timbre * 36.0f);
  CONSTRAIN(cutoff, 0.0f, 0.3f);
  const float body_q = 1.3f + 1.7f * (1.0f - model);
  left_.body.set_f_q<FREQUENCY_ACCURATE>(cutoff * 0.8f, body_q);
  left_.high_pass.set_f_q<FREQUENCY_ACCURATE>(cutoff, 0.7f);
  if (stereo) {
    right_.body.set_f_q<FREQUENCY_ACCURATE>(cutoff * 0.74f, body_q);
    right_.high_pass.set_f_q<FREQUENCY_ACCURATE>(
        min(cutoff * 1.09f, 0.3f), 0.7f);
  }

  // MORPH: closed hat to crash.
  const float decay = DecayCoefficient(
      0.012f * SemitonesToRatio(parameters.morph * 96.0f));
  const float strike_decay = DecayCoefficient(0.006f);
  const float strike_level = 0.8f * parameters.morph;

  const float noise_level = 0.06f + 0.30f * model;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    const float level = 0.3f + 0.7f * parameters.accent;
    envelope_ = level;
    strike_ = level;
  }

  ParameterInterpolator sustain_gain(
      &sustain_gain_,
      sustain ? parameters.accent * parameters.morph : 0.0f,
      size);

  uint32_t phase[12];
  for (int i = 0; i < 12; ++i) {
    phase[i] = phase_[i];
  }

  for (size_t n = 0; n < size; ++n) {
    uint32_t analog = 0;
    uint32_t dense = 0;
    for (int i = 0; i < 6; ++i) {
      phase[i] += increment[i];
      phase[i + 6] += increment[i + 6];
      const uint32_t low = phase[i] >> 31;
      const uint32_t high = phase[i + 6] >> 31;
      analog += low;
      dense += low ^ high;
    }
    const float analog_metal = 0.333f * static_cast<float>(analog) - 1.0f;
    const float dense_metal = 0.333f * static_cast<float>(dense) - 1.0f;
    const float metal = analog_metal + model * (dense_metal - analog_metal);

    float gain;
    if (sustain) {
      gain = sustain_gain.Next();
    } else {
      gain = envelope_ + strike_level * strike_;
      envelope_ *= decay;
      strike_ *= strike_decay;
    }

    float body_left;
    const float left = ProcessChannel(
        &left_, metal + noise_level * Noise(), model, &body_left);
    if (stereo) {
      float body_right;
      const float right = ProcessChannel(
          &right_, metal + noise_level * Noise(), model, &body_right);
      out[n] = 1.6f * left * gain;
      aux[n] = 1.6f * right * gain;
    } else {
      out[n] = 1.6f * left * gain;
      aux[n] = 0.5f * body_left * gain;
    }
  }

  for (int i = 0; i < 12; ++i) {
    phase_[i] = phase[i];
  }
}

}  // namespace plaits
