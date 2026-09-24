// Copyright 2026 Rubato Audio.
//
// Host test for plaits/overrun_sweep.h. Drives the complete schedule with a
// fake cycle counter and a model of the DAC's double buffer that replays stale
// frames when a fill is late, checks the on-module bookkeeping, and writes the
// resulting OUT/AUX stream as a WAV so plaits/tools/overrun_sweep_host.py can
// be verified end to end (packet decode + pilot glitch detection) before any
// hardware run.
//
//   g++ -std=c++98 -O2 -DTEST -DPLAITS_OVERRUN_SWEEP_ENGINES=3 \
//     -DPLAITS_OVERRUN_SWEEP_TAIL_SECONDS=1 -I. \
//     plaits/test/overrun_sweep_test.cc plaits/resources.cc \
//     -o /tmp/overrun_sweep_test && /tmp/overrun_sweep_test out.wav out.json

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plaits/overrun_sweep.h"

namespace plaits {
uint32_t overrun_sweep_test_cycles = 0;
}

using namespace plaits;

static void Check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
  }
}

// Time the cost bracket cannot see (interrupt entry, other ISRs): the output
// deadline is missed when bracketed cost + this exceeds the period.
static const float kOverhead = 0.03f;
// The sweep's own work after Render, which only the end-of-callback counter
// ("late with overhead") includes.
static const float kSweepOverhead = 0.02f;

// Synthetic engine cost for the block the sweep is about to render.
static float Cost(const OverrunSweep& sweep) {
  const int engine = sweep.engine();
  const int state = sweep.state();
  switch (sweep.phase()) {
    case OverrunSweep::PHASE_GRID:
      if (engine == 1) {
        const int pitch = (state / OverrunSweep::kParamStates) %
            OverrunSweep::kPitches;
        const int trigger = state / (OverrunSweep::kParamStates *
            OverrunSweep::kPitches * OverrunSweep::kOutputs);
        if (trigger == 2 && pitch == 4) return 1.03f;
      }
      return 0.45f;
    case OverrunSweep::PHASE_TAIL:
      // Engine 2's first tail slows down late in the ring-out, the way a
      // denormal decay would.
      if (engine == 2 && state == 0 && sweep.block() >= 3000) return 1.02f;
      return 0.5f;
    default:
      return 0.4f;
  }
}

static bool StereoCapable(int engine) { return engine == 1; }

