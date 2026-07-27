// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// Diagnostic copy of StringEngine (inharmonic-string) with per-section cycle
// bracketing. See the .cc for why this exists.

#ifndef PLAITS_LAB_STRING_AUTOPSY_ENGINE_H_
#define PLAITS_LAB_STRING_AUTOPSY_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/physical_modelling/string_voice.h"

namespace plaits {

const int kAutopsyStrings = 3;

class StringAutopsyEngine : public Engine {
 public:
  StringAutopsyEngine() { }
  ~StringAutopsyEngine() { }
  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters, float* out,
      float* aux, size_t size, bool* already_enveloped);

 private:
  StringVoice voice_[kAutopsyStrings];
  float f0_[kAutopsyStrings];
  DelayLine<float, 16> f0_delay_;
  int active_string_;
  float* temp_buffer_;
  DISALLOW_COPY_AND_ASSIGN(StringAutopsyEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_STRING_AUTOPSY_ENGINE_H_
