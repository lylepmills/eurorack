// Copyright 2026 Rubato Audio.

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "plaits/build_config.h"
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"
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

class DispatchProbeEngine : public plaits::Engine {
 public:
  explicit DispatchProbeEngine(bool native)
      : native_(native), render_count_(0), sync_count_(0) { }

  void Init(stmlib::BufferAllocator* allocator) { (void) allocator; }
  void Reset() { }
  void LoadUserData(const uint8_t* user_data) { (void) user_data; }
  void Render(
      const plaits::EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped) {
    sizes_[render_count_] = size;
    triggers_[render_count_] = parameters.trigger;
    masks_[render_count_] = parameters.hard_sync;
    for (size_t i = 0; i < size; ++i) {
      out[i] = static_cast<float>(render_count_ + 1);
      aux[i] = -out[i];
    }
    ++render_count_;
    *already_enveloped = false;
  }
  bool hard_sync_capable() const { return native_; }
  void HardSync() { ++sync_count_; }

  bool native_;
  int render_count_;
  int sync_count_;
  size_t sizes_[3];
  int triggers_[3];
  uint32_t masks_[3];
};

bool TestHardSyncDispatch() {
  plaits::EngineParameters parameters = TestParameters();
  parameters.trigger = plaits::TRIGGER_UNPATCHED;
  parameters.hard_sync = (1u << 5) | (1u << 9);
  float out[kBlockSize];
  float aux[kBlockSize];
  bool already_enveloped = false;

  DispatchProbeEngine fallback(false);
  plaits::RenderEngineWithHardSync(
      &fallback, parameters, out, aux, kBlockSize, &already_enveloped);
  if (fallback.render_count_ != 2 || fallback.sync_count_ != 1 ||
      fallback.sizes_[0] != 5 || fallback.sizes_[1] != 19 ||
      fallback.triggers_[0] != plaits::TRIGGER_UNPATCHED ||
      fallback.triggers_[1] != plaits::TRIGGER_RISING_EDGE ||
      fallback.masks_[0] || fallback.masks_[1] ||
      out[4] != 1.0f || out[5] != 2.0f) {
    fprintf(stderr, "fallback sync dispatch did not split at first edge\n");
    return false;
  }

  DispatchProbeEngine native(true);
  plaits::RenderEngineWithHardSync(
      &native, parameters, out, aux, kBlockSize, &already_enveloped);
  if (native.render_count_ != 1 || native.sync_count_ != 0 ||
      native.sizes_[0] != kBlockSize ||
      native.masks_[0] != parameters.hard_sync ||
      native.triggers_[0] != plaits::TRIGGER_UNPATCHED) {
    fprintf(stderr, "native sync dispatch did not preserve full mask\n");
    return false;
  }
  return true;
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
  if (!TestHardSyncDispatch() ||
      !TestResetPlacement<plaits::WaveshapingEngine>("waveshaping") ||
      !TestResetPlacement<plaits::FMEngine>("two-op FM") ||
      !TestResetPlacement<plaits::WavetableEngine>("wavetable") ||
      !TestResetPlacement<plaits::VirtualAnalogVCFEngine>("VA+VCF") ||
      !TestResetPlacement<plaits::GrainEngine>("granular formant") ||
      !TestResetPlacement<plaits::AdditiveEngine>("harmonic") ||
      !TestResetPlacement<plaits::SwarmEngine>("swarm") ||
      !TestResetPlacement<plaits::WaveTerrainEngine>("wave terrain")) {
    return 1;
  }
  printf("hard_sync_engine_test: all checks passed\n");
  return 0;
}
