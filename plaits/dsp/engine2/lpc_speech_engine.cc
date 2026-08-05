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

#include "plaits/dsp/engine2/lpc_speech_engine.h"

#include "plaits/dsp/speech/lpc_speech_synth_words.h"

namespace plaits {

using namespace stmlib;

void LPCSpeechEngine::Init(BufferAllocator* allocator) {
#if PLAITS_HAS_CUSTOM_SPEECH_BANKS
  InitRecipeSpeechWordBank(&lpc_speech_synth_word_bank_, allocator);
#else
  lpc_speech_synth_word_bank_.Init(
      word_banks_,
      LPC_SPEECH_SYNTH_NUM_WORD_BANKS,
      allocator);
#endif
  lpc_speech_synth_controller_.Init(&lpc_speech_synth_word_bank_);
#if PLAITS_HAS_CUSTOM_SPEECH_BANKS
  word_bank_quantizer_.Init(RecipeSpeechWordBankCount(), 0.1f, false);
#else
  word_bank_quantizer_.Init(LPC_SPEECH_SYNTH_NUM_WORD_BANKS, 0.1f, false);
#endif
  prosody_amount_ = 0.0f;
}

void LPCSpeechEngine::Reset() {
  lpc_speech_synth_word_bank_.Reset();
}

void LPCSpeechEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  // The phoneme position moved to Speech Sounds, leaving the recipe's selected
  // stock and custom word banks evenly addressable across HARMONICS.
  const int word_bank = word_bank_quantizer_.Process(parameters.harmonics);
  const bool free_running = parameters.trigger & TRIGGER_UNPATCHED;
  const bool trigger = parameters.trigger & TRIGGER_RISING_EDGE;
  const bool replay_prosody = word_bank >= 0 && !free_running;
  *already_enveloped = replay_prosody;

  // Preserve the stock LPC controls: TIMBRE shifts the vocal tract and MORPH
  // selects the word/address. The dedicated engine exposes the playback speed
  // that stock Speech hides on an attenuverter through MACRO instead, with the
  // original recording speed at noon. Voice supplies prosody from the
  // unpatched FM attenuverter.
  const float speed = (parameters.macro - 0.5f) * 2.0f;
  lpc_speech_synth_controller_.Render(
      free_running,
      trigger,
      word_bank,
      NoteToFrequency(parameters.note),
      prosody_amount_,
      speed,
      parameters.morph,
      parameters.timbre,
      replay_prosody ? parameters.accent : 1.0f,
      aux,
      out,
      size);

  if (PLAITS_STEREO_LPC_SPEECH && parameters.stereo) {
    float voice_l, voice_r, secondary_l, secondary_r;
    StereoPanGains(0.4f, &voice_l, &voice_r);
    StereoPanGains(0.6f, &secondary_l, &secondary_r);
    for (size_t i = 0; i < size; ++i) {
      const float voice = out[i];
      const float secondary = aux[i];
      out[i] = voice * voice_l + secondary * secondary_l;
      aux[i] = voice * voice_r + secondary * secondary_r;
    }
  }
}

}  // namespace plaits
