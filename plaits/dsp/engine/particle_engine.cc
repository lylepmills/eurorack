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
// Filtered random pulses.
//
// OUT: particles through resonant band-pass filters, low-pass colored and
// diffused. AUX: raw random pulses.
// alt firmware, stereo mode: each particle is panned to a fixed position
// spread across the stereo field, both channels get the OUT treatment, the
// diffuser processes the mono sum and its wet signal is added equally to
// both sides, and the raw pulses are not sent to AUX.

#include "plaits/dsp/engine/particle_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

void ParticleEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kNumParticles; ++i) {
    particle_[i].Init();
  }
  diffuser_.Init(allocator->Allocate<uint16_t>(8192));
  post_filter_.Init();
  right_post_filter_.Init();
}

void ParticleEngine::Reset() {
  diffuser_.Reset();
}

// Equal-power gains for the fixed pan positions
// { 0.05, 0.85, 0.3, 0.7, 0.15, 0.95 }. Precomputing them removes twelve
// square roots per stereo audio block without changing their float values.
const float particle_pan_left[kNumParticles] = {
  0.974679410f,
  0.387298316f,
  0.836660028f,
  0.547722578f,
  0.921954453f,
  0.223606825f,
};

const float particle_pan_right[kNumParticles] = {
  0.223606795f,
  0.921954453f,
  0.547722578f,
  0.836660028f,
  0.387298346f,
  0.974679410f,
};

// The equal-power pan gain pairs sum to 1.297 on average: compensate, so
// that the diffuser receives the particles at their mono level.
const float kStereoToMonoGain = 0.7708f;

void ParticleEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);
  const float density_sqrt = NoteToFrequency(
      60.0f + parameters.timbre * parameters.timbre * 72.0f);
  const float density = density_sqrt * density_sqrt * (1.0f / kNumParticles);
  const float gain = 1.0f / density;
  const float q_sqrt = SemitonesToRatio(parameters.morph >= 0.5f
      ? (parameters.morph - 0.5f) * 120.0f
      : 0.0f);
  const float q = 0.5f + q_sqrt * q_sqrt;
  const float spread = 48.0f * parameters.harmonics * parameters.harmonics;
  const float raw_diffusion_sqrt = 2.0f * fabsf(parameters.morph - 0.5f);
  const float raw_diffusion = raw_diffusion_sqrt * raw_diffusion_sqrt;
  const float stock_diffusion = parameters.morph < 0.5f
      ? raw_diffusion
      : 0.0f;
  const float diffusion = ApplyMacro(
      stock_diffusion, 0.0f, 1.0f, parameters.macro);
  const bool sync = parameters.trigger & TRIGGER_RISING_EDGE;
  
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);

  if ((PLAITS_STEREO_PARTICLE_NOISE && parameters.stereo)) {
    for (int i = 0; i < kNumParticles; ++i) {
      particle_[i].RenderStereo(
          sync,
          density,
          gain,
          f0,
          spread,
          q,
          particle_pan_left[i],
          particle_pan_right[i],
          out,
          aux,
          size);
    }

    post_filter_.set_f_q<FREQUENCY_DIRTY>(min(f0, 0.49f), 0.5f);
    post_filter_.Process<FILTER_MODE_LOW_PASS>(out, out, size);
    right_post_filter_.set_f_q<FREQUENCY_DIRTY>(min(f0, 0.49f), 0.5f);
    right_post_filter_.Process<FILTER_MODE_LOW_PASS>(aux, aux, size);

    // The diffuser is fed with the mono sum, and processed with a wet-only
    // mix so that its output can then be crossfaded - at the mono dry/wet
    // law - into both channels.
    float mono[kMaxBlockSize];
    for (size_t i = 0; i < size; ++i) {
      mono[i] = (out[i] + aux[i]) * kStereoToMonoGain;
    }
    diffuser_.Process(1.0f, 0.5f * diffusion + 0.25f, mono, size);
    const float amount = 0.8f * diffusion * diffusion;
    for (size_t i = 0; i < size; ++i) {
      out[i] += amount * (mono[i] - out[i]);
      aux[i] += amount * (mono[i] - aux[i]);
    }
    return;
  }

  for (int i = 0; i < kNumParticles; ++i) {
    particle_[i].Render(
        sync,
        density,
        gain,
        f0,
        spread,
        q,
        out,
        aux,
        size);
  }

  post_filter_.set_f_q<FREQUENCY_DIRTY>(min(f0, 0.49f), 0.5f);
  post_filter_.Process<FILTER_MODE_LOW_PASS>(out, out, size);

  diffuser_.Process(
      0.8f * diffusion * diffusion,
      0.5f * diffusion + 0.25f,
      out,
      size);
}

}  // namespace plaits
