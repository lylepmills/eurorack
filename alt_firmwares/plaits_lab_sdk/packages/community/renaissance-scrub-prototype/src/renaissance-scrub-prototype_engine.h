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

#ifndef PLAITS_LAB_RENAISSANCE_SCRUB_PROTOTYPE_ENGINE_H_
#define PLAITS_LAB_RENAISSANCE_SCRUB_PROTOTYPE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/speech/lpc_speech_synth.h"

namespace plaits {

// Continuous-LPC word playback with Renaissance's control split. HARMONICS
// selects a word, TIMBRE seeks within it, and a trigger plays forward from
// that point. With no trigger, the selected frame is held. MORPH shifts the
// formants and MACRO controls captured pitch prosody around a flat midpoint.
class RenaissanceScrubPrototypeEngine : public Engine {
 public:
  RenaissanceScrubPrototypeEngine() { }
  ~RenaissanceScrubPrototypeEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  void LoadWord(int word);

  LPCSpeechSynth synth_;
  const LPCSpeechSynth::Frame* frames_;
  int word_;
  int num_frames_;
  int playback_frame_;
  size_t remaining_frame_samples_;

  float clock_phase_;
  float sample_[2];
  float next_sample_[2];

  DISALLOW_COPY_AND_ASSIGN(RenaissanceScrubPrototypeEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_RENAISSANCE_SCRUB_PROTOTYPE_ENGINE_H_
