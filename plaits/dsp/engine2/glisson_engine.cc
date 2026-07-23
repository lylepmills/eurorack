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

namespace {

// Keep chirps below the least useful top octave. This intentionally uses a
// cheap clamp: the engine calls it twice per grain per sample on a 72 MHz MCU.
float LimitFrequency(float frequency) {
  return min(0.22f, frequency);
}

}  // namespace

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
    StereoPanGains(
        0.5f, &grain_[i].start_gain_left, &grain_[i].start_gain_right);
    grain_[i].end_gain_left = grain_[i].start_gain_left;
    grain_[i].end_gain_right = grain_[i].start_gain_right;
  }
  num_grains_ = 0;
  reset_pending_ = true;
}

void GlissonEngine::StartGrain(
    Grain* grain,
    float scatter,
    float direction,
    float duration,
    float delay,
    bool stereo) {
  const float random_pitch = 2.0f * Random::GetFloat() - 1.0f;
  const float centre = random_pitch * scatter;
  const float excursion = direction * (3.0f + 0.65f * scatter);

  grain->start_ratio = SemitonesToRatio(centre - 0.5f * excursion);
  grain->end_ratio = SemitonesToRatio(centre + 0.5f * excursion);
  grain->envelope_phase = -delay;
  grain->envelope_increment = 1.0f / duration;

  // Drawn only in stereo, so that the mono render consumes an unchanged
  // random sequence.
  if (stereo) {
    // Independent, arcsine-distributed endpoints: uniform draws average too
    // close to the centre to read as wide, and a mirrored trajectory would
    // cross the centre exactly when the parabolic grain envelope peaks.
    const float start_pan = 0.5f - \
        0.5f * Sine(0.25f + 0.5f * Random::GetFloat());
    const float end_pan = 0.5f - \
        0.5f * Sine(0.25f + 0.5f * Random::GetFloat());
    StereoPanGains(
        start_pan, &grain->start_gain_left, &grain->start_gain_right);
    StereoPanGains(
        end_pan, &grain->end_gain_left, &grain->end_gain_right);
  }
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
          static_cast<float>(i) / static_cast<float>(num_grains),
          parameters.stereo);
    }
    reset_pending_ = false;
  } else if (num_grains > num_grains_) {
    for (int i = num_grains_; i < num_grains; ++i) {
      StartGrain(
          &grain_[i], scatter, direction, duration, 0.0f, parameters.stereo);
    }
  }
  num_grains_ = num_grains;

  const float gain = 0.72f / static_cast<float>(num_grains);
  if (parameters.stereo) {
    for (int i = 0; i < num_grains; ++i) {
      Grain* g = &grain_[i];
      for (size_t j = 0; j < size; ++j) {
        g->envelope_phase += g->envelope_increment;
        if (g->envelope_phase >= 1.0f) {
          StartGrain(g, scatter, direction, duration, 0.0f, true);
        }
        if (g->envelope_phase < 0.0f) {
          continue;
        }

        float t = g->envelope_phase;
        const float curved = t * t * (3.0f - 2.0f * t);
        t += (curved - t) * parameters.macro;
        const float ratio = g->start_ratio + \
            (g->end_ratio - g->start_ratio) * t;

        g->phase += LimitFrequency(f0 * ratio);
        g->phase -= static_cast<int>(g->phase);

        const float envelope = 4.0f * g->envelope_phase * \
            (1.0f - g->envelope_phase);
        const float sample = SineNoWrap(g->phase) * envelope * gain;
        // Only the forward chirp is rendered, panned along its trajectory
        // with the same bent t as the pitch glide.
        out[j] += sample * (g->start_gain_left + \
            (g->end_gain_left - g->start_gain_left) * t);
        aux[j] += sample * (g->start_gain_right + \
            (g->end_gain_right - g->start_gain_right) * t);
      }
    }
  } else {
    for (int i = 0; i < num_grains; ++i) {
      Grain* g = &grain_[i];
      for (size_t j = 0; j < size; ++j) {
        g->envelope_phase += g->envelope_increment;
        if (g->envelope_phase >= 1.0f) {
          StartGrain(g, scatter, direction, duration, 0.0f, false);
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

        g->phase += LimitFrequency(f0 * ratio);
        g->phase -= static_cast<int>(g->phase);
        g->aux_phase += LimitFrequency(f0 * reverse_ratio);
        g->aux_phase -= static_cast<int>(g->aux_phase);

        // A parabolic grain window avoids a third interpolated sine lookup
        // for every grain and sample. The remaining oscillator phases are
        // already wrapped, so the cheaper no-wrap lookup is safe.
        const float envelope = 4.0f * g->envelope_phase * \
            (1.0f - g->envelope_phase);
        out[j] += SineNoWrap(g->phase) * envelope * gain;
        aux[j] += SineNoWrap(g->aux_phase) * envelope * gain;
      }
    }
  }
}

}  // namespace plaits
