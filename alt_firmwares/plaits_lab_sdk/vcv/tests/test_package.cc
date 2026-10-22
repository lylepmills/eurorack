// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
// Package smoke test: check both output paths across control/patch states.
// Musical expectations belong to the package's SDK scenarios, not this host.
#include <cmath>
#include <cstdio>
#include "lab_voice.h"

int main() {
  using namespace plaits_lab_vcv;
  int checks = 0;
  for (int mode = 0; mode < 4; ++mode) {
    LabVoice voice;
    Controls c;
    Inputs in;
    in.trigger_patched = mode != 0;
    in.level_patched = mode == 2;
    c.plaits_lpg = mode == 3;
    double energy = 0.0;
    for (int block = 0; block < 4000; ++block) {
      c.harmonics = (block / 1000) / 3.0f;
      c.timbre = c.harmonics;
      c.morph = 1.0f - c.harmonics;
      c.macro = c.harmonics;
      in.trigger = block % 1000 < 20 ? 5.0f : 0.0f;
      in.level = (block / 1000) * 2.0f;
      float out[kBlockSize], aux[kBlockSize];
      voice.Render(c, in, out, aux);
      for (size_t i = 0; i < kBlockSize; ++i) {
        if (!std::isfinite(out[i]) || !std::isfinite(aux[i]) ||
            std::fabs(out[i]) > 5.0f || std::fabs(aux[i]) > 5.0f) {
          std::printf("FAIL %s: output bounds, mode %d, block %d\n", LabVoice::name(), mode, block);
          return 1;
        }
        energy += out[i] * out[i] + aux[i] * aux[i];
      }
    }
    ++checks;
    std::printf("ok    %s: OUT/AUX bounds, mode %d (combined RMS %.4f V)\n",
                LabVoice::name(), mode, std::sqrt(energy / (4000 * kBlockSize * 2)));
  }
  std::printf("package smoke: %d checks passed\n", checks);
  return 0;
}
