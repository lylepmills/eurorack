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

// All Six-Op pan positions are fixed. Keeping their equal-power gains as
// float constants removes four VSQRTs from every audio block without changing
// the panning law or its rounded float results.
const float kSixOpCenterPan = 0.707106781f;  // sqrt(0.5)
const float kSixOpPanLeft[kNumSixOpVoices] = {
  0.894427191f,  // sqrt(0.8)
  0.447213595f,  // sqrt(0.2)
};
const float kSixOpPanRight[kNumSixOpVoices] = {
  0.447213595f,  // sqrt(0.2)
  0.894427191f,  // sqrt(0.8)
};

void SixOpEngine::Init(BufferAllocator* allocator) {
  patch_index_quantizer_.Init(32, 0.005f, false);

  algorithms_.Init();
  for (int i = 0; i < kNumSixOpVoices; ++i) {
    voice_[i].Init(&algorithms_, kCorrectedSampleRate);
  }
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize * 4);
  acc_buffer_ = allocator->Allocate<float>(kMaxBlockSize * kNumSixOpVoices);
  patches_ = allocator->Allocate<fm::Patch>(kNumPatchesPerBank);
  num_patches_ = kNumPatchesPerBank;

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
  // A bare load (no explicit length) is a full 32-patch bank — the shape of a
  // stock DX7 .syx and of the runtime TIMBRE-loaded user bank. Behaviour is
  // byte-identical to the original fixed-32 path.
  LoadUserData(user_data, kNumPatchesPerBank * fm::Patch::SYX_SIZE);
}

