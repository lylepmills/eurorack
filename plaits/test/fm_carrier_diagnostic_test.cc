#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "plaits/fm_carrier_diagnostic.h"

using namespace plaits;

static void Check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
  }
}

static int RunArticulations(
    FmCarrierDiagnostic* diagnostic,
    float usage,
    int drone_level,
    int triggered_level) {
  Patch patch = { };
  Modulations modulations = { };
  Voice::Frame frames[kBlockSize] = { };
  int blocks = 0;
  while (!diagnostic->finished()) {
    diagnostic->Prepare(&patch, &modulations, kBlockSize);
    Check(patch.engine >= 0 && patch.engine < PLAITS_ENGINE_COUNT,
        "diagnostic engine stays in range");
    Check(patch.timbre >= 0.02f && patch.timbre <= 0.98f,
        "stress TIMBRE stays in tested range");
    frames[0].out = modulations.trigger_patched
        ? triggered_level : drone_level;
    diagnostic->Observe(usage, frames, kBlockSize);
    ++blocks;
  }
  return blocks;
}

static int Run(FmCarrierDiagnostic* diagnostic, float usage, bool signal) {
  return RunArticulations(
      diagnostic, usage, signal ? 32 : 0, signal ? 32 : 0);
}

int main() {
  Check(PLAITS_ENGINE_COUNT == 3, "diagnostic corpus has three FM engines");
  const int patch_count = 32 + 32 + 7;
  const int expected_blocks = patch_count *
      FmCarrierDiagnostic::kTimbreStates *
      FmCarrierDiagnostic::kPitchStates *
      FmCarrierDiagnostic::kArticulationStates *
      FmCarrierDiagnostic::kBlocksPerState;
  Check(expected_blocks == 90880, "22.72-second sweep block count");

  FmCarrierDiagnostic healthy;
  healthy.Init();
  Check(healthy.progress() == 0.0f, "sweep progress starts at zero");
  Check(Run(&healthy, 0.72f, true) == expected_blocks,
      "healthy sweep visits every state exactly once");
  Check(healthy.progress() == 1.0f, "sweep progress finishes at one");
  Check(healthy.passed(), "healthy sweep passes");
  Check(fabsf(healthy.peak_usage() - 0.72f) < 0.0001f,
      "healthy sweep retains peak CPU");
  Check(healthy.over_ninety() == 0 && healthy.missed_deadline() == 0,
      "healthy sweep has CPU headroom");
  Check(healthy.silent_centres() == 0, "healthy centres produce signal");
  Check(healthy.failure_mask() == 0, "healthy sweep has no failure bits");

  Patch patch = { };
  Modulations modulations = { };
  healthy.Prepare(&patch, &modulations, kBlockSize);
  Check(patch.engine == 0 && fabsf(patch.note - 36.0f) < 0.001f,
      "audible loop begins on its first scene");
  Check(fabsf(patch.timbre - 0.5f) < 0.001f,
      "audible scene begins at stock midpoint");
  Voice::Frame frames[kBlockSize];
  for (size_t i = 0; i < kBlockSize; ++i) {
    frames[i].out = 123;
    frames[i].aux = -123;
  }
  healthy.MuteStress(frames, kBlockSize);
  Check(frames[0].out == 123 && frames[0].aux == -123,
      "audible loop is not muted after qualification");

  FmCarrierDiagnostic overloaded;
  overloaded.Init();
  Run(&overloaded, 1.01f, true);
  Check(!overloaded.passed() && overloaded.missed_deadline() > 0,
      "deadline miss fails qualification");
  Check(overloaded.failure_mask() ==
      (FmCarrierDiagnostic::FAILURE_CPU_HEADROOM |
       FmCarrierDiagnostic::FAILURE_DEADLINE),
      "overload reports CPU and deadline bits");

  FmCarrierDiagnostic low_headroom;
  low_headroom.Init();
  Run(&low_headroom, 0.91f, true);
  Check(low_headroom.failure_mask() ==
      FmCarrierDiagnostic::FAILURE_CPU_HEADROOM,
      "sub-deadline overload reports only the CPU bit");

  FmCarrierDiagnostic silent;
  silent.Init();
  Run(&silent, 0.5f, false);
  Check(!silent.passed() && silent.silent_centres() == patch_count,
      "one silent centre check per patch fails qualification");
  Check(silent.failure_mask() == FmCarrierDiagnostic::FAILURE_SILENCE,
      "silent sweep reports only the silence bit");

  FmCarrierDiagnostic drone_quiet;
  drone_quiet.Init();
  RunArticulations(&drone_quiet, 0.5f, 0, 32);
  Check(drone_quiet.passed() && drone_quiet.silent_centres() == 0,
      "triggered signal accepts a voice that is silent as a drone");

  FmCarrierDiagnostic numerical_signal;
  numerical_signal.Init();
  RunArticulations(&numerical_signal, 0.5f, 4, 4);
  Check(numerical_signal.passed() && numerical_signal.silent_centres() == 0,
      "real low-level samples are not misclassified as a render failure");

  printf(
      "PASS: autonomous 71-patch diagnostic covers %d blocks, reports CPU "
      "and genuine two-mode silence failures, and enters its audible loop.\n",
      expected_blocks);
}