struct Wav {
  FILE* f;
  uint32_t frames;
  void Open(const char* path) {
    f = fopen(path, "wb");
    Check(f != NULL, "open wav");
    unsigned char header[44] = { 0 };
    fwrite(header, 1, 44, f);
    frames = 0;
  }
  void Write(short l, short r) {
    short s[2] = { l, r };
    fwrite(s, 2, 2, f);
    ++frames;
  }
  static void Put32(unsigned char* p, uint32_t v) {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
  }
  static void Put16(unsigned char* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
  void Close(uint32_t rate) {
    unsigned char h[44];
    memcpy(h, "RIFF", 4); Put32(h + 4, 36 + frames * 4);
    memcpy(h + 8, "WAVEfmt ", 8); Put32(h + 16, 16); Put16(h + 20, 1);
    Put16(h + 22, 2); Put32(h + 24, rate); Put32(h + 28, rate * 4);
    Put16(h + 32, 4); Put16(h + 34, 16);
    memcpy(h + 36, "data", 4); Put32(h + 40, frames * 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
    fclose(f);
  }
};

int main(int argc, char** argv) {
  const char* wav_path = argc > 1 ? argv[1] : "/tmp/overrun_sweep_test.wav";
  const char* json_path = argc > 2 ? argv[2] : "/tmp/overrun_sweep_test.json";

  static OverrunSweep sweep;
  sweep.Init(7);
  Patch patch;
  Modulations modulations;
  memset(&patch, 0, sizeof(patch));
  memset(&modulations, 0, sizeof(modulations));

  // Schedule arithmetic the decoder mirrors.
  Check(OverrunSweep::kGridStates == 3645, "grid size");
  OverrunSweep::Settings s;
  OverrunSweep::GridState(2754, &s);
  Check(s.trigger == 2 && s.output == 0 && s.note == 108.0f &&
        s.harmonics == 0.0f && s.twist == 0.0f, "grid decode");
  OverrunSweep::GridState(80, &s);
  Check(s.harmonics == 1.0f && s.timbre == 1.0f && s.morph == 1.0f &&
        s.twist == 1.0f && s.note == 12.0f, "grid corner");

  // DAC model: two halves; a late fill plays the half's previous contents
  // for as many frames as the fill was late.
  Voice::Frame halves[2][kBlockSize];
  memset(halves, 0, sizeof(halves));
  uint32_t late = 0;
  uint32_t double_pending = 0;
  uint32_t expected_stale_blocks = 0;
  int half = 0;
  const float budget = kBlockSize * (static_cast<float>(F_CPU) / kSampleRate);

  Wav wav;
  wav.Open(wav_path);
  FILE* spans = fopen(json_path, "w");
  Check(spans != NULL, "open json");
  fprintf(spans, "{\"stale_frames\": [");
  bool first_span = true;

  int report_blocks = 0;
  const int kReportBlocks = OverrunSweep::kBlocksPerSecond * 40;
  while (report_blocks < kReportBlocks) {
    Voice::Frame frames[kBlockSize];
    if (sweep.reporting()) {
      sweep.WriteReport(frames, kBlockSize);
      ++report_blocks;
      for (size_t i = 0; i < kBlockSize; ++i) {
        wav.Write(frames[i].out, frames[i].aux);
      }
      continue;
    }
    sweep.BeginCallback(0);
    const uint32_t start = overrun_sweep_test_cycles;
    sweep.Prepare(&patch, &modulations);
    const float cost = Cost(sweep);
    overrun_sweep_test_cycles = start + static_cast<uint32_t>(cost * budget);
    for (size_t i = 0; i < kBlockSize; ++i) {
      frames[i].out = 32767;  // engine OUT; the pilot (|x| <= 16000) replaces it
      frames[i].aux = 0;
    }
    sweep.EndRender(kBlockSize);
    const float usage = sweep.last_usage();
    const float effective = usage + kOverhead;
    const bool output_late = effective > 1.0f;
    sweep.WriteOutputs(frames, kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
      Check(frames[i].out != 32767, "pilot owns OUT");
    }
    sweep.Observe(output_late, late, double_pending,
                  StereoCapable(patch.engine));
    if (effective + kSweepOverhead > 1.0f) ++late;  // end-of-callback view
    sweep.EndCallback(kBlockSize);
    sweep.Poll();  // the idle main loop between interrupts

    int stale = 0;
    if (output_late) {
      stale = static_cast<int>(ceilf((effective - 1.0f) * kBlockSize));
      if (stale > static_cast<int>(kBlockSize)) stale = kBlockSize;
      ++expected_stale_blocks;
      fprintf(spans, "%s[%u, %d]", first_span ? "" : ", ", wav.frames, stale);
      first_span = false;
    }
    for (size_t i = 0; i < kBlockSize; ++i) {
      const Voice::Frame& played =
          static_cast<int>(i) < stale ? halves[half][i] : frames[i];
      wav.Write(played.out, played.aux);
      halves[half][i] = frames[i];
    }
    half ^= 1;
  }
  wav.Close(47872);

  // On-module bookkeeping.
  const OverrunSweepEngineResult& e0 = sweep.result(0);
  const OverrunSweepEngineResult& e1 = sweep.result(1);
  const OverrunSweepEngineResult& e2 = sweep.result(2);
  Check(e0.grid.late == 0 && e0.tail.late == 0, "engine 0 clean");
  Check(e0.stereo_capable == 0 && e1.stereo_capable == 1, "stereo latch");
  Check(e1.grid.late == 243 * 48,
      "engine 1: every block of the hot states is late");
  Check(e1.grid.late_states == 243, "engine 1 hot state count");
  Check(e1.grid.peak >= 1025 && e1.grid.peak <= 1040, "engine 1 peak");
  Check(e1.random.late == 0 && e1.tail.late == 0, "engine 1 elsewhere clean");
  Check(e2.grid.late == 0, "engine 2 grid clean");
  Check(e2.tail.late > 0 && e2.tail.worst_state == 0, "engine 2 tail late");
  Check(e2.tail.late_states == 1, "engine 2 one late tail");
  Check(e2.tail.late == 989, "engine 2 tail blocks 3000..3988 late");
  Check(e2.tail_worst_block >= 3000, "engine 2 tail peak is late");
  // Engine 0 has no stereo path, so its grid skipped the stereo third; the
  // decoder checks the resulting timeline against the capture.
  Check(e1.late_with_overhead >= e1.grid.late, "overhead view >= deadline");
  for (int i = 0; i < OverrunSweep::kLadderSteps; ++i) {
    const float load = OverrunSweep::LadderLoad(i);
    const bool should_be_late = load + kOverhead > 1.0f + 1e-4f;
    Check((sweep.ladder_late(i) > 0) == should_be_late, "ladder threshold");
    Check(fabsf(sweep.ladder_peak(i) / 1000.0f - load) < 0.01f,
        "ladder load reproduced");
  }
  Check(sweep.FailureMask() == 3, "failure mask");

  fprintf(spans, "], \"late\": %u, \"ladder_late\": [", expected_stale_blocks);
  for (int i = 0; i < OverrunSweep::kLadderSteps; ++i) {
    fprintf(spans, "%s%u", i ? ", " : "", sweep.ladder_late(i));
  }
  fprintf(spans, "], \"engine_late\": [%u, %u, %u], \"tail_late\": [%u, %u, %u]}\n",
      e0.grid.late + e0.random.late, e1.grid.late + e1.random.late,
      e2.grid.late + e2.random.late, e0.tail.late, e1.tail.late, e2.tail.late);
  fclose(spans);
  printf("PASS overrun_sweep_test: %u stale blocks, %u frames (%.1f s)\n",
      expected_stale_blocks, wav.frames, wav.frames / 47872.0f);
  return 0;
}
