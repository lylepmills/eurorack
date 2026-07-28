// Copyright 1995-2023 Perry R. Cook and Gary P. Scavone.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// STK's BandedWG -- Essl and Cook's banded waveguides -- as one Plaits engine.

#include "plaits/dsp/engine2/banded_waveguide_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Free-free bar: the classic inharmonic series.
const float kUniformBarModes[] = { 1.0f, 2.756f, 5.404f, 8.933f };
const float kUniformBarGains[] = { 0.9f, 0.81f, 0.729f, 0.6561f };
const float kUniformBarExcitation[] = { 1.0f, 1.0f, 1.0f, 1.0f };

// Marimba bar, undercut so the second mode sits two octaves up.
const float kTunedBarModes[] = {
    1.0f, 4.0198391420f, 10.7184986595f, 18.0697050938f };
const float kTunedBarGains[] = { 0.999f, 0.998001f, 0.997003f, 0.996006f };
const float kTunedBarExcitation[] = { 1.0f, 1.0f, 1.0f, 1.0f };

const float kGlassModes[] = { 1.0f, 2.32f, 4.25f, 6.63f, 9.38f };
const float kGlassGains[] = {
    0.999f, 0.998001f, 0.997003f, 0.996006f, 0.99501f };
const float kGlassExcitation[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

// Tibetan prayer bowl, ICMC'02. The first six of upstream's twelve: three
// near-unison PAIRS, which is where the beating comes from.
const float kBowlModes[] = {
    0.996108344f, 1.0038916562f, 2.979178f, 2.99329767f, 5.704452f, 5.704452f };
const float kBowlGains[] = {
    0.999925960128219f, 0.999925960128219f,
    0.999982774366897f, 0.999982774366897f,
    1.0f, 1.0f };
const float kBowlExcitation[] = {
    1.1900357f, 1.1900357f, 1.0914886f, 1.0914886f, 4.2995041f, 4.2995041f };

const int kDelayCapacity[kBandedWgMaxModes] = {
  kBandedWgDelaySize0,
  kBandedWgDelaySize1,
  kBandedWgDelaySize2,
  kBandedWgDelaySize3,
  kBandedWgDelaySize4,
  kBandedWgDelaySize5
};

// Upstream's BowTable: a friction curve that falls off as the fourth power of
// the differential velocity, floored and ceilinged. The stick-slip lives here.
inline float BowTable(float input, float slope) {
  float sample = input * slope;
  sample = fabsf(sample) + 0.75f;
  const float squared = sample * sample;
  float output = 1.0f / (squared * squared);
  CONSTRAIN(output, 0.01f, 0.98f);
  return output;
}

}  // namespace

const BandedWgPreset kBandedWgPresets[kBandedWgNumPresets] = {
  { "Uniform bar", 4, kUniformBarModes, kUniformBarGains,
      kUniformBarExcitation },
  { "Tuned bar", 4, kTunedBarModes, kTunedBarGains, kTunedBarExcitation },
  { "Glass harmonica", 5, kGlassModes, kGlassGains, kGlassExcitation },
  { "Prayer bowl", 6, kBowlModes, kBowlGains, kBowlExcitation },
};

void BandedWaveguideEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kBandedWgMaxModes; ++i) {
    delay_capacity_[i] = kDelayCapacity[i];
    delay_line_[i] = allocator->Allocate<float>(kDelayCapacity[i]);
  }
  preset_ = -1;
  previous_frequency_ = 0.0f;
  Reset();
}

void BandedWaveguideEngine::Reset() {
  num_modes_ = kBandedWgPresets[0].num_modes;
  gain_normalization_ = 1.0f;
  velocity_input_ = 0.0f;
  for (int i = 0; i < kBandedWgMaxModes; ++i) {
    delay_length_[i] = 2;
    delay_write_[i] = 0;
    for (int j = 0; j < delay_capacity_[i]; ++j) {
      delay_line_[i][j] = 0.0f;
    }
    bp_a1_[i] = 0.0f;
    bp_a2_[i] = 0.0f;
    bp_b0_[i] = 0.0f;
    bp_x1_[i] = 0.0f;
    bp_x2_[i] = 0.0f;
    bp_y1_[i] = 0.0f;
    bp_y2_[i] = 0.0f;
  }
  preset_ = -1;
  previous_frequency_ = 0.0f;
}

