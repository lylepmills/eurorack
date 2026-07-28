// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' VFOF: a bank of five narrow SVFs excited by a band-limited saw.

#include "plaits/dsp/engine2/vowel_fof_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/engine2/vowel_fof_data.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' InterpolateFormantParameter: bilinear across the vowel x register
// grid, per formant.
inline float InterpolateFormant(
    const int16_t* table, float vowel, float voice_register, int formant) {
  const float x = vowel * static_cast<float>(kVowelFofGridSize - 1);
  const float y = voice_register * static_cast<float>(kVowelFofGridSize - 1);
  int xi = static_cast<int>(x);
  int yi = static_cast<int>(y);
  CONSTRAIN(xi, 0, kVowelFofGridSize - 2);
  CONSTRAIN(yi, 0, kVowelFofGridSize - 2);
  const float xf = x - static_cast<float>(xi);
  const float yf = y - static_cast<float>(yi);
  const int stride = kVowelFofGridSize * kVowelFofNumFormants;
  const float a = static_cast<float>(
      table[xi * stride + yi * kVowelFofNumFormants + formant]);
  const float b = static_cast<float>(
      table[(xi + 1) * stride + yi * kVowelFofNumFormants + formant]);
  const float c = static_cast<float>(
      table[xi * stride + (yi + 1) * kVowelFofNumFormants + formant]);
  const float d = static_cast<float>(
      table[(xi + 1) * stride + (yi + 1) * kVowelFofNumFormants + formant]);
  const float ab = a + (b - a) * xf;
  const float cd = c + (d - c) * xf;
  return ab + (cd - ab) * yf;
}

}  // namespace

void VowelFofEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  excitation_.Init();
  Reset();
}

void VowelFofEngine::Reset() {
  for (int i = 0; i < kVowelFofNumFormants; ++i) {
    svf_lp_[i] = 0.0f;
    svf_bp_[i] = 0.0f;
  }
  noise_state_ = 0x21f2ac31;
}

void VowelFofEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Reset();
  }

  const float frequency = NoteToFrequency(parameters.note);

  // MACRO restores the formant amplitudes Braids leaves flat. At the detent
  // the tilt is zero and every formant is voiced at unity, exactly as the
  // amplitudes[0] read does on hardware.
  const float tilt = ApplyMacro(0.0f, -0.5f, 1.0f, parameters.macro);

  float f[kVowelFofNumFormants];
  float amplitude[kVowelFofNumFormants];
  float noise_makeup[kVowelFofNumFormants];

  for (int i = 0; i < kVowelFofNumFormants; ++i) {
    // MORPH is the vowel and TIMBRE the register, matching speech_engine.
    const float note = InterpolateFormant(
        kVowelFofFrequency, parameters.morph, parameters.timbre, i) *
        (1.0f / 128.0f);
    const float formant_frequency = NoteToFrequency(note);
    // Chamberlin f = 2 sin(pi fc / fs); Sine(x) is sin(2 pi x), so the
    // argument is half the normalized frequency.
    float coefficient = 2.0f * Sine(0.5f * formant_frequency);
    CONSTRAIN(coefficient, 0.0f, 1.0f);
    f[i] = coefficient;

    const float raw = InterpolateFormant(
        kVowelFofAmplitude, parameters.morph, parameters.timbre, i) *
        (1.0f / 16384.0f);
    // Linear interpolation between flat (Braids) and the table's own balance.
    amplitude[i] = 1.0f + (raw - 1.0f) * tilt;

    // A Q = 64 bandpass has bandwidth fc/64, so noise through a high formant
    // carries far more energy than through a low one. Level it by 1/sqrt(f).
    noise_makeup[i] = Sqrt(0.02f / max(coefficient, 1e-4f));
  }

  const float breath = parameters.harmonics;

  // Render the exciter for the whole block in one call. Asking for a single
  // sample at a time makes the oscillator rebuild its parameter interpolators
  // once per sample rather than once per block: 77% of the CPU budget against
  // 72%. The five-filter bank is the rest and is irreducible.
  float excitation[kMaxBlockSize];
  excitation_.Render<OSCILLATOR_SHAPE_SAW>(frequency, 0.0f, excitation, size);

  for (size_t i = 0; i < size; ++i) {
    const float saw = excitation[i] * kVowelFofExcitationScale;

    noise_state_ = noise_state_ * 1664525u + 1013904223u;
    const float noise = static_cast<float>(noise_state_ >> 9) * \
        (1.0f / 8388608.0f) - 1.0f;

    float sum = 0.0f;
    float sum_aux = 0.0f;
    for (int k = 0; k < kVowelFofNumFormants; ++k) {
      const float in = saw + (noise * noise_makeup[k] - saw) * breath;
      const float notch = in - svf_bp_[k] * kVowelFofDamping;
      svf_lp_[k] += f[k] * svf_bp_[k];
      CONSTRAIN(svf_lp_[k], -1.0f, 1.0f);
      const float hp = notch - svf_lp_[k];
      svf_bp_[k] += f[k] * hp;
      CONSTRAIN(svf_bp_[k], -1.0f, 1.0f);
      // Limit BEFORE the output scale. Braids drives at full scale and CLIPs
      // its state every sample; scaling first and limiting after makes the
      // limiter a no-op exactly where hardware is clipping hardest. Declared
      // deviation: Braids clips the STATE inside the resonant loop, so at
      // Q = 64 these states stay bounded where the port's only bounds the tap.
      const float tap = SoftClip(svf_bp_[k]);
      sum += tap * amplitude[k];
      sum_aux += tap * amplitude[kVowelFofNumFormants - 1 - k];
    }

    out[i] = kVowelFofOutputScale * sum;
    aux[i] = kVowelFofOutputScale * sum_aux;
  }
}

}  // namespace plaits
