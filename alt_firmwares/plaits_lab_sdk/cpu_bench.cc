// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// CPU cost harness for the Plaits Lab SDK's reference-ratio check.
//
// Renders one engine flat out and prints the nanoseconds it spends per output
// sample. The number is only meaningful RELATIVE to the same harness run over a
// stock engine on the same machine: the host is far faster than the module's
// 72 MHz Cortex-M4, so an absolute host timing (or a "fraction of real time"
// figure) says nothing about whether an engine fits on hardware. Comparing two
// engines built with identical flags cancels the machine out, and stock engines
// are known to fit — so the ratio is the signal. See cpu_reference_ratio() in
// plaits_lab.py.
//
// The engine under test is selected by PLAITS_LAB_ENGINE_HEADER and
// PLAITS_LAB_ENGINE_CLASS, exactly as render_model.cc and wasm_audition.cc do.

#include <cstdio>
#include <cstdlib>
#include <chrono>

#include "stmlib/utils/buffer_allocator.h"
#include "plaits/dsp/engine/engine.h"
#include PLAITS_LAB_ENGINE_HEADER

using namespace plaits;

// Generous next to the module's real per-engine slice; this harness measures
// CPU cost, not memory footprint, and an engine that overruns it would fail the
// hardware build long before this check runs.
static char g_memory[65536];

int main(int argc, char** argv) {
  int blocks = argc > 1 ? atoi(argv[1]) : 200000;
  if (blocks < 1000) {
    blocks = 1000;
  }

  stmlib::BufferAllocator allocator(g_memory, sizeof(g_memory));
  static PLAITS_LAB_ENGINE_CLASS engine;
  engine.Init(&allocator);

  // Mid-position controls: the cost of an engine that branches on its knobs
  // should be sampled somewhere representative rather than at an extreme.
  EngineParameters parameters;
  parameters.trigger = 0;
  parameters.note = 48.0f;
  parameters.timbre = 0.5f;
  parameters.morph = 0.5f;
  parameters.harmonics = 0.5f;
  parameters.accent = 1.0f;
  parameters.macro = 0.5f;
  parameters.chord_set_option = 0;
  parameters.stereo = false;

  float out[kBlockSize];
  float aux[kBlockSize];
  bool already_enveloped = false;

  // Warm up: let any lazily-built table and the branch predictors settle so the
  // timed run measures steady-state render cost.
  for (int i = 0; i < 200; ++i) {
    engine.Render(parameters, out, aux, kBlockSize, &already_enveloped);
  }

  const std::chrono::steady_clock::time_point started =
      std::chrono::steady_clock::now();
  for (int i = 0; i < blocks; ++i) {
    engine.Render(parameters, out, aux, kBlockSize, &already_enveloped);
  }
  const std::chrono::steady_clock::time_point finished =
      std::chrono::steady_clock::now();

  const double elapsed_ns =
      std::chrono::duration<double, std::nano>(finished - started).count();
  const double per_sample =
      elapsed_ns / (static_cast<double>(blocks) * static_cast<double>(kBlockSize));

  // Consume the buffers so the render loop cannot be optimized away.
  double guard = 0.0;
  for (size_t i = 0; i < kBlockSize; ++i) {
    guard += out[i] + aux[i];
  }
  printf("%.4f %.1f\n", per_sample, guard * 0.0);
  return 0;
}
