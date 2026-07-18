// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "plaits/dsp/dsp.h"
#include "stmlib/test/wav_writer.h"

#ifndef PLAITS_LAB_ENGINE_HEADER
#error PLAITS_LAB_ENGINE_HEADER must name the package engine header
#endif
#ifndef PLAITS_LAB_ENGINE_CLASS
#error PLAITS_LAB_ENGINE_CLASS must name the package engine class
#endif

#include PLAITS_LAB_ENGINE_HEADER

namespace {

const size_t kPreviewBlockSize = plaits::kBlockSize;
char allocator_memory[16 * 1024];

float Parse(const char* value) {
  return static_cast<float>(std::atof(value));
}

float Interpolate(float start, float end, float position) {
  return start + (end - start) * position;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 15) {
    std::fprintf(stderr, "renderer received %d arguments; expected 14\n", argc - 1);
    return 2;
  }

  const char* output_path = argv[1];
  const int duration_seconds = std::atoi(argv[2]);
  const float note = Parse(argv[3]);
  const float harmonics_start = Parse(argv[4]);
  const float harmonics_end = Parse(argv[5]);
  const float timbre_start = Parse(argv[6]);
  const float timbre_end = Parse(argv[7]);
  const float morph_start = Parse(argv[8]);
  const float morph_end = Parse(argv[9]);
  const float macro_start = Parse(argv[10]);
  const float macro_end = Parse(argv[11]);
  const float trigger_hz = Parse(argv[12]);
  const float out_gain = Parse(argv[13]);
  const float aux_gain = Parse(argv[14]);

  if (duration_seconds < 1 || duration_seconds > 30) {
    std::fprintf(stderr, "duration must be between 1 and 30 seconds\n");
    return 2;
  }

  stmlib::BufferAllocator allocator(allocator_memory, sizeof(allocator_memory));
  PLAITS_LAB_ENGINE_CLASS engine;
  engine.Init(&allocator);
  engine.LoadUserData(NULL);
  engine.Reset();

  stmlib::WavWriter writer(2, static_cast<size_t>(plaits::kSampleRate), duration_seconds);
  if (!writer.Open(output_path)) {
    std::perror(output_path);
    return 2;
  }

  plaits::EngineParameters parameters;
  parameters.note = note;
  parameters.accent = 0.8f;
  parameters.chord_set_option = 0;

  const size_t total_frames = static_cast<size_t>(duration_seconds * plaits::kSampleRate);
  const size_t trigger_period = trigger_hz > 0.0f
      ? static_cast<size_t>(plaits::kSampleRate / trigger_hz)
      : 0;
  float peak = 0.0f;

  for (size_t frame = 0; frame < total_frames; frame += kPreviewBlockSize) {
    const float position = static_cast<float>(frame) / static_cast<float>(total_frames - 1);
    parameters.harmonics = Interpolate(harmonics_start, harmonics_end, position);
    parameters.timbre = Interpolate(timbre_start, timbre_end, position);
    parameters.morph = Interpolate(morph_start, morph_end, position);
    parameters.macro = Interpolate(macro_start, macro_end, position);

    if (!trigger_period) {
      parameters.trigger = plaits::TRIGGER_UNPATCHED;
    } else {
      const size_t phase = frame % trigger_period;
      parameters.trigger = phase < trigger_period / 4 ? plaits::TRIGGER_HIGH : plaits::TRIGGER_LOW;
      if (phase < kPreviewBlockSize) {
        parameters.trigger |= plaits::TRIGGER_RISING_EDGE;
      }
    }

    float out[kPreviewBlockSize];
    float aux[kPreviewBlockSize];
    bool already_enveloped = false;
    engine.Render(parameters, out, aux, kPreviewBlockSize, &already_enveloped);
    for (size_t i = 0; i < kPreviewBlockSize; ++i) {
      if (!std::isfinite(out[i]) || !std::isfinite(aux[i])) {
        std::fprintf(stderr, "non-finite output at frame %zu\n", frame + i);
        return 3;
      }
      out[i] *= std::fabs(out_gain);
      aux[i] *= std::fabs(aux_gain);
      peak = std::fmax(peak, std::fmax(std::fabs(out[i]), std::fabs(aux[i])));
    }
    writer.Write(out, aux, kPreviewBlockSize);
  }

  std::printf("rendered %s (%d s, peak %.4f)\n", output_path, duration_seconds, peak);
  return 0;
}
