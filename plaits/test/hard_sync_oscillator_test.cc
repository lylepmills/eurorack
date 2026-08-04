// Copyright 2026 Rubato Audio.

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "plaits/dsp/oscillator/variable_saw_oscillator.h"
#include "plaits/dsp/oscillator/variable_shape_oscillator.h"

namespace {

const size_t kBlockSize = 12;
const size_t kSyncSample = 5;

bool CheckPrefixAndReset(const float* free_run, const float* synced) {
  for (size_t i = 0; i < kSyncSample; ++i) {
    if (fabsf(free_run[i] - synced[i]) > 1.0e-7f) {
      return false;
    }
  }

  for (size_t i = kSyncSample; i < kBlockSize; ++i) {
    if (fabsf(free_run[i] - synced[i]) > 1.0e-4f) {
      return true;
    }
  }
  return false;
}

bool CheckEqual(const float* a, const float* b, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    if (fabsf(a[i] - b[i]) > 1.0e-7f) {
      return false;
    }
  }
  return true;
}

bool TestVariableShapeZeroMask() {
  plaits::VariableShapeOscillator default_oscillator;
  plaits::VariableShapeOscillator runtime_mask_oscillator;
  default_oscillator.Init();
  runtime_mask_oscillator.Init();

  float expected[64];
  float actual[64];
  volatile uint32_t runtime_zero = 0;
  default_oscillator.Render(0.21f, 0.43f, 0.31f, expected, 64);
  runtime_mask_oscillator.Render(
      0.21f, 0.43f, 0.31f, actual, 64, runtime_zero);
  return CheckEqual(expected, actual, 64);
}

bool TestVariableSawZeroMask() {
  plaits::VariableSawOscillator default_oscillator;
  plaits::VariableSawOscillator runtime_mask_oscillator;
  default_oscillator.Init();
  runtime_mask_oscillator.Init();

  float expected[64];
  float actual[64];
  volatile uint32_t runtime_zero = 0;
  default_oscillator.Render(0.21f, 0.47f, 0.62f, expected, 64);
  runtime_mask_oscillator.Render(
      0.21f, 0.47f, 0.62f, actual, 64, runtime_zero);
  return CheckEqual(expected, actual, 64);
}

bool TestVariableShapeSyncPlacement() {
  plaits::VariableShapeOscillator free_oscillator;
  plaits::VariableShapeOscillator sync_oscillator;
  free_oscillator.Init();
  sync_oscillator.Init();

  float warm_free[37];
  float warm_sync[37];
  free_oscillator.Render(0.013f, 0.43f, 0.31f, warm_free, 37);
  sync_oscillator.Render(0.013f, 0.43f, 0.31f, warm_sync, 37);

  float free_run[kBlockSize];
  float synced[kBlockSize];
  free_oscillator.Render(0.013f, 0.43f, 0.31f, free_run, kBlockSize);
  sync_oscillator.Render(
      0.013f,
      0.43f,
      0.31f,
      synced,
      kBlockSize,
      static_cast<uint32_t>(1u << kSyncSample));
  return CheckPrefixAndReset(free_run, synced);
}

bool TestVariableSawSyncPlacement() {
  plaits::VariableSawOscillator free_oscillator;
  plaits::VariableSawOscillator sync_oscillator;
  free_oscillator.Init();
  sync_oscillator.Init();

  float warm_free[37];
  float warm_sync[37];
  free_oscillator.Render(0.017f, 0.47f, 0.62f, warm_free, 37);
  sync_oscillator.Render(0.017f, 0.47f, 0.62f, warm_sync, 37);

  float free_run[kBlockSize];
  float synced[kBlockSize];
  free_oscillator.Render(0.017f, 0.47f, 0.62f, free_run, kBlockSize);
  sync_oscillator.Render(
      0.017f,
      0.47f,
      0.62f,
      synced,
      kBlockSize,
      static_cast<uint32_t>(1u << kSyncSample));
  return CheckPrefixAndReset(free_run, synced);
}

}  // namespace

int main() {
  if (!TestVariableShapeZeroMask()) {
    fprintf(stderr, "variable-shape zero mask changed free-run output\n");
    return 1;
  }
  if (!TestVariableSawZeroMask()) {
    fprintf(stderr, "variable-saw zero mask changed free-run output\n");
    return 1;
  }
  if (!TestVariableShapeSyncPlacement()) {
    fprintf(stderr, "variable-shape sync was not applied at sample 5\n");
    return 1;
  }
  if (!TestVariableSawSyncPlacement()) {
    fprintf(stderr, "variable-saw sync was not applied at sample 5\n");
    return 1;
  }
  printf("hard_sync_oscillator_test: all checks passed\n");
  return 0;
}
