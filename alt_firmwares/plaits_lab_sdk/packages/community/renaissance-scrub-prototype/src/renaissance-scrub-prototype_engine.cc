// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Modified from Speech (mutable-instruments/speech@1.0.0) for Plaits Lab.
// The original copyright and license notice follow.

// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "renaissance-scrub-prototype_engine.h"

#include <algorithm>

#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/dsp/speech/lpc_speech_synth_controller.h"
#include "plaits/dsp/speech/lpc_speech_synth_words.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const uint8_t kEnergyLut[16] = {
  0x00, 0x02, 0x03, 0x04, 0x05, 0x07, 0x0a, 0x0f,
  0x14, 0x20, 0x29, 0x39, 0x51, 0x72, 0xa1, 0xff
};

const uint8_t kPeriodLut[64] = {
  0, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 45, 47, 49, 51, 53,
  54, 57, 59, 61, 63, 66, 69, 71, 73, 77, 79, 81, 85, 87, 92, 95, 99,
  102, 106, 110, 115, 119, 123, 128, 133, 138, 143, 149, 154, 160
};

const int16_t k0Lut[32] = {
  -32064, -31872, -31808, -31680, -31552, -31424, -31232, -30848,
  -30592, -30336, -30016, -29696, -29376, -28928, -28480, -27968,
  -26368, -24256, -21632, -18368, -14528, -10048, -5184, 0,
  5184, 10048, 14528, 18368, 21632, 24256, 26368, 27968
};

const int16_t k1Lut[32] = {
  -20992, -19328, -17536, -15552, -13440, -11200, -8768, -6272,
  -3712, -1088, 1536, 4160, 6720, 9216, 11584, 13824,
  15936, 17856, 19648, 21248, 22656, 24000, 25152, 26176,
  27072, 27840, 28544, 29120, 29632, 30080, 30464, 32384
};

const int8_t k2Lut[16] = {
  -110, -97, -83, -70, -56, -43, -29, -16, -2, 11, 25, 38, 52, 65, 79, 92
};
const int8_t k3Lut[16] = {
  -82, -68, -54, -40, -26, -12, 1, 15, 29, 43, 57, 71, 85, 99, 113, 126
};
const int8_t k4Lut[16] = {
  -82, -70, -59, -47, -35, -24, -12, -1, 11, 23, 34, 46, 57, 69, 81, 92
};
const int8_t k5Lut[16] = {
  -64, -53, -42, -31, -20, -9, 3, 14, 25, 36, 47, 58, 69, 80, 91, 102
};
const int8_t k6Lut[16] = {
  -77, -65, -53, -41, -29, -17, -5, 7, 19, 31, 43, 55, 67, 79, 90, 102
};
const int8_t k7Lut[8] = { -64, -40, -16, 7, 31, 55, 79, 102 };
const int8_t k8Lut[8] = { -64, -44, -24, -4, 16, 37, 57, 77 };
const int8_t k9Lut[8] = { -51, -33, -15, 4, 22, 32, 59, 77 };

}  // namespace

size_t RenaissanceScrubPrototypeEngine::DecodeWord(
    const uint8_t* data, bool store) {
  BitStream bitstream;
  bitstream.Init(data);

  LPCSpeechSynth::Frame frame;
  frame.energy = 0;
  frame.period = 0;
  frame.k0 = 0;
  frame.k1 = 0;
  frame.k2 = 0;
  frame.k3 = 0;
  frame.k4 = 0;
  frame.k5 = 0;
  frame.k6 = 0;
  frame.k7 = 0;
  frame.k8 = 0;
  frame.k9 = 0;

  if (store) {
    num_frames_ = 0;
  }
  while (true) {
    const int energy = bitstream.GetBits(4);
    if (energy == 0) {
      frame.energy = 0;
    } else if (energy == 0xf) {
      bitstream.Flush();
      break;
    } else {
      frame.energy = kEnergyLut[energy];
      const bool repeat = bitstream.GetBits(1);
      frame.period = kPeriodLut[bitstream.GetBits(6)];
      if (!repeat) {
        frame.k0 = k0Lut[bitstream.GetBits(5)];
        frame.k1 = k1Lut[bitstream.GetBits(5)];
        frame.k2 = k2Lut[bitstream.GetBits(4)];
        frame.k3 = k3Lut[bitstream.GetBits(4)];
        if (frame.period) {
          frame.k4 = k4Lut[bitstream.GetBits(4)];
          frame.k5 = k5Lut[bitstream.GetBits(4)];
          frame.k6 = k6Lut[bitstream.GetBits(4)];
          frame.k7 = k7Lut[bitstream.GetBits(3)];
          frame.k8 = k8Lut[bitstream.GetBits(3)];
          frame.k9 = k9Lut[bitstream.GetBits(3)];
        }
      }
    }
    if (store && num_frames_ < kMaxWordFrames) {
      frames_[num_frames_++] = frame;
    }
  }
  return bitstream.ptr() - data;
}

