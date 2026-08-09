// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Additive synthesis with 24+8 partials.
//
// OUT: 24 integer harmonics. AUX: 8 harmonics of the organ subset.
// alt firmware, stereo mode: OUT carries the 24-harmonic voice and AUX carries
// a short all-pass phase rotation of it. This keeps the spectrum and level
// matched while avoiding a second 24-partial accumulation.

#include "plaits/dsp/engine/additive_engine.h"

#include <algorithm>

#include "stmlib/dsp/cosine_oscillator.h"
#include "plaits/build_config.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

#if PLAITS_BUILD_ENABLE_SYNC_INPUT
#define PLAITS_HARD_SYNC_EVENTS(parameters) ((parameters).hard_sync)
#else
#define PLAITS_HARD_SYNC_EVENTS(parameters) 0u
#endif

namespace plaits {

using namespace std;
using namespace stmlib;

void AdditiveEngine::Init(BufferAllocator* allocator) {
  amplitudes_ = allocator->Allocate<float>(kNumHarmonics);
  for (int i = 0; i < kNumHarmonicOscillators; ++i) {
    harmonic_oscillator_[i].Init();
  }
  stereo_allpass_.Init();
}

void AdditiveEngine::Reset() {
  fill(
      &amplitudes_[0],
      &amplitudes_[kNumHarmonics],
      0.0f);
}

void AdditiveEngine::UpdateAmplitudes(
    float centroid,
    float slope,
    float bumps,
    float* amplitudes,
    const int* harmonic_indices,
    size_t num_harmonics) {
  const float n = (static_cast<float>(num_harmonics) - 1.0f);
  const float margin = (1.0f / slope - 1.0f) / (1.0f + bumps);
  const float center = centroid * (n + margin) - 0.5f * margin;

  float sum = 0.001f;

  for (size_t i = 0; i < num_harmonics; ++i) {
    float order = fabsf(static_cast<float>(i) - center) * slope;
    float gain = 1.0f - order;
    gain += fabsf(gain);
    gain *= gain;

    float b = 0.25f + order * bumps;
    float bump_factor = 1.0f + Sine(b);

    gain *= bump_factor;
    gain *= gain;
    gain *= gain;
    
    int j = harmonic_indices[i];
    
    // Warning about the following line: this is not a proper LP filter because
    // of the normalization. But in spite of its strange working, this line
    // turns out to be absolutely essential.
    //
    // I have tried both normalizing the LP-ed spectrum, and LP-ing the
    // normalized spectrum, and both of them cause more annoyances than this
    // "incorrect" solution.
    
    ONE_POLE(amplitudes[j], gain, 0.001f);
    sum += amplitudes[j];
  }

  sum = 1.0f / sum;

  for (size_t i = 0; i < num_harmonics; ++i) {
    amplitudes[harmonic_indices[i]] *= sum;
  }
}

inline float Bump(float x, float centroid, float slope) {
  float d = fabsf(x - centroid);
  float bump = 1.0f - d * slope;
  return bump + fabsf(bump);
}

const int integer_harmonics[24] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23
};

const int organ_harmonics[8] = {
  0, 1, 2, 3, 5, 7, 9, 11
};

void AdditiveEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);

  const float centroid = parameters.timbre;
  const float raw_bumps = parameters.harmonics;
  const float raw_slope = (1.0f - 0.6f * raw_bumps) * parameters.morph;
  const float slope = 0.01f + 1.99f * raw_slope * raw_slope * raw_slope;
  const float bumps = 16.0f * raw_bumps * raw_bumps;
  UpdateAmplitudes(
      centroid,
      slope,
      bumps,
      &amplitudes_[0],
      integer_harmonics,
      24);
  const float odd_gain = ApplyMacro(
      1.0f, 1.5f, 0.5f, parameters.macro);
  const float even_gain = ApplyMacro(
      1.0f, 0.5f, 1.5f, parameters.macro);
  for (int i = 0; i < 24; ++i) {
    amplitudes_[i] *= (i & 1) ? even_gain : odd_gain;
  }

  const uint32_t hard_sync = PLAITS_HARD_SYNC_EVENTS(parameters);

#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  const float* frequency_offset = parameters.frequency_offset;
  if (frequency_offset) {
    if ((PLAITS_STEREO_HARMONIC && parameters.stereo)) {
      harmonic_oscillator_[0].RenderLinearFm<1>(
          f0, frequency_offset, &amplitudes_[0], out, size, hard_sync);
      harmonic_oscillator_[1].RenderLinearFm<13>(
          f0, frequency_offset, &amplitudes_[12], out, size, hard_sync);
      for (size_t i = 0; i < size; ++i) {
        aux[i] = stereo_allpass_.Process(out[i]);
      }
      return;
    }

    harmonic_oscillator_[0].RenderLinearFm<1>(
        f0, frequency_offset, &amplitudes_[0], out, size, hard_sync);
    harmonic_oscillator_[1].RenderLinearFm<13>(
        f0, frequency_offset, &amplitudes_[12], out, size, hard_sync);
    UpdateAmplitudes(
        centroid,
        slope,
        bumps,
        &amplitudes_[24],
        organ_harmonics,
        8);
    harmonic_oscillator_[2].RenderLinearFm<1>(
        f0, frequency_offset, &amplitudes_[24], aux, size, hard_sync);
    return;
  }
#endif

  if ((PLAITS_STEREO_HARMONIC && parameters.stereo)) {
    harmonic_oscillator_[0].Render<1>(
        f0, &amplitudes_[0], out, size, hard_sync);
    harmonic_oscillator_[1].Render<13>(
        f0, &amplitudes_[12], out, size, hard_sync);
    for (size_t i = 0; i < size; ++i) {
      aux[i] = stereo_allpass_.Process(out[i]);
    }
    return;
  }

  harmonic_oscillator_[0].Render<1>(
      f0, &amplitudes_[0], out, size, hard_sync);
  harmonic_oscillator_[1].Render<13>(
      f0, &amplitudes_[12], out, size, hard_sync);

  UpdateAmplitudes(
      centroid,
      slope,
      bumps,
      &amplitudes_[24],
      organ_harmonics,
      8);

  harmonic_oscillator_[2].Render<1>(
      f0, &amplitudes_[24], aux, size, hard_sync);
}

}  // namespace plaits

#undef PLAITS_HARD_SYNC_EVENTS
