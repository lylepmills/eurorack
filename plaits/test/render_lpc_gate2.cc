// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Host-only renderer for CMU-Arctic-derived LPC frame plans. It uses the real
// Plaits LPC synth but is not linked into firmware.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdint.h>
#include <vector>

#include "plaits/dsp/speech/lpc_speech_synth.h"
#include "plaits/dsp/oscillator/oscillator.h"
#include "stmlib/utils/random.h"

namespace {

const int kOutputSampleRate = 48000;
const int kSamplesPerLPCFrame = kOutputSampleRate / 40;

void WriteU16(std::FILE* file, uint16_t value) {
  std::fwrite(&value, sizeof(value), 1, file);
}

void WriteU32(std::FILE* file, uint32_t value) {
  std::fwrite(&value, sizeof(value), 1, file);
}

bool WriteWav(const char* path, const std::vector<float>& samples) {
  std::FILE* file = std::fopen(path, "wb");
  if (!file) {
    return false;
  }
  const uint32_t data_size = static_cast<uint32_t>(samples.size() * 2);
  std::fwrite("RIFF", 4, 1, file);
  WriteU32(file, 36 + data_size);
  std::fwrite("WAVEfmt ", 8, 1, file);
  WriteU32(file, 16);
  WriteU16(file, 1);
  WriteU16(file, 1);
  WriteU32(file, kOutputSampleRate);
  WriteU32(file, kOutputSampleRate * 2);
  WriteU16(file, 2);
  WriteU16(file, 16);
  std::fwrite("data", 4, 1, file);
  WriteU32(file, data_size);
  for (size_t i = 0; i < samples.size(); ++i) {
    const float sample = std::max(-1.0f, std::min(1.0f, samples[i]));
    const int16_t pcm = static_cast<int16_t>(sample * 32767.0f);
    std::fwrite(&pcm, sizeof(pcm), 1, file);
  }
  std::fclose(file);
  return true;
}

bool ReadPlan(
    const char* path,
    std::vector<plaits::LPCSpeechSynth::Frame>* frames) {
  std::ifstream input(path);
  if (!input) {
    return false;
  }
  int values[12];
  while (input >> values[0] >> values[1] >> values[2] >> values[3] >>
      values[4] >> values[5] >> values[6] >> values[7] >> values[8] >>
      values[9] >> values[10] >> values[11]) {
    if (values[0] < 0 || values[0] > 255 ||
        values[1] < 0 || values[1] > 255 ||
        values[2] < -32768 || values[2] > 32767 ||
        values[3] < -32768 || values[3] > 32767) {
      return false;
    }
    for (int i = 4; i < 12; ++i) {
      if (values[i] < -128 || values[i] > 127) {
        return false;
      }
    }
    plaits::LPCSpeechSynth::Frame frame;
    frame.energy = values[0];
    frame.period = values[1];
    frame.k0 = values[2];
    frame.k1 = values[3];
    frame.k2 = values[4];
    frame.k3 = values[5];
    frame.k4 = values[6];
    frame.k5 = values[7];
    frame.k6 = values[8];
    frame.k7 = values[9];
    frame.k8 = values[10];
    frame.k9 = values[11];
    frames->push_back(frame);
  }
  return !frames->empty() && input.eof();
}

class Renderer {
 public:
  Renderer(float internal_rate, float pitch_shift, float prosody_amount) :
      internal_rate_(internal_rate), pitch_shift_(pitch_shift),
      prosody_amount_(prosody_amount),
      clock_phase_(0.0f), sample_(0.0f), next_sample_(0.0f) {
    synth_.Init();
    stmlib::Random::Seed(0x21);
  }

  void SelectFrame(const plaits::LPCSpeechSynth::Frame& frame) {
    plaits::LPCSpeechSynth::Frame pair[2] = { frame, frame };
    synth_.PlayFrame(pair, 0.0f, false);
  }

  float RenderSample() {
    float this_sample = next_sample_;
    next_sample_ = 0.0f;
    clock_phase_ += internal_rate_;
    if (clock_phase_ >= 1.0f) {
      clock_phase_ -= 1.0f;
      const float reset_time = clock_phase_ / internal_rate_;
      float excitation;
      float new_sample;
      synth_.Render(
          prosody_amount_, pitch_shift_, &excitation, &new_sample, 1);
      const float discontinuity = new_sample - sample_;
      this_sample += discontinuity * stmlib::ThisBlepSample(reset_time);
      next_sample_ += discontinuity * stmlib::NextBlepSample(reset_time);
      sample_ = new_sample;
    }
    next_sample_ += sample_;
    return this_sample;
  }

 private:
  plaits::LPCSpeechSynth synth_;
  float internal_rate_;
  float pitch_shift_;
  float prosody_amount_;
  float clock_phase_;
  float sample_;
  float next_sample_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 5 || argc > 7) {
    std::fprintf(stderr,
        "usage: %s PLAN OUTPUT.wav FORMANT_SEMITONES PITCH_HZ "
        "[PROSODY [GAIN]]\n",
        argv[0]);
    return 2;
  }
  const float formant_semitones = static_cast<float>(std::atof(argv[3]));
  const float pitch_hz = static_cast<float>(std::atof(argv[4]));
  const float prosody_amount = argc == 6
      ? static_cast<float>(std::atof(argv[5]))
      : argc == 7 ? static_cast<float>(std::atof(argv[5])) : 0.0f;
  const float gain = argc == 7
      ? static_cast<float>(std::atof(argv[6]))
      : 0.35f;
  if (formant_semitones < -18.0f || formant_semitones > 18.0f ||
      pitch_hz < 50.0f || pitch_hz > 300.0f ||
      prosody_amount < 0.0f || prosody_amount > 1.0f ||
      gain < 0.0f || gain > 2.0f) {
    std::fprintf(stderr,
        "formant range is -18..18 semitones; pitch range is 50..300 Hz; "
        "prosody range is 0..1; gain range is 0..2\n");
    return 2;
  }

  std::vector<plaits::LPCSpeechSynth::Frame> frames;
  if (!ReadPlan(argv[1], &frames)) {
    std::fprintf(stderr, "invalid or unreadable plan: %s\n", argv[1]);
    return 2;
  }

  const float rate_ratio = std::pow(2.0f, formant_semitones / 12.0f);
  Renderer renderer(
      rate_ratio / 6.0f,
      pitch_hz / (rate_ratio * 100.0f),
      prosody_amount);
  std::vector<float> samples;
  samples.reserve(frames.size() * kSamplesPerLPCFrame);
  float peak = 0.0f;
  for (size_t i = 0; i < frames.size(); ++i) {
    renderer.SelectFrame(frames[i]);
    for (int j = 0; j < kSamplesPerLPCFrame; ++j) {
      const float sample = renderer.RenderSample();
      samples.push_back(sample);
      peak = std::max(peak, std::fabs(sample));
    }
  }

  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] *= gain;
  }
  if (!WriteWav(argv[2], samples)) {
    std::perror(argv[2]);
    return 2;
  }
  std::printf("rendered %s (%.3f s, %zu LPC frames, formant %+.1f st, "
      "pitch %.1f Hz, prosody %.2f, raw peak %.4f, gain %.2f)\n",
      argv[2], static_cast<double>(samples.size()) / kOutputSampleRate,
      frames.size(), formant_semitones, pitch_hz, prosody_amount, peak, gain);
  return 0;
}
