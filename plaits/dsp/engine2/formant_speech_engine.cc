// Copyright 2016 Emilie Gillet.
// Copyright 2026 Rubato Audio.
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

#include "plaits/dsp/engine2/formant_speech_engine.h"

#include "plaits/build_config.h"

namespace plaits {

using namespace stmlib;

void FormantSpeechEngine::Init(BufferAllocator* allocator) {
  naive_speech_synth_.Init();
  sam_speech_synth_.Init();
  // Phoneme scan mode never dereferences a word bank. Keeping this null avoids
  // linking or allocating the word-bank data that belongs to LPC Words.
  lpc_speech_synth_controller_.Init(NULL);
  temp_buffer_[0] = allocator->Allocate<float>(kMaxBlockSize);
  temp_buffer_[1] = allocator->Allocate<float>(kMaxBlockSize);
  post_filter_ = 0.0f;
}

void FormantSpeechEngine::Reset() {
  post_filter_ = 0.0f;
}

void FormantSpeechEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  if (parameters.frequency_offset) {
    EngineParameters sample_parameters = parameters;
    sample_parameters.frequency_offset = NULL;
    const float base_frequency = NoteToFrequency(parameters.note);
    for (size_t i = 0; i < size; ++i) {
      sample_parameters.note = NoteWithFrequencyOffset(
          parameters.note, base_frequency, parameters.frequency_offset[i]);
      sample_parameters.trigger = i == 0
          ? parameters.trigger
          : parameters.trigger & ~TRIGGER_RISING_EDGE;
      Render(
          sample_parameters, out + i, aux + i, 1, already_enveloped);
    }
    return;
  }
#endif
  *already_enveloped = false;

  const float f0 = NoteToFrequency(parameters.note);
  const bool trigger = parameters.trigger & TRIGGER_RISING_EDGE;

  // Expand stock Speech's group 0..2 across the whole dial: naive at the left
  // endpoint, SAM at noon, and continuously scanned LPC phonemes at the right.
  // The render order and interpolation below match the corresponding stock
  // branches exactly at every point.
  const float group = parameters.harmonics * 2.0f;
  float blend;
  if (group <= 1.0f) {
    naive_speech_synth_.Render(
        trigger,
        f0,
        parameters.morph,
        parameters.timbre,
        temp_buffer_[0],
        aux,
        out,
        size);
    blend = group;
  } else {
    lpc_speech_synth_controller_.Render(
        parameters.trigger & TRIGGER_UNPATCHED,
        trigger,
        -1,
        f0,
        0.0f,
        0.0f,
        parameters.morph,
        parameters.timbre,
        1.0f,
        aux,
        out,
        size);
    blend = 2.0f - group;
  }

  sam_speech_synth_.Render(
      trigger,
      f0,
      parameters.morph,
      parameters.timbre,
      temp_buffer_[0],
      temp_buffer_[1],
      size);

  blend *= blend * (3.0f - 2.0f * blend);
  blend *= blend * (3.0f - 2.0f * blend);
  for (size_t i = 0; i < size; ++i) {
    aux[i] += (temp_buffer_[0][i] - aux[i]) * blend;
    out[i] += (temp_buffer_[1][i] - out[i]) * blend;
  }

  if (PLAITS_STEREO_FORMANT_SPEECH && parameters.stereo) {
    float voice_l, voice_r, secondary_l, secondary_r;
    StereoPanGains(0.4f, &voice_l, &voice_r);
    StereoPanGains(0.6f, &secondary_l, &secondary_r);
    for (size_t i = 0; i < size; ++i) {
      const float voice = out[i];
      const float secondary = aux[i];
      out[i] = voice * voice_l + secondary * secondary_l;
      aux[i] = voice * voice_r + secondary * secondary_r;
    }
  } else {
    // Preserve Speech's useful fourth axis: excitation below noon, the stock
    // voice at noon, then progressively sharper formants above it.
    const float voice_amount = parameters.macro * 2.0f;
    const float spectral_sharpening = (parameters.macro - 0.5f) * 5.0f;
    for (size_t i = 0; i < size; ++i) {
      const float voice = out[i];
      ONE_POLE(post_filter_, voice, 0.12f);
      if (parameters.macro < 0.5f) {
        out[i] = aux[i] + (out[i] - aux[i]) * voice_amount;
      } else {
        out[i] = voice + (voice - post_filter_) * spectral_sharpening;
      }
    }
  }
}

}  // namespace plaits