void SixOpEngine::LoadUserData(const uint8_t* user_data, size_t length) {
  // The shipped firmware always supplies a real patch bank, but the SDK preview
  // and the reference-render path call LoadUserData(NULL) for engines with no
  // bank; unpacking a null pointer dereferences it. Guard the unpack and still
  // reset the voices so a null load leaves a well-defined (default) patch set.
  //
  // `length` is the count of valid packed bytes; length / SYX_SIZE patches are
  // actually present (a recipe may bake fewer than 32). Re-size the Harmonics
  // patch-index quantizer to that count so the dial addresses only the real
  // patches — the block-fill zone-spreading the web builder used to do to reach
  // 32 becomes unnecessary. A null load keeps the full 32-step quantizer.
  int n = user_data
      ? static_cast<int>(length / fm::Patch::SYX_SIZE)
      : kNumPatchesPerBank;
  if (n < 1) {
    n = 1;
  } else if (n > kNumPatchesPerBank) {
    n = kNumPatchesPerBank;
  }
  num_patches_ = n;
  patch_index_quantizer_.Init(n, 0.005f, false);

  if (user_data) {
    for (int i = 0; i < n; ++i) {
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

    // A free-running drone has exactly one sustained voice. Keeping the second
    // voice loaded and advancing its silent envelopes wasted half of the FM
    // render budget, which is enough to overrun on expensive DX7 algorithms.
    voice_[0].LoadPatch(&patches_[patch_index]);
    Voice<6>::Parameters* p = voice_[0].mutable_parameters();
    p->sustain = true;
    p->gate = false;
    p->note = parameters.note;
    p->velocity = parameters.accent;
    p->brightness = parameters.timbre;
    p->envelope_control = t;
    p->modulator_detune = modulator_detune;
    voice_[0].set_modulations(voice_[0].lfo());
    voice_[1].UnloadPatch();
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

  if (parameters.trigger & TRIGGER_UNPATCHED) {
    // Render the single sustained voice at the native block size. The old
    // staggered path rendered 2 * size samples every other block while also
    // spending the intervening block rendering a silent voice. This produces
    // the same continuous oscillator stream without the silent work.
    fill(&temp_buffer_[0], &temp_buffer_[size], 0.0f);
    voice_[0].Render(temp_buffer_, size);
    fill(&acc_buffer_[0], &acc_buffer_[(kNumSixOpVoices - 1) * size], 0.0f);
    rendered_voice_ = 0;

    const float output_gain = 0.25f *
        ((PLAITS_STEREO_SIX_OP && parameters.stereo)
            ? kSixOpCenterPan
            : 1.0f);
    const float macro = parameters.macro;
    const float darkness = (0.5f - macro) * 2.0f;
    const float coefficient = 1.0f - darkness * 0.92f;
    const float saturation = (macro - 0.5f) * 2.0f;
    if (macro < 0.5f) {
      for (size_t i = 0; i < size; ++i) {
        float sample = SoftClip(temp_buffer_[i] * output_gain);
        ONE_POLE(post_filter_, sample, coefficient);
        sample = post_filter_;
        aux[i] = out[i] = sample;
      }
    } else if (macro > 0.5f) {
      for (size_t i = 0; i < size; ++i) {
        float sample = SoftClip(temp_buffer_[i] * output_gain);
        post_filter_ = sample;
        sample += (SoftLimit(sample * 3.0f) - sample) * saturation;
        aux[i] = out[i] = sample;
      }
    } else {
      for (size_t i = 0; i < size; ++i) {
        const float sample = SoftClip(temp_buffer_[i] * output_gain);
        post_filter_ = sample;
        aux[i] = out[i] = sample;
      }
    }
    post_filter_right_ = post_filter_;
  } else if ((PLAITS_STEREO_SIX_OP && parameters.stereo)) {
    // Staggered rendering, split by voice: the accumulation buffer always
    // holds the tail of the single voice rendered on the previous block, so
    // per-voice pan gains can be applied when the two halves are combined.
    const int previous_voice = rendered_voice_;
    fill(&temp_buffer_[0], &temp_buffer_[kNumSixOpVoices * size], 0.0f);
    rendered_voice_ = (rendered_voice_ + 1) % kNumSixOpVoices;
    voice_[rendered_voice_].Render(temp_buffer_, size * kNumSixOpVoices);

    // A free-running drone sustains a single voice: keep it centred rather
    // than parked on one side. With a trigger patched, the round-robin voice
    // allocation makes successive notes alternate between the two sides.
    const bool unpatched = parameters.trigger & TRIGGER_UNPATCHED;
    const float previous_left_gain = unpatched
        ? kSixOpCenterPan
        : kSixOpPanLeft[previous_voice];
    const float previous_right_gain = unpatched
        ? kSixOpCenterPan
        : kSixOpPanRight[previous_voice];
    const float current_left_gain = unpatched
        ? kSixOpCenterPan
        : kSixOpPanLeft[rendered_voice_];
    const float current_right_gain = unpatched
        ? kSixOpCenterPan
        : kSixOpPanRight[rendered_voice_];
    const float macro = parameters.macro;
    const float darkness = (0.5f - macro) * 2.0f;
    const float coefficient = 1.0f - darkness * 0.92f;
    const float saturation = (macro - 0.5f) * 2.0f;
    if (macro < 0.5f) {
      for (size_t i = 0; i < size; ++i) {
        const float previous = acc_buffer_[i] * 0.25f;
        const float current = temp_buffer_[i] * 0.25f;
        float left = SoftClip(
            previous * previous_left_gain + current * current_left_gain);
        float right = SoftClip(
            previous * previous_right_gain + current * current_right_gain);
        ONE_POLE(post_filter_, left, coefficient);
        left = post_filter_;
        ONE_POLE(post_filter_right_, right, coefficient);
        right = post_filter_right_;
        out[i] = left;
        aux[i] = right;
      }
    } else if (macro > 0.5f) {
      for (size_t i = 0; i < size; ++i) {
        const float previous = acc_buffer_[i] * 0.25f;
        const float current = temp_buffer_[i] * 0.25f;
        float left = SoftClip(
            previous * previous_left_gain + current * current_left_gain);
        float right = SoftClip(
            previous * previous_right_gain + current * current_right_gain);
        post_filter_ = left;
        post_filter_right_ = right;
        // The first SoftClip bounds each channel to [-1, 1], so the driven
        // sample is already inside SoftClip's rational section. Calling
        // SoftLimit directly removes redundant range checks without changing
        // the transfer function.
        left += (SoftLimit(left * 3.0f) - left) * saturation;
        right += (SoftLimit(right * 3.0f) - right) * saturation;
        out[i] = left;
        aux[i] = right;
      }
    } else {
      // At the neutral MACRO position saturation is exactly zero. Avoid doing
      // two additional soft clips per sample only to crossfade them away.
      for (size_t i = 0; i < size; ++i) {
        const float previous = acc_buffer_[i] * 0.25f;
        const float current = temp_buffer_[i] * 0.25f;
        const float left = SoftClip(
            previous * previous_left_gain + current * current_left_gain);
        const float right = SoftClip(
            previous * previous_right_gain + current * current_right_gain);
        post_filter_ = left;
        post_filter_right_ = right;
        out[i] = left;
        aux[i] = right;
      }
    }
    // std::copy lowers to a generic memmove in GCC 4.8.3. This fixed, small
    // transfer is cheaper as a plain loop and has identical copy semantics
    // because these buffers never overlap.
    for (size_t i = 0; i < (kNumSixOpVoices - 1) * size; ++i) {
      acc_buffer_[i] = temp_buffer_[size + i];
    }
  } else {
    // Staggered rendering.
    for (size_t i = 0; i < (kNumSixOpVoices - 1) * size; ++i) {
      temp_buffer_[i] = acc_buffer_[i];
    }
    fill(
        &temp_buffer_[(kNumSixOpVoices - 1) * size],
        &temp_buffer_[kNumSixOpVoices * size],
        0.0f);
    rendered_voice_ = (rendered_voice_ + 1) % kNumSixOpVoices;
    voice_[rendered_voice_].Render(temp_buffer_, size * kNumSixOpVoices);

    const float macro = parameters.macro;
    const float darkness = (0.5f - macro) * 2.0f;
    const float coefficient = 1.0f - darkness * 0.92f;
    const float saturation = (macro - 0.5f) * 2.0f;
    if (macro < 0.5f) {
      for (size_t i = 0; i < size; ++i) {
        float sample = SoftClip(temp_buffer_[i] * 0.25f);
        ONE_POLE(post_filter_, sample, coefficient);
        sample = post_filter_;
        aux[i] = out[i] = sample;
      }
    } else if (macro > 0.5f) {
      for (size_t i = 0; i < size; ++i) {
        float sample = SoftClip(temp_buffer_[i] * 0.25f);
        post_filter_ = sample;
        const float saturated = SoftLimit(sample * 3.0f);
        sample += (saturated - sample) * saturation;
        aux[i] = out[i] = sample;
      }
    } else {
      for (size_t i = 0; i < size; ++i) {
        const float sample = SoftClip(temp_buffer_[i] * 0.25f);
        post_filter_ = sample;
        aux[i] = out[i] = sample;
      }
    }
    for (size_t i = 0; i < (kNumSixOpVoices - 1) * size; ++i) {
      acc_buffer_[i] = temp_buffer_[size + i];
    }
  }
}

}  // namespace plaits
