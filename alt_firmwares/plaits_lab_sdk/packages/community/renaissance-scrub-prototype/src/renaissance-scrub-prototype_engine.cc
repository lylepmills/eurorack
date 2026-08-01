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

#include "stmlib/dsp/units.h"

#include "plaits/dsp/oscillator/oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const int kLPCFramesPerSecond = 40;

// Host-generated continuous LPC analysis of two Kokoro listening-test phrases.
// This bank is for local prototype evaluation, not a release provenance decision.
#include "renaissance-scrub-prototype_phrases.inc"

}  // namespace

void RenaissanceScrubPrototypeEngine::LoadPhrase(int phrase) {
  if (phrase < 0) {
    phrase = 0;
  } else if (phrase >= kNumPhrases) {
    phrase = kNumPhrases - 1;
  }
  if (phrase == phrase_) {
    return;
  }

  frames_ = &kPhraseFrames[kPhraseOffsets[phrase]];
  num_frames_ = kPhraseLengths[phrase];
  phrase_ = phrase;
  playback_frame_ = -1;
  remaining_frame_samples_ = 0;
}

void RenaissanceScrubPrototypeEngine::Init(BufferAllocator* allocator) {
  Reset();
}

void RenaissanceScrubPrototypeEngine::Reset() {
  synth_.Init();
  frames_ = NULL;
  phrase_ = -1;
  num_frames_ = 0;
  playback_frame_ = -1;
  remaining_frame_samples_ = 0;
  clock_phase_ = 0.0f;
  fill(&sample_[0], &sample_[2], 0.0f);
  fill(&next_sample_[0], &next_sample_[2], 0.0f);
  LoadPhrase(0);
}

void RenaissanceScrubPrototypeEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = true;

  int phrase = static_cast<int>(parameters.harmonics * kNumPhrases);
  if (phrase >= kNumPhrases) {
    phrase = kNumPhrases - 1;
  }
  LoadPhrase(phrase);

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
  // MACRO is neutral at noon. Clockwise restores the captured pitch contour;
  // counter-clockwise inverts it for an intentionally experimental register.
  const float prosody_amount = (parameters.macro - 0.5f) * 2.0f;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
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
    remaining_frame_samples_ = kSampleRate / kLPCFramesPerSecond;
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
      synth_.Render(
          prosody_amount, pitch_shift, &new_sample[0], &new_sample[1], 1);

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
