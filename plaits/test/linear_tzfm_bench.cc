// Host-side relative-cost benchmark for the engines that consume Plaits
// Palette's audio-rate linear TZFM buffer. Absolute host nanoseconds are not a
// Cortex-M4 budget; the modulated/baseline ratio is the useful result.

#include <chrono>
#include <cmath>
#include <cstdio>

#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine2/vowel_fof_engine.h"
#include "stmlib/utils/buffer_allocator.h"

using namespace plaits;
using namespace stmlib;

const size_t kBenchBlockSize = 24;
const int kIterations = 400000;
char ram[128 * 1024];

template<typename EngineType>
void Benchmark(const char* name) {
  BufferAllocator allocator(ram, sizeof(ram));
  EngineType engine;
  engine.Init(&allocator);
  engine.Reset();

  EngineParameters parameters;
  parameters.trigger = TRIGGER_UNPATCHED;
  parameters.note = 48.0f;
  parameters.timbre = 0.6f;
  parameters.morph = 0.4f;
  parameters.harmonics = 0.5f;
  parameters.accent = 0.8f;
  parameters.macro = 0.5f;
  parameters.chord_set_option = 0;
  parameters.stereo = false;

  float linear_fm[kBenchBlockSize];
  for (size_t i = 0; i < kBenchBlockSize; ++i) {
    // Cross zero repeatedly at an A4-ish carrier without creating an
    // unrealistically constant branch pattern.
    linear_fm[i] = 0.018f * sinf(
        6.28318530718f * static_cast<float>(i) /
        static_cast<float>(kBenchBlockSize));
  }

  float out[kBenchBlockSize];
  float aux[kBenchBlockSize];
  bool already_enveloped = false;
  for (int i = 0; i < 4000; ++i) {
    engine.Render(
        parameters, out, aux, kBenchBlockSize, &already_enveloped);
  }

  const std::chrono::high_resolution_clock::time_point baseline_start =
      std::chrono::high_resolution_clock::now();
  for (int i = 0; i < kIterations; ++i) {
    parameters.linear_fm = NULL;
    engine.Render(
        parameters, out, aux, kBenchBlockSize, &already_enveloped);
  }
  const std::chrono::high_resolution_clock::time_point modulated_start =
      std::chrono::high_resolution_clock::now();
  for (int i = 0; i < kIterations; ++i) {
    parameters.linear_fm = linear_fm;
    engine.Render(
        parameters, out, aux, kBenchBlockSize, &already_enveloped);
  }
  const std::chrono::high_resolution_clock::time_point end =
      std::chrono::high_resolution_clock::now();

  const double baseline_ns =
      std::chrono::duration<double, std::nano>(
          modulated_start - baseline_start).count() / kIterations;
  const double modulated_ns =
      std::chrono::duration<double, std::nano>(
          end - modulated_start).count() / kIterations;
  printf(
      "%-14s baseline %8.1f ns  tzfm %8.1f ns  ratio %.3f\n",
      name,
      baseline_ns,
      modulated_ns,
      modulated_ns / baseline_ns);
}

int main() {
#if defined(__SSE2__)
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
  Benchmark<WaveshapingEngine>("waveshaping");
  Benchmark<VowelFofEngine>("vowel-fof");
  return 0;
}
