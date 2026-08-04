// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
// Host renderer for decoded LPC plan files used by Speech-bank previews.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdint.h>
#include <vector>

#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/dsp/speech/lpc_speech_synth.h"
#include "stmlib/utils/random.h"

namespace {
const int kRate = 48000;
const int kSamplesPerFrame = kRate / 40;
const int kGap = kRate / 10;

void U16(FILE* f, uint16_t x) { fwrite(&x, sizeof(x), 1, f); }
void U32(FILE* f, uint32_t x) { fwrite(&x, sizeof(x), 1, f); }

bool WriteWav(const char* path, const std::vector<float>& samples) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
  fwrite("RIFF", 4, 1, f); U32(f, 36 + bytes); fwrite("WAVEfmt ", 8, 1, f);
  U32(f, 16); U16(f, 1); U16(f, 1); U32(f, kRate); U32(f, kRate * 2);
  U16(f, 2); U16(f, 16); fwrite("data", 4, 1, f); U32(f, bytes);
  for (size_t i = 0; i < samples.size(); ++i) {
    float value = std::max(-1.0f, std::min(1.0f, samples[i]));
    int16_t pcm = static_cast<int16_t>(value * 32767.0f);
    fwrite(&pcm, sizeof(pcm), 1, f);
  }
  fclose(f);
  return true;
}

bool ReadPlan(const char* path, std::vector<plaits::LPCSpeechSynth::Frame>* out) {
  std::ifstream input(path);
  int v[12];
  while (input >> v[0] >> v[1] >> v[2] >> v[3] >> v[4] >> v[5] >>
      v[6] >> v[7] >> v[8] >> v[9] >> v[10] >> v[11]) {
    plaits::LPCSpeechSynth::Frame f;
    f.energy=v[0]; f.period=v[1]; f.k0=v[2]; f.k1=v[3]; f.k2=v[4];
    f.k3=v[5]; f.k4=v[6]; f.k5=v[7]; f.k6=v[8]; f.k7=v[9];
    f.k8=v[10]; f.k9=v[11]; out->push_back(f);
  }
  return !out->empty() && input.eof();
}

class Renderer {
 public:
  explicit Renderer(float prosody)
      : prosody_(prosody), phase_(0.0f), sample_(0.0f), next_(0.0f) {
    synth_.Init(); stmlib::Random::Seed(0x21);
  }
  void Frame(const plaits::LPCSpeechSynth::Frame& f) {
    plaits::LPCSpeechSynth::Frame pair[2] = { f, f };
    synth_.PlayFrame(pair, 0.0f, false);
  }
  float Sample() {
    float result = next_; next_ = 0.0f; phase_ += 1.0f / 6.0f;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f; float reset = phase_ * 6.0f; float excitation, value;
      synth_.Render(prosody_, 1.0f, &excitation, &value, 1);
      float d = value - sample_;
      result += d * stmlib::ThisBlepSample(reset);
      next_ += d * stmlib::NextBlepSample(reset); sample_ = value;
    }
    next_ += sample_; return result;
  }
 private:
  plaits::LPCSpeechSynth synth_;
  float prosody_, phase_, sample_, next_;
};
}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) return 2;
  const float prosody = static_cast<float>(atof(argv[2]));
  std::vector<std::vector<plaits::LPCSpeechSynth::Frame> > words(argc - 3);
  for (int i = 3; i < argc; ++i) if (!ReadPlan(argv[i], &words[i - 3])) return 2;
  Renderer renderer(prosody); std::vector<float> output;
  for (size_t word = 0; word < words.size(); ++word) {
    for (size_t frame = 0; frame < words[word].size(); ++frame) {
      renderer.Frame(words[word][frame]);
      for (int i = 0; i < kSamplesPerFrame; ++i) output.push_back(renderer.Sample());
    }
    if (word + 1 < words.size()) output.insert(output.end(), kGap, 0.0f);
  }
  return WriteWav(argv[1], output) ? 0 : 2;
}
