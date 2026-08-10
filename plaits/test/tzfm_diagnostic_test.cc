#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "plaits/tzfm_diagnostic.h"

using namespace plaits;

static void Check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
  }
}

int main() {
  TzfmDiagnostic diagnostic;
  TzfmDiagnosticCounters counters = { 10, 20, 30, 40 };
  Patch patch = { };
  Modulations modulations = { };

  diagnostic.Init(4);
  Check(TzfmDiagnostic::kStatesPerStage == 540, "sweep state count");
  diagnostic.Prepare(&patch, &modulations, kBlockSize);
  Check(patch.engine == 0, "first engine");
  Check(fabsf(patch.note - 24.0f) < 0.001f, "first pitch");
  Check(fabsf(patch.harmonics - 0.02f) < 0.001f, "first harmonics");
  Check(fabsf(patch.frequency_modulation_amount + 1.0f) < 0.001f,
      "TZFM side");
  Check(modulations.frequency_patched, "synthetic FM patched");
  Check(!modulations.frequency_audio_rate, "slow stage block rate");
  for (size_t i = 1; i < kBlockSize; ++i) {
    Check(modulations.frequency_audio[i] == modulations.frequency_audio[0],
        "slow stage holds one FM value");
  }

  TzfmDiagnostic exponential;
  exponential.Init(8, true);
  exponential.Prepare(&patch, &modulations, kBlockSize);
  Check(
      modulations.frequency == modulations.frequency_audio[0],
      "exponential slow stage drives the ordinary block-rate FM input");

  const int blocks_per_stage =
      TzfmDiagnostic::kStatesPerStage * TzfmDiagnostic::kBlocksPerState;
  for (int i = 0; i < blocks_per_stage; ++i) {
    diagnostic.Observe(i == 17 ? 1.1f : 0.75f, counters);
  }
  Check(diagnostic.stage() == 1, "advances to fast stage");
  Check(diagnostic.result(0).measured[0] ==
      TzfmDiagnostic::kStatesPerStage * TzfmDiagnostic::kMeasuredBlocks,
      "slow measured block count");
  // Block 17 falls after the 16-block warmup of the first state.
  Check(diagnostic.result(0).missed_deadline[0] == 1,
      "deadline recorded after warmup");

  diagnostic.Prepare(&patch, &modulations, kBlockSize);
  Check(modulations.frequency_audio_rate, "fast stage sample rate");
  counters.overruns += 3;
  counters.resyncs += 4;
  counters.underflows += 5;
  counters.excess_lag += 6;
  for (int i = 0; i < blocks_per_stage; ++i) {
    diagnostic.Observe(0.8f, counters);
  }
  Check(diagnostic.engine() == 1 && diagnostic.stage() == 0,
      "advances to next engine");
  Check(diagnostic.result(0).fast_faults.overruns == 3, "overrun delta");
  Check(diagnostic.result(0).fast_faults.resyncs == 4, "resync delta");
  Check(diagnostic.result(0).fast_faults.underflows == 5, "underflow delta");
  Check(diagnostic.result(0).fast_faults.excess_lag == 6,
      "excess-lag delta");

  for (int engine = 1; engine < PLAITS_ENGINE_COUNT; ++engine) {
    for (int stage = 0; stage < 2; ++stage) {
      for (int i = 0; i < blocks_per_stage; ++i) {
        diagnostic.Observe(0.5f, counters);
      }
    }
  }
  Check(diagnostic.reporting(), "enters report mode after final engine");
  Check(!diagnostic.fast_stage(), "fast converter disabled for report");

  Voice::Frame frames[kBlockSize];
  diagnostic.WriteReport(frames, kBlockSize);
  for (size_t i = 0; i < kBlockSize; ++i) {
    Check(frames[i].out == 0 && frames[i].aux == 0,
        "report starts with one second of silence");
  }

  printf("tzfm_diagnostic_test: all checks passed\n");
  return 0;
}