void RenaissanceScrubPrototypeEngine::LoadWord(int word) {
  if (word < 0) {
    word = 0;
  } else if (word >= kNumWords) {
    word = kNumWords - 1;
  }
  if (word == word_) {
    return;
  }

  const uint8_t* data = bank_1;
  for (int i = 0; i <= word; ++i) {
    data += DecodeWord(data, i == word);
  }
  if (num_frames_ > 0) {
    // LPCSpeechSynth::PlayFrame always reads frame n and n + 1, even when the
    // requested blend is zero. Duplicate the final frame as a guard.
    frames_[num_frames_] = frames_[num_frames_ - 1];
  }
  word_ = word;
  playback_frame_ = -1;
  remaining_frame_samples_ = 0;
}

void RenaissanceScrubPrototypeEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void RenaissanceScrubPrototypeEngine::Reset() {
  synth_.Init();
  word_ = -1;
  num_frames_ = 0;
  playback_frame_ = -1;
  remaining_frame_samples_ = 0;
  clock_phase_ = 0.0f;
  fill(&sample_[0], &sample_[2], 0.0f);
  fill(&next_sample_[0], &next_sample_[2], 0.0f);
  LoadWord(0);
}

void RenaissanceScrubPrototypeEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  int word = static_cast<int>(parameters.harmonics * kNumWords);
  if (word >= kNumWords) {
    word = kNumWords - 1;
  }
  LoadWord(word);

  if (num_frames_ == 0) {
    fill(out, out + size, 0.0f);
    fill(aux, aux + size, 0.0f);
    return;
  }

  const float f0 = NoteToFrequency(parameters.note);
  const float rate_ratio = SemitonesToRatio((parameters.morph - 0.5f) * 36.0f);
  const float rate = rate_ratio / 6.0f;
  const float pitch_shift = f0 /
      (rate_ratio * kLPCSpeechSynthDefaultF0 / kCorrectedSampleRate);
  const float time_stretch = SemitonesToRatio(
      -(parameters.macro - 0.5f) * 24.0f);

  if (parameters.trigger == TRIGGER_RISING_EDGE) {
    playback_frame_ = static_cast<int>(
        parameters.timbre * static_cast<float>(num_frames_));
    if (playback_frame_ >= num_frames_) {
      playback_frame_ = num_frames_ - 1;
    }
    remaining_frame_samples_ = 0;
  }

  if (playback_frame_ == -1 && remaining_frame_samples_ == 0) {
    synth_.PlayFrame(
        frames_,
        parameters.timbre * (static_cast<float>(num_frames_) - 1.0001f),
        true);
  } else if (remaining_frame_samples_ == 0) {
    synth_.PlayFrame(frames_, static_cast<float>(playback_frame_), false);
    remaining_frame_samples_ = static_cast<size_t>(
        kSampleRate / kLPCSpeechSynthFPS * time_stretch);
    ++playback_frame_;
    if (playback_frame_ >= num_frames_) {
      playback_frame_ = -1;
    }
  }
  remaining_frame_samples_ -= min(size, remaining_frame_samples_);

  while (size--) {
    float this_sample[2];
    copy(&next_sample_[0], &next_sample_[2], &this_sample[0]);
    fill(&next_sample_[0], &next_sample_[2], 0.0f);

    clock_phase_ += rate;
    if (clock_phase_ >= 1.0f) {
      clock_phase_ -= 1.0f;
      const float reset_time = clock_phase_ / rate;
      float new_sample[2];
      synth_.Render(0.0f, pitch_shift, &new_sample[0], &new_sample[1], 1);

      const float excitation_step = new_sample[0] - sample_[0];
      const float voice_step = new_sample[1] - sample_[1];
      this_sample[0] += excitation_step * ThisBlepSample(reset_time);
      next_sample_[0] += excitation_step * NextBlepSample(reset_time);
      this_sample[1] += voice_step * ThisBlepSample(reset_time);
      next_sample_[1] += voice_step * NextBlepSample(reset_time);
      copy(&new_sample[0], &new_sample[2], &sample_[0]);
    }
    next_sample_[0] += sample_[0];
    next_sample_[1] += sample_[1];
    *aux++ = this_sample[0];
    *out++ = this_sample[1];
  }
}

}  // namespace plaits
