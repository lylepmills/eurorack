// Copyright 2026 Rubato Audio.

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "plaits/build_config.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "stmlib/utils/buffer_allocator.h"

namespace {

const size_t kBlockSize = 24;
const size_t kSyncSample = 5;

plaits::EngineParameters TestParameters() {
  plaits::EngineParameters parameters;
  parameters.trigger = plaits::TRIGGER_LOW;
  parameters.note = 52.0f;
  parameters.timbre = 0.61f;
  parameters.morph = 0.37f;
  parameters.harmonics = 0.72f;
  parameters.accent = 0.8f;
  parameters.macro = 0.5f;
  parameters.articulation_envelope = 0.0f;
  parameters.articulation_envelope_active = false;
  parameters.chord_set_option = 0;
  parameters.hard_sync = 0;
  parameters.stereo = false;
  return parameters;
}

bool Equal(float a, float b) {
  return fabsf(a - b) <= 1.0e-7f;
}

template<typename EngineType>
bool TestResetPlacement(const char* name) {
  EngineType free_engine;
  EngineType sync_engine;
  uint32_t free_memory[4096];
  uint32_t sync_memory[4096];
  stmlib::BufferAllocator free_allocator(free_memory, sizeof(free_memory));
  stmlib::BufferAllocator sync_allocator(sync_memory, sizeof(sync_memory));
  free_engine.Init(&free_allocator);
  sync_engine.Init(&sync_allocator);
  free_engine.Reset();
  sync_engine.Reset();
  free_engine.LoadUserData(NULL);
  sync_engine.LoadUserData(NULL);

  if (!free_engine.hard_sync_capable() || !sync_engine.hard_sync_capable()) {
    fprintf(stderr, "%s did not opt in to hard sync\n", name);
    return false;
  }

  plaits::EngineParameters parameters = TestParameters();
  float free_out[kBlockSize];
  float free_aux[kBlockSize];
  float sync_out[kBlockSize];
  float sync_aux[kBlockSize];
  bool already_enveloped = false;

  // Advance both instances to the same non-zero oscillator state.
  free_engine.Render(
      parameters, free_out, free_aux, kBlockSize, &already_enveloped);
  sync_engine.Render(
      parameters, sync_out, sync_aux, kBlockSize, &already_enveloped);

  free_engine.Render(
      parameters, free_out, free_aux, kBlockSize, &already_enveloped);
  parameters.hard_sync = static_cast<uint32_t>(1u << kSyncSample);
  sync_engine.Render(
      parameters, sync_out, sync_aux, kBlockSize, &already_enveloped);

  for (size_t i = 0; i < kSyncSample; ++i) {
    if (!Equal(free_out[i], sync_out[i]) ||
        !Equal(free_aux[i], sync_aux[i])) {
      fprintf(stderr, "%s changed before sync sample %lu\n",
          name, static_cast<unsigned long>(i));
      return false;
    }
  }

  for (size_t i = kSyncSample; i < kBlockSize; ++i) {
    if (!Equal(free_out[i], sync_out[i]) ||
        !Equal(free_aux[i], sync_aux[i])) {
      return true;
    }
  }
  fprintf(stderr, "%s did not react to sync at sample %lu\n",
      name, static_cast<unsigned long>(kSyncSample));
  return false;
}

}  // namespace

int main() {
#if !PLAITS_BUILD_ENABLE_SYNC_INPUT
  fprintf(stderr, "compile with -DPLAITS_BUILD_ENABLE_SYNC_INPUT=1\n");
  return 1;
#endif
  if (!TestResetPlacement<plaits::WaveshapingEngine>("waveshaping") ||
      !TestResetPlacement<plaits::FMEngine>("two-op FM") ||
      !TestResetPlacement<plaits::WavetableEngine>("wavetable") ||
      !TestResetPlacement<plaits::VirtualAnalogVCFEngine>("VA+VCF")) {
    return 1;
  }
  printf("hard_sync_engine_test: all checks passed\n");
  return 0;
}
