// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' TOY* model: a bit-mangled phase ramp through a sample-and-hold
// clock, 4x oversampled and decimated.

#include "plaits/dsp/engine2/toy_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/downsampler/4x_downsampler.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' mangler, kept in uint8 arithmetic because the wrap is the sound:
//   held = (((phase >> 24) ^ (x << 1)) & (~x)) + (x >> 1)
// `~x` masks off every bit the operand sets, and the trailing bias term is
// what MACRO reparameterises -- Braids hardcodes it to x/2.
inline uint8_t MangleRamp(uint8_t ramp, uint8_t x, int bias) {
  const int masked = (ramp ^ (x << 1)) & (~static_cast<int>(x));
  return static_cast<uint8_t>(masked + bias);
}

}  // namespace

void ToyEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void ToyEngine::Reset() {
  phase_ = 0.0f;
  ramp_increment_ = 0.0f;
  decimation_counter_ = 0.0f;
  held_sample_ = 128;
  decimation_counter_r_ = 0.0f;
  held_sample_r_ = 128;
  downsampler_state_ = 0.0f;
  downsampler_state_r_ = 0.0f;
  dc_input_ = 0.0f;
  dc_output_ = 0.0f;
  dc_input_aux_ = 0.0f;
  dc_output_aux_ = 0.0f;
}

void ToyEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
    decimation_counter_ = 0.0f;
    decimation_counter_r_ = 0.0f;
  }

  // 4x oversampling: the ramp advances a quarter turn per sub-sample.
  const float target_increment = NoteToFrequency(parameters.note) * 0.25f;

  // HARMONICS is Braids' `x = parameter_[1] >> 8`, the mangle operand.
  const uint8_t x = static_cast<uint8_t>(parameters.harmonics * 127.0f);

  // MACRO scales the mangler's trailing bias. Braids welds it to x >> 1, so
  // the detent value of 0.5 reproduces it exactly.
  const float fold = ApplyMacro(0.5f, 0.0f, 1.5f, parameters.macro);
  const int bias = static_cast<int>(static_cast<float>(x) * fold);

  // TIMBRE sets the free-running hold count, Braids' pitch-independent clock.
  float free_count = kToyMaxDecimation - \
      (kToyMaxDecimation - 1.0f) * parameters.timbre;

  // MORPH crossfades that clock toward one that TRACKS the played note, so
  // the crush artefacts lock to the pitch instead of beating against it. The
  // ratio form makes the two coincide exactly at MIDI 60, so the knob does
  // not jump during a middle-C audition.
  const float note_frequency = max(1e-6f, NoteToFrequency(parameters.note));
  const float tracked_count = free_count * \
      NoteToFrequency(60.0f) / note_frequency;
  float count = free_count + \
      (tracked_count - free_count) * parameters.morph;
  CONSTRAIN(count, 1.0f, 4096.0f);
  const float count_r = count / kToyStereoClockRatio;

  ParameterInterpolator increment_modulation(
      &ramp_increment_, target_increment, size);

  Downsampler downsampler(&downsampler_state_);
  Downsampler downsampler_r(&downsampler_state_r_);

  for (size_t i = 0; i < size; ++i) {
    const float increment = increment_modulation.Next();
    float raw = 0.0f;

    for (int t = 0; t < 4; ++t) {
      phase_ += increment;
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      }
      const uint8_t ramp = static_cast<uint8_t>(phase_ * 256.0f);

      decimation_counter_ += 1.0f;
      if (decimation_counter_ >= count) {
        held_sample_ = MangleRamp(ramp, x, bias);
        decimation_counter_ = 0.0f;
      }
      const float sample = \
          (static_cast<float>(held_sample_) - 128.0f) * (1.0f / 128.0f);
      downsampler.Accumulate(t, sample);
      raw = sample;

      if (PLAITS_STEREO_TOY && parameters.stereo) {
        decimation_counter_r_ += 1.0f;
        if (decimation_counter_r_ >= count_r) {
          held_sample_r_ = MangleRamp(ramp, x, bias);
          decimation_counter_r_ = 0.0f;
        }
        downsampler_r.Accumulate(t, \
            (static_cast<float>(held_sample_r_) - 128.0f) * (1.0f / 128.0f));
      }
    }

    const float filtered = downsampler.Read();
    const float dc_out = filtered - dc_input_ + 0.9975f * dc_output_;
    dc_input_ = filtered;
    dc_output_ = dc_out;
    out[i] = dc_out;

    if (PLAITS_STEREO_TOY && parameters.stereo) {
      const float filtered_r = downsampler_r.Read();
      const float dc_r = filtered_r - dc_input_aux_ + 0.9975f * dc_output_aux_;
      dc_input_aux_ = filtered_r;
      dc_output_aux_ = dc_r;
      aux[i] = dc_r;
    } else {
      // The last sub-sample with no reconstruction filter: the same mangler
      // heard through its own aliasing, which is the lo-fi half of the model.
      const float dc_aux = raw - dc_input_aux_ + 0.9975f * dc_output_aux_;
      dc_input_aux_ = raw;
      dc_output_aux_ = dc_aux;
      aux[i] = dc_aux;
    }
  }
}

}  // namespace plaits
