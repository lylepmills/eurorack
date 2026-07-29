// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' HARMONICS: twelve partials under two Lorentzian bumps.

#include "plaits/dsp/engine2/harmonics_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' wav_sine is NOT Plaits' lut_sine. It starts at -32512 and reaches
// zero a quarter of the way through, so it is -cos(2*pi*phase); Plaits' starts
// at 0 and is sin(2*pi*phase). A quarter cycle apart.
//
// All partials share this convention, so it does not move the magnitude
// spectrum or RMS -- but it does move the waveform and therefore the peak.
// Under -cos all twelve partials align at phase 0 and the sum reaches the
// normalizer's full scale, which is the bound the positive output gain rests
// on (SPEC R1). Under sin they would align at a quarter cycle in and cancel
// differently. Render() obtains -cos(n * phase) from a cosine recurrence.

// Braids' ToParameter equivalent: the module's knobs arrive as int16 0..32767
// and the width computation at digital_oscillator.cc:936-939 quantises hard
// enough that the port has to enter the same integer.
inline int32_t ToBraidsParameter(float normalized) {
  int32_t value = static_cast<int32_t>(normalized * 32767.0f);
  CONSTRAIN(value, 0, 32767);
  return value;
}

}  // namespace

void HarmonicsEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

// SPEC R15: another engine's buffers alias this one's, so every accumulator is
// zeroed on engine switch. Zero is also exactly where Braids starts --
// DigitalOscillator::Init() memsets state_ (digital_oscillator.h:246-258) and
// Render() calls it on every shape change (:111-115) -- and for THIS model the
// starting state is audible, because the >> 8 slew never lifts a partial whose
// target is under 256 off zero.
void HarmonicsEngine::Reset() {
  phase_ = 0.0f;
  frequency_ = 0.01f;
  for (int i = 0; i < kHarmonicsNumPartials; ++i) {
    amplitude_[i] = 0;
    amplitude_aux_[i] = 0;
  }
}

// digital_oscillator.cc:932-962, in the same integer arithmetic. Spread offsets
// the second bump's position and width_scale scales the bump width; both are
// neutral at their stock values, so the whole function reduces to Braids'
// exactly when MORPH and MACRO sit at noon.
void HarmonicsEngine::ComputeTargets(
    float peak_position,
    float colour,
    float spread,
    float width_scale,
    int32_t increment_msb,
    int32_t* target) {
  const int32_t p0 = ToBraidsParameter(peak_position);
  const int32_t p1 = ToBraidsParameter(colour);

  const int32_t peak = (kHarmonicsNumPartials * p0) >> 7;
  int32_t second_peak = (peak >> 1) +
      kHarmonicsNumPartials * (kHarmonicsPositionScale / 2) +
      static_cast<int32_t>(spread * static_cast<float>(kHarmonicsPositionScale));
  const int32_t second_peak_amount = p1 * p1 >> 15;

  const int32_t sqrtsqrt_width = p1 < 16384 ? p1 >> 6 : 511 - (p1 >> 6);
  const int32_t sqrt_width = sqrtsqrt_width * sqrtsqrt_width >> 10;
  int32_t width = sqrt_width * sqrt_width + 4;
  width = static_cast<int32_t>(static_cast<float>(width) * width_scale);
  if (width < 1) {
    width = 1;
  }

  int32_t total = 0;
  for (int i = 0; i < kHarmonicsNumPartials; ++i) {
    const int32_t x = i * kHarmonicsPositionScale;
    int32_t d = x - peak;
    int32_t g = 32768 * 128 / (128 + d * d / width);

    d = x - second_peak;
    g += second_peak_amount * 128 / (128 + d * d / width);
    total += g;
    target[i] = g;
  }

  // total >= 1 always: the first bump contributes 32768 at its own position
  // and its Lorentzian skirt never reaches zero over a range this short, so
  // the division is safe. Braids does not guard it either.
  const int32_t attenuation = 2147483647 / total;
  for (int i = 0; i < kHarmonicsNumPartials; ++i) {
    if (increment_msb * (i + 1) > kHarmonicsGuardThreshold) {
      target[i] = 0;
    } else {
      target[i] = target[i] * attenuation >> 16;
    }
  }
}

void HarmonicsEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  // Braids resets phase on a sync pulse (digital_oscillator.cc:967-970).
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
  }

  const bool stereo = PLAITS_STEREO_HARMONICS && parameters.stereo;

  const float frequency = NoteToFrequency(parameters.note);

  // The guard at :956 tests the TOP 16 BITS of the increment, so the port
  // takes the same floor rather than the exact ratio.
  const int32_t increment_msb = static_cast<int32_t>(
      frequency * kHarmonicsIncrementScale);

  // OUT takes the played Peak; mono AUX takes it reflected, so the pair
  // carries opposite ends of the series and coincides at Peak 0.5. In stereo
  // the two sides take small opposing offsets instead.
  float peak_main = parameters.timbre;
  float peak_aux = 1.0f - parameters.timbre;
  if (stereo) {
    peak_main = parameters.timbre - kHarmonicsStereoPeak;
    peak_aux = parameters.timbre + kHarmonicsStereoPeak;
    CONSTRAIN(peak_main, 0.0f, 1.0f);
    CONSTRAIN(peak_aux, 0.0f, 1.0f);
  }

  const float spread = (parameters.morph - 0.5f) * 2.0f * kHarmonicsMaxSpread;
  const float width_scale = ApplyMacro(
      1.0f,
      kHarmonicsMinWidthScale,
      kHarmonicsMaxWidthScale,
      parameters.macro);

  int32_t target[kHarmonicsNumPartials];
  int32_t target_aux[kHarmonicsNumPartials];
  ComputeTargets(
      peak_main, parameters.harmonics, spread, width_scale, increment_msb,
      &target[0]);
  ComputeTargets(
      peak_aux, parameters.harmonics, spread, width_scale, increment_msb,
      &target_aux[0]);

  ParameterInterpolator fm(&frequency_, frequency, size);

  while (size--) {
    const float f = fm.Next();
    phase_ += f;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }

    float sum = 0.0f;
    float sum_aux = 0.0f;

    // All twelve oscillators are integer harmonics of the same phase. One
    // cosine lookup and the Chebyshev recurrence
    //
    //   cos((n + 1)x) = 2 cos(x) cos(nx) - cos((n - 1)x)
    //
    // therefore produces the same -cos convention as
    // BraidsSine(phase_ * (i + 1)) without twelve independent wrapped table
    // reads. Resetting the recurrence from the fundamental on every sample
    // prevents error from accumulating over time.
    const float cosine_1 = Sine(phase_ + 0.25f);
    const float two_cosine_1 = 2.0f * cosine_1;
    float cosine_previous = 1.0f;
    float cosine = cosine_1;
    for (int i = 0; i < kHarmonicsNumPartials; ++i) {
      const float partial = -cosine;
      sum += partial * static_cast<float>(amplitude_[i]);
      sum_aux += partial * static_cast<float>(amplitude_aux_[i]);

      // digital_oscillator.cc:974, and the shift is the point: >> on a
      // negative int32 floors -- arithmetic on every toolchain this builds
      // with, and what Braids' own ARM codegen does -- so a positive
      // difference under 256 becomes zero and the partial stops short of, or
      // never leaves, zero. Writing this as a float one-pole would silently
      // remove the gate the model is partly made of.
      amplitude_[i] += (target[i] - amplitude_[i]) >> 8;
      amplitude_aux_[i] += (target_aux[i] - amplitude_aux_[i]) >> 8;

      const float cosine_next =
          two_cosine_1 * cosine - cosine_previous;
      cosine_previous = cosine;
      cosine = cosine_next;
    }

    // The amplitudes sum to at most 32768 by construction (:954-959), so this
    // is already bounded; the CONSTRAIN stands in for Braids' CLIP.
    const float scale = kHarmonicsSineAmplitude / 32768.0f;
    float value = sum * scale;
    float value_aux = sum_aux * scale;
    CONSTRAIN(value, -1.0f, 1.0f);
    CONSTRAIN(value_aux, -1.0f, 1.0f);

    *out++ = value;
    *aux++ = value_aux;
  }
}

}  // namespace plaits
