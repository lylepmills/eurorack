// Copyright 2021 Emilie Gillet.
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
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// 6-operator FM synth.

#include "plaits/dsp/engine2/six_op_engine.h"

#include <algorithm>

#include "plaits/resources.h"

namespace plaits {

using namespace fm;
using namespace std;
using namespace stmlib;

void FMVoice::Init(fm::Algorithms<6>* algorithms, float sample_rate) {
  voice_.Init(algorithms, sample_rate);
  lfo_.Init(sample_rate);
  
  parameters_.sustain = false;
  parameters_.gate = false;
  parameters_.note = 48.0f;
  parameters_.velocity = 0.5f;
  parameters_.brightness = 0.5f;
  parameters_.envelope_control = 0.5f;
  parameters_.pitch_mod = 0.0f;
  parameters_.amp_mod = 0.0f;
  parameters_.modulator_detune = 0.0f;
  
  patch_ = NULL;
}

void FMVoice::Render(float* buffer, size_t size) {
  if (!patch_) {
    return;
  }
  voice_.Render(parameters_, buffer, size);
}

void FMVoice::LoadPatch(const fm::Patch* patch) {
  if (patch == patch_) {
    return;
  }
  patch_ = patch;
  voice_.SetPatch(patch_);
  lfo_.Set(patch_->modulations);
}

const int kNumPatchesPerBank = 32;

void SixOpEngine::Init(BufferAllocator* allocator) {
  patch_index_quantizer_.Init(32, 0.005f, false);

  algorithms_.Init();
  for (int i = 0; i < kNumSixOpVoices; ++i) {
    voice_[i].Init(&algorithms_, kCorrectedSampleRate);
  }
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize * 4);
  acc_buffer_ = allocator->Allocate<float>(kMaxBlockSize * kNumSixOpVoices);
  patches_ = allocator->Allocate<fm::Patch>(kNumPatchesPerBank);
  
  post_filter_ = 0.0f;
  post_filter_right_ = 0.0f;
  active_voice_ = kNumSixOpVoices - 1;
  rendered_voice_ = 0;
}

void SixOpEngine::Reset() {
  post_filter_ = 0.0f;
  post_filter_right_ = 0.0f;
}

void SixOpEngine::LoadUserData(const uint8_t* user_data) {
  // The shipped firmware always supplies a real patch bank, but the SDK preview
  // and the reference-render path call LoadUserData(NULL) for engines with no
  // bank; unpacking a null pointer dereferences it. Guard the unpack and still
  // reset the voices so a null load leaves a well-defined (default) patch set.
  if (user_data) {
    for (int i = 0; i < kNumPatchesPerBank; ++i) {
      patches_[i].Unpack(user_data + i * fm::Patch::SYX_SIZE);
    }
  }
  for (int i = 0; i < kNumSixOpVoices; ++i) {
    voice_[i].UnloadPatch();
  }
}

void SixOpEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  int patch_index = patch_index_quantizer_.Process(
      parameters.harmonics * 1.02f);
  const float modulator_detune = 24.0f * (parameters.macro - 0.5f);
  
  if (parameters.trigger & TRIGGER_UNPATCHED) {
    const float t = parameters.morph;
    voice_[0].mutable_lfo()->Scrub(2.0f * kCorrectedSampleRate * t);

    for (int i = 0; i < kNumSixOpVoices; ++i) {
      voice_[i].LoadPatch(&patches_[patch_index]);
      Voice<6>::Parameters* p = voice_[i].mutable_parameters();
      p->sustain = i == 0 ? true : false;
      p->gate = false;
      p->note = parameters.note;
      p->velocity = parameters.accent;
      p->brightness = parameters.timbre;
      p->envelope_control = t;
      p->modulator_detune = modulator_detune;
      voice_[i].set_modulations(voice_[0].lfo());
    }
  } else {
    if (parameters.trigger & TRIGGER_RISING_EDGE) {
      active_voice_ = (active_voice_ + 1) % kNumSixOpVoices;
      voice_[active_voice_].LoadPatch(&patches_[patch_index]);
      voice_[active_voice_].mutable_lfo()->Reset();
    }
    Voice<6>::Parameters* p = voice_[active_voice_].mutable_parameters();
    p->note = parameters.note;
    p->velocity = parameters.accent;
    p->envelope_control = parameters.morph;
    voice_[active_voice_].mutable_lfo()->Step(float(size));
    
    for (int i = 0; i < kNumSixOpVoices; ++i) {
      Voice<6>::Parameters* p = voice_[i].mutable_parameters();
      p->brightness = parameters.timbre;
      p->modulator_detune = modulator_detune;
      p->sustain = false;
      p->gate = (parameters.trigger & TRIGGER_HIGH) && (i == active_voice_);
      if (voice_[i].patch() != voice_[active_voice_].patch()) {
        voice_[i].mutable_lfo()->Step(float(size));
        voice_[i].set_modulations(voice_[i].lfo());
      } else {
        voice_[i].set_modulations(voice_[active_voice_].lfo());
      }
    }
  }

  // Naive block rendering.
  // fill(temp_buffer_[0], temp_buffer_[size], 0.0f);
  // for (int i = 0; i < kNumSixOpVoices; ++i) {
  //   voice_[i].Render(temp_buffer_, size);
  // }

  if ((PLAITS_STEREO_SIX_OP && parameters.stereo)) {
    // Staggered rendering, split by voice: the accumulation buffer always
    // holds the tail of the single voice rendered on the previous block, so
    // per-voice pan gains can be applied when the two halves are combined.
    const int previous_voice = rendered_voice_;
    fill(&temp_buffer_[0], &temp_buffer_[kNumSixOpVoices * size], 0.0f);
    rendered_voice_ = (rendered_voice_ + 1) % kNumSixOpVoices;
    voice_[rendered_voice_].Render(temp_buffer_, size * kNumSixOpVoices);

    float pan_left[kNumSixOpVoices];
    float pan_right[kNumSixOpVoices];
    // A free-running drone sustains a single voice: keep it centred rather
    // than parked on one side. With a trigger patched, the round-robin voice
    // allocation makes successive notes alternate between the two sides.
    const bool unpatched = parameters.trigger & TRIGGER_UNPATCHED;
    for (int i = 0; i < kNumSixOpVoices; ++i) {
      const float position = unpatched
          ? 0.5f
          : 0.2f + 0.6f * static_cast<float>(i) / \
              static_cast<float>(kNumSixOpVoices - 1);
      StereoPanGains(position, &pan_left[i], &pan_right[i]);
    }

    for (size_t i = 0; i < size; ++i) {
      const float previous = acc_buffer_[i] * 0.25f;
      const float current = temp_buffer_[i] * 0.25f;
      float left = SoftClip(
          previous * pan_left[previous_voice] + \
          current * pan_left[rendered_voice_]);
      float right = SoftClip(
          previous * pan_right[previous_voice] + \
          current * pan_right[rendered_voice_]);
      if (parameters.macro < 0.5f) {
        const float darkness = (0.5f - parameters.macro) * 2.0f;
        const float coefficient = 1.0f - darkness * 0.92f;
        ONE_POLE(post_filter_, left, coefficient);
        left = post_filter_;
        ONE_POLE(post_filter_right_, right, coefficient);
        right = post_filter_right_;
      } else {
        post_filter_ = left;
        post_filter_right_ = right;
        const float saturation = (parameters.macro - 0.5f) * 2.0f;
        left += (SoftClip(left * 3.0f) - left) * saturation;
        right += (SoftClip(right * 3.0f) - right) * saturation;
      }
      out[i] = left;
      aux[i] = right;
    }
    copy(
        &temp_buffer_[size],
        &temp_buffer_[kNumSixOpVoices * size],
        &acc_buffer_[0]);
  } else {
    // Staggered rendering.
    copy(
        &acc_buffer_[0],
        &acc_buffer_[(kNumSixOpVoices - 1) * size],
        &temp_buffer_[0]);
    fill(
        &temp_buffer_[(kNumSixOpVoices - 1) * size],
        &temp_buffer_[kNumSixOpVoices * size],
        0.0f);
    rendered_voice_ = (rendered_voice_ + 1) % kNumSixOpVoices;
    voice_[rendered_voice_].Render(temp_buffer_, size * kNumSixOpVoices);

    for (size_t i = 0; i < size; ++i) {
      float sample = SoftClip(temp_buffer_[i] * 0.25f);
      if (parameters.macro < 0.5f) {
        const float darkness = (0.5f - parameters.macro) * 2.0f;
        const float coefficient = 1.0f - darkness * 0.92f;
        ONE_POLE(post_filter_, sample, coefficient);
        sample = post_filter_;
      } else {
        post_filter_ = sample;
        const float saturation = (parameters.macro - 0.5f) * 2.0f;
        const float saturated = SoftClip(sample * 3.0f);
        sample += (saturated - sample) * saturation;
      }
      aux[i] = out[i] = sample;
    }
    copy(
        &temp_buffer_[size],
        &temp_buffer_[kNumSixOpVoices * size],
        &acc_buffer_[0]);
  }
}

}  // namespace plaits
