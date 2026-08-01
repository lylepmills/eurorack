// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Host-only renderer for the Renaissance custom-word Gate 1 experiment.
// It deliberately uses Plaits' existing MIT-licensed LPC phoneme frames and
// synth. It is not linked into firmware.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdint.h>
#include <string>
#include <vector>

// The stock phoneme table is private because firmware only reaches it through
// LPCSpeechSynthController. This research renderer needs read-only access to
// audition those exact frames without copying them into a second table.
#define private public
#include "plaits/dsp/speech/lpc_speech_synth_controller.h"
#undef private

#include "plaits/dsp/oscillator/oscillator.h"
#include "stmlib/utils/random.h"

namespace {

const int kOutputSampleRate = 48000;
const int kSamplesPerLPCFrame = kOutputSampleRate / 40;
struct Segment {
  int frame;
  int ticks;
};

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
    float sample = samples[i];
    sample = std::max(-1.0f, std::min(1.0f, sample));
    const int16_t pcm = static_cast<int16_t>(sample * 32767.0f);
    std::fwrite(&pcm, sizeof(pcm), 1, file);
  }
  std::fclose(file);
  return true;
}

bool ReadPlan(const char* path, std::vector<Segment>* segments) {
  std::ifstream input(path);
  if (!input) {
    return false;
  }
  Segment segment;
  while (input >> segment.frame >> segment.ticks) {
    if (segment.frame < -1 || segment.frame >= plaits::kLPCSpeechSynthNumPhonemes ||
        segment.ticks < 1) {
      return false;
    }
    segments->push_back(segment);
  }
  return !segments->empty() && input.eof();
}

class Renderer {
 public:
  Renderer(float internal_rate, float pitch_shift) :
      internal_rate_(internal_rate), pitch_shift_(pitch_shift),
      clock_phase_(0.0f), sample_(0.0f), next_sample_(0.0f) {
    synth_.Init();
    stmlib::Random::Seed(0x21);
  }

  void SelectFrame(int index) {
    plaits::LPCSpeechSynth::Frame frame;
    if (index < 0) {
      frame.energy = 0;
      frame.period = 0;
      frame.k0 = frame.k1 = 0;
      frame.k2 = frame.k3 = frame.k4 = frame.k5 = 0;
      frame.k6 = frame.k7 = frame.k8 = frame.k9 = 0;
    } else {
      frame = plaits::LPCSpeechSynthController::phonemes_[index];
    }
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
      synth_.Render(0.0f, pitch_shift_, &excitation, &new_sample, 1);
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
  float clock_phase_;
  float sample_;
  float next_sample_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::fprintf(stderr,
        "usage: %s PLAN OUTPUT.wav FORMANT_SEMITONES PITCH_HZ\n", argv[0]);
    return 2;
  }

  const float formant_semitones = static_cast<float>(std::atof(argv[3]));
  const float pitch_hz = static_cast<float>(std::atof(argv[4]));
  if (formant_semitones < -18.0f || formant_semitones > 18.0f ||
      pitch_hz < 50.0f || pitch_hz > 300.0f) {
    std::fprintf(stderr, "formant range is -18..18 semitones; pitch range is 50..300 Hz\n");
    return 2;
  }
  const float rate_ratio = std::pow(2.0f, formant_semitones / 12.0f);
  const float internal_rate = rate_ratio / 6.0f;
  // Counteract the rate change so formant register moves without changing the
  // requested fundamental, matching LPCSpeechSynthController's behavior.
  const float pitch_shift = pitch_hz / (rate_ratio * 100.0f);

  std::vector<Segment> segments;
  if (!ReadPlan(argv[1], &segments)) {
    std::fprintf(stderr, "invalid or unreadable plan: %s\n", argv[1]);
    return 2;
  }

  size_t total_samples = 0;
  for (size_t i = 0; i < segments.size(); ++i) {
    total_samples += segments[i].ticks * kSamplesPerLPCFrame;
  }

  Renderer renderer(internal_rate, pitch_shift);
  std::vector<float> samples;
  samples.reserve(total_samples);
  float peak = 0.0f;
  for (size_t i = 0; i < segments.size(); ++i) {
    renderer.SelectFrame(segments[i].frame);
    const int count = segments[i].ticks * kSamplesPerLPCFrame;
    for (int j = 0; j < count; ++j) {
      const float sample = renderer.RenderSample();
      samples.push_back(sample);
      peak = std::max(peak, std::fabs(sample));
    }
  }

  // A single fixed gain keeps separate renders directly comparable. Avoid
  // per-file normalization, which would hide weak or over-energetic mappings.
  const float gain = 0.42f;
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] *= gain;
  }

  if (!WriteWav(argv[2], samples)) {
    std::perror(argv[2]);
    return 2;
  }
  std::printf("rendered %s (%.3f s, formant %+.1f st, pitch %.1f Hz, "
      "raw peak %.4f, fixed gain %.2f)\n",
      argv[2], static_cast<double>(samples.size()) / kOutputSampleRate,
      formant_semitones, pitch_hz, peak, gain);
  return 0;
}
