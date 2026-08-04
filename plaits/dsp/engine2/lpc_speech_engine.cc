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
  lpc_speech_synth_word_bank_.Init(
      word_banks_,
      LPC_SPEECH_SYNTH_NUM_WORD_BANKS,
      allocator);
  lpc_speech_synth_controller_.Init(&lpc_speech_synth_word_bank_);
  word_bank_quantizer_.Init(LPC_SPEECH_SYNTH_NUM_WORD_BANKS + 1, 0.1f, false);
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
  // Six evenly addressable materials: interpolated phonemes, then the five
  // stock word banks. The 1.1 range is the stock Speech quantizer's full input
  // span after its model crossfade has ended.
  const int word_bank = word_bank_quantizer_.Process(
      parameters.harmonics * 1.1f) - 1;
  const bool free_running = parameters.trigger & TRIGGER_UNPATCHED;
  const bool trigger = parameters.trigger & TRIGGER_RISING_EDGE;
  const bool replay_prosody = word_bank >= 0 && !free_running;
  *already_enveloped = replay_prosody;

  // MORPH's midpoint is the stock playback speed. MACRO directly controls the
  // vocal-tract/formant shift that stock Speech hides on TIMBRE. Recorded
  // prosody stays suppressed so pitched playback follows the played note.
  const float speed = (parameters.morph - 0.5f) * 2.0f;
  lpc_speech_synth_controller_.Render(
      free_running,
      trigger,
      word_bank,
      NoteToFrequency(parameters.note),
      0.0f,
      speed,
      parameters.timbre,
      parameters.macro,
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
