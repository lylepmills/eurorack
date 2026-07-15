// Copyright 2026 Lyle Mills.
//
// Glisson/granular chirp synthesis engine.

#include "plaits/dsp/engine2/glisson_engine.h"

#include <algorithm>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void GlissonEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void GlissonEngine::Reset() {
  for (int i = 0; i < kNumGlissonGrains; ++i) {
    grain_[i].phase = 0.0f;
    grain_[i].aux_phase = 0.0f;
    grain_[i].envelope_phase = 1.0f;
    grain_[i].envelope_increment = 0.001f;
    grain_[i].start_ratio = 1.0f;
    grain_[i].end_ratio = 1.0f;
  }
  num_grains_ = 0;
  reset_pending_ = true;
}

void GlissonEngine::StartGrain(
    Grain* grain,
    float scatter,
    float direction,
    float duration,
    float delay) {
  const float random_pitch = 2.0f * Random::GetFloat() - 1.0f;
  const float centre = random_pitch * scatter;
  const float excursion = direction * (3.0f + 0.65f * scatter);

  grain->start_ratio = SemitonesToRatio(centre - 0.5f * excursion);
  grain->end_ratio = SemitonesToRatio(centre + 0.5f * excursion);
  grain->envelope_phase = -delay;
  grain->envelope_increment = 1.0f / duration;
}

void GlissonEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);

  const float f0 = NoteToFrequency(parameters.note);
  const float scatter = 2.0f + 34.0f * parameters.harmonics * \
      parameters.harmonics;
  const float direction = 2.0f * parameters.morph - 1.0f;
  const float duration_seconds = 0.012f * SemitonesToRatio(
      parameters.macro * 60.0f);
  const float duration = duration_seconds * kCorrectedSampleRate;
  const int num_grains = 1 + static_cast<int>(
      parameters.timbre * static_cast<float>(kNumGlissonGrains - 1) + 0.5f);

  if (reset_pending_ || (parameters.trigger & TRIGGER_RISING_EDGE)) {
    for (int i = 0; i < kNumGlissonGrains; ++i) {
      StartGrain(
          &grain_[i],
          scatter,
          direction,
          duration,
          static_cast<float>(i) / static_cast<float>(num_grains));
    }
    reset_pending_ = false;
  } else if (num_grains > num_grains_) {
    for (int i = num_grains_; i < num_grains; ++i) {
      StartGrain(&grain_[i], scatter, direction, duration, 0.0f);
    }
  }
  num_grains_ = num_grains;

  const float gain = 0.8f / static_cast<float>(num_grains);
  for (int i = 0; i < num_grains; ++i) {
    Grain* g = &grain_[i];
    for (size_t j = 0; j < size; ++j) {
      g->envelope_phase += g->envelope_increment;
      if (g->envelope_phase >= 1.0f) {
        StartGrain(g, scatter, direction, duration, 0.0f);
      }
      if (g->envelope_phase < 0.0f) {
        continue;
      }

      float t = g->envelope_phase;
      // The fourth macro also bends the trajectory: short grains are nearly
      // linear, long grains linger at their endpoints.
      const float curved = t * t * (3.0f - 2.0f * t);
      t += (curved - t) * parameters.macro;
      const float ratio = g->start_ratio + \
          (g->end_ratio - g->start_ratio) * t;
      const float reverse_ratio = g->end_ratio + \
          (g->start_ratio - g->end_ratio) * t;

      g->phase += min(0.24f, f0 * ratio);
      g->phase -= static_cast<int>(g->phase);
      g->aux_phase += min(0.24f, f0 * reverse_ratio);
      g->aux_phase -= static_cast<int>(g->aux_phase);

      const float envelope = 0.5f - 0.5f * Sine(
          g->envelope_phase + 0.25f);
      out[j] += Sine(g->phase) * envelope * gain;
      aux[j] += Sine(g->aux_phase) * envelope * gain;
    }
  }
}

}  // namespace plaits