void BandedWaveguideEngine::Pluck(float amplitude) {
  // Upstream fills each line with a burst whose length is scaled by the ratio
  // of that line to the shortest, so the excitation arrives at every mode at
  // once rather than smearing across them.
  const BandedWgPreset& p = kBandedWgPresets[preset_ < 0 ? 0 : preset_];
  const int shortest = max(delay_length_[num_modes_ - 1], 1);
  for (int i = 0; i < num_modes_; ++i) {
    const int count = min(delay_length_[i] / shortest, delay_length_[i]);
    const float value = p.excitation[i] * amplitude /
        static_cast<float>(num_modes_);
    for (int j = 0; j < count; ++j) {
      delay_line_[i][delay_write_[i]] = value;
      delay_write_[i] = (delay_write_[i] + 1) % delay_length_[i];
    }
  }
}

void BandedWaveguideEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  int preset = static_cast<int>(parameters.harmonics * kBandedWgNumPresets);
  CONSTRAIN(preset, 0, kBandedWgNumPresets - 1);
  const BandedWgPreset& p = kBandedWgPresets[preset];

  // Fold rather than allocate for a register no bar or bowl occupies.
  float note = parameters.note;
  while (note < kBandedWgLowestNote) {
    note += 12.0f;
  }
  float frequency = NoteToFrequency(note) * kSampleRate;
  CONSTRAIN(frequency, 1.0f, kBandedWgHighestFrequency);

  const bool retune = preset != preset_ ||
      fabsf(frequency - previous_frequency_) > 0.01f;
  if (retune) {
    preset_ = preset;
    previous_frequency_ = frequency;
    const float base = kSampleRate / frequency;
    const float radius = max(
        1.0f - float(M_PI) * kBandedWgBandwidth / kSampleRate, 0.0f);

    num_modes_ = p.num_modes;
    for (int i = 0; i < p.num_modes; ++i) {
      int length = static_cast<int>(base / p.modes[i]);
      if (length <= 2 || i >= kBandedWgMaxModes) {
        // Upstream drops every mode from here up rather than letting a
        // sub-two-sample delay through.
        num_modes_ = i;
        break;
      }
      CONSTRAIN(length, 2, delay_capacity_[i]);
      if (length != delay_length_[i]) {
        for (int j = 0; j < delay_capacity_[i]; ++j) {
          delay_line_[i][j] = 0.0f;
        }
        delay_write_[i] = 0;
        delay_length_[i] = length;
      }

      float mode_frequency = frequency * p.modes[i];
      CONSTRAIN(mode_frequency, 1.0f, 0.49f * kSampleRate);
      bp_a2_[i] = radius * radius;
      bp_a1_[i] = -2.0f * radius *
          cosf(2.0f * float(M_PI) * mode_frequency / kSampleRate);
      // Peak-normalised: zeros at +-1 with a gain that puts unity at the
      // resonance. This is what makes the loop gain equal to the mode gain.
      bp_b0_[i] = 0.5f - 0.5f * bp_a2_[i];
      bp_x1_[i] = 0.0f;
      bp_x2_[i] = 0.0f;
      bp_y1_[i] = 0.0f;
      bp_y2_[i] = 0.0f;
    }
    if (num_modes_ < 1) {
      num_modes_ = 1;
    }
    // Presets are normalised so the FUNDAMENTAL always sees the full loop
    // gain, and only the relative damping between modes is the preset's.
    // Without this the uniform bar is inaudible under a bow: its gains are
    // pow(0.9, i+1), so its loop gain is 0.899 against the other presets'
    // 0.999, and it measured 600x quieter than its neighbours. Upstream never
    // hits this because a uniform bar defaults to being STRUCK, and a struck
    // bar wants exactly that damping.
    float largest = 0.0f;
    for (int i = 0; i < num_modes_; ++i) {
      largest = max(largest, p.base_gains[i]);
    }
    gain_normalization_ = largest > 0.0f ? 1.0f / largest : 1.0f;
  }

  // MORPH is bow velocity, from barely moving to hard. Upstream makes zero
  // mean "pluck instead of bow" and puts the choice on a sustain pedal, which
  // here would make the bottom of a knob SILENT whenever TRIG is unpatched --
  // a third of the audition rendered as nothing. The pluck is not lost, it
  // just moves to the trigger, where a struck bar belongs anyway; MORPH stays
  // continuously alive.
  //
  // The floor is set by MEASUREMENT, not by upstream's number. The bow table's
  // grip falls off as the FOURTH power of the difference between bow and bar
  // velocity, so there is a genuine minimum bow speed below which a ringing bar
  // is damped rather than driven -- a real bow has one too. Swept against this
  // engine it sits near 0.055, so upstream's 0.03 floor leaves the bottom fifth
  // of the knob silent. The range starts at the threshold instead.
  //
  // Bow force also costs grip (see TIMBRE below), so velocity is compensated
  // for it, which is what a player does with the same trade.
  const float bow_force = parameters.timbre;
  const float bow_velocity = (0.055f + 0.055f * parameters.morph) *
      (1.0f + 1.0f * bow_force);

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Pluck(0.7f);
    velocity_input_ = 0.0f;
  }

  // TIMBRE is bow force, as upstream's steep-to-broad friction slope. Upstream
  // runs it to 1.0, which measures as silence from about 0.6 of the knob up --
  // a broad friction curve simply cannot hold the bar. Stopped at 3.0, where
  // the character has fully changed and the bar still speaks.
  const float slope = 10.0f - 7.0f * bow_force;

  // MACRO is the loop gain, i.e. how long it rings. Capped short of 1.0
  // because the bandpasses are peak-normalised and the delays lossless, so at
  // unity the bank self-oscillates whether or not it is being bowed.
  const float loop_gain = ApplyMacro(
      0.999f, 0.9f, kBandedWgMaxLoopGain, parameters.macro);

  for (size_t i = 0; i < size; ++i) {
    velocity_input_ = 0.0f;
    for (int k = 0; k < num_modes_; ++k) {
      const int read = (delay_write_[k] + 1) % delay_length_[k];
      velocity_input_ += loop_gain * delay_line_[k][read];
    }
    float input = bow_velocity - velocity_input_;
    input *= BowTable(input, slope);
    input /= static_cast<float>(num_modes_);

    float data = 0.0f;
    float fundamental = 0.0f;
    for (int k = 0; k < num_modes_; ++k) {
      const int read = (delay_write_[k] + 1) % delay_length_[k];
      const float delayed = delay_line_[k][read];
      const float x = input + p.base_gains[k] * gain_normalization_ *
          loop_gain * delayed;

      const float y = bp_b0_[k] * (x - bp_x2_[k])
          - bp_a1_[k] * bp_y1_[k] - bp_a2_[k] * bp_y2_[k];
      bp_x2_[k] = bp_x1_[k];
      bp_x1_[k] = x;
      bp_y2_[k] = bp_y1_[k];
      bp_y1_[k] = y;

      delay_line_[k][delay_write_[k]] = y;
      delay_write_[k] = (delay_write_[k] + 1) % delay_length_[k];

      data += y;
      if (k == 0) {
        fundamental = y;
      }
    }

    // Upstream's fixed x4 makeup. Clipped rather than left to run away: a bow
    // driving a bank at loop gain 0.9999 genuinely can, and a positive
    // postProcessing gain would bypass the limiter.
    out[i] = SoftClip(data * 4.0f);
    // AUX is the fundamental band alone -- the pitch under the inharmonic
    // bank, which is exactly what a bar or bowl makes hard to hear.
    aux[i] = SoftClip(fundamental * 4.0f);
  }
}

}  // namespace plaits
