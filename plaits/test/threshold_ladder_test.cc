// Copyright 2026 Rubato Audio.
//
// Host test for plaits/threshold_ladder.h: drives the ladder with a fake cycle
// counter and a DAC model that replays stale frames when the block is written
// late, and writes the AUX stream (a 1330 Hz sine standing in for the Voice
// sub-oscillator) so plaits/test/test_threshold_ladder_host.py can check the
// decoder end to end.
//
//   g++ -std=c++98 -O2 -DTEST -I. plaits/test/threshold_ladder_test.cc \
//     plaits/resources.cc -o /tmp/threshold_ladder_test

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plaits/threshold_ladder.h"

namespace plaits {
uint32_t threshold_ladder_test_cycles = 0;
}

using namespace plaits;

static void Check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
  }
}

// Modelled: engine + UI cost before the burn, post-processing after it, and
// the un-bracketed time (interrupt entry) that decides the real deadline.
static const float kBase = 0.30f;
static const float kPost = 0.02f;
static const float kEntry = 0.05f;

int main(int argc, char** argv) {
  const char* wav_path = argc > 1 ? argv[1] : "/tmp/threshold_ladder.wav";
  static ThresholdLadder ladder;
  ladder.Init();
  Patch patch;
  Modulations modulations;
  memset(&patch, 0, sizeof(patch));
  memset(&modulations, 0, sizeof(modulations));
  const float budget = kBlockSize * (static_cast<float>(F_CPU) / kSampleRate);

  FILE* f = fopen(wav_path, "wb");
  Check(f != NULL, "open wav");
  unsigned char header[44] = { 0 };
  fwrite(header, 1, 44, f);
  uint32_t frames_written = 0;

  Voice::Frame halves[2][kBlockSize];
  memset(halves, 0, sizeof(halves));
  int half = 0;
  float phase = 0.0f;
  int report_blocks = 0;
  int first_late_step = -1;
  while (report_blocks < ThresholdLadder::kBlocksPerSecond * 25) {
    Voice::Frame frames[kBlockSize];
    if (ladder.reporting()) {
      ladder.WriteReport(frames, kBlockSize);
      ++report_blocks;
      for (size_t i = 0; i < kBlockSize; ++i) {
        short s[2] = { frames[i].out, frames[i].aux };
        fwrite(s, 2, 2, f);
        ++frames_written;
      }
      continue;
    }
    ladder.BeginCallback();
    const uint32_t start = threshold_ladder_test_cycles;
    ladder.Prepare(&patch, &modulations);
    Check(patch.aux_output_option == 2 && patch.aux_subosc_option == 3,
          "AUX is the sine sub-oscillator");
    threshold_ladder_test_cycles = start + static_cast<uint32_t>(kBase * budget);
    ladder.Burn();
    threshold_ladder_test_cycles += static_cast<uint32_t>(kPost * budget);
    const float bracket =
        static_cast<float>(threshold_ladder_test_cycles - start) / budget;
    // Voice writes the sub-oscillator sine.
    for (size_t i = 0; i < kBlockSize; ++i) {
      phase += 1330.0f / 47872.34f;
      if (phase >= 1.0f) phase -= 1.0f;
      frames[i].out = 0;
      frames[i].aux = static_cast<short>(sinf(6.2831853f * phase) * 16000.0f);
    }
    const bool late = bracket + kEntry > 1.0f;
    const ThresholdLadder::Phase before = ladder.phase();
    const int step = ladder.step();
    ladder.EndRender(late);
    ladder.WriteStart(frames, kBlockSize);
    if (late && before == ThresholdLadder::PHASE_LADDER && first_late_step < 0) {
      first_late_step = step;
    }
    const int stale = late ? static_cast<int>(
        ceilf((bracket + kEntry - 1.0f) * kBlockSize)) : 0;
    for (size_t i = 0; i < kBlockSize; ++i) {
      const Voice::Frame& played = static_cast<int>(i) < stale && stale <= 12
          ? halves[half][i] : frames[i];
      short s[2] = { played.out, played.aux };
      fwrite(s, 2, 2, f);
      ++frames_written;
      halves[half][i] = frames[i];
    }
    half ^= 1;
  }
  // WAV header at 47872 Hz, stereo 16-bit.
  unsigned char h[44];
  memcpy(h, "RIFF", 4);
  const uint32_t data = frames_written * 4;
  const uint32_t riff = 36 + data;
  memcpy(h + 4, &riff, 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  const uint32_t fmt = 16; memcpy(h + 16, &fmt, 4);
  const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  memcpy(h + 20, &pcm, 2); memcpy(h + 22, &ch, 2);
  const uint32_t rate = 47872, bps = rate * 4;
  memcpy(h + 24, &rate, 4); memcpy(h + 28, &bps, 4);
  memcpy(h + 32, &align, 2); memcpy(h + 34, &bits, 2);
  memcpy(h + 36, "data", 4); memcpy(h + 40, &data, 4);
  fseek(f, 0, SEEK_SET);
  fwrite(h, 1, 44, f);
  fclose(f);
  // bracket = target + post; late when target + post + entry > 1.
  Check(first_late_step >= 0, "some step went late");
  printf("PASS threshold_ladder_test: first late step %d (target %.2f), %u frames\n",
         first_late_step, ThresholdLadder::Load(first_late_step), frames_written);
  return 0;
}
