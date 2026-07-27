// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// WHY THIS FILE EXISTS. The stock inharmonic-string engine measured 6080
// cycles/sample on hardware -- 405% of the audio budget and ~4x what its
// emulated instruction count predicts -- and SEVEN candidate explanations were
// tested and refuted (pitch, memory traffic, denormals, state build-up, divide
// count, trigger state, note-wobble strumming). This is a byte-faithful copy of
// that engine (mono path) with cycle brackets around each sub-part, so the
// hardware itself reports WHERE the cycles go instead of an analyst guessing
// why. Sections: 0 = whole engine render, 1..3 = the three string voices.
// Voice-level overhead = the probe's total minus section 0; engine misc =
// section 0 minus the voices.
//
// The brackets compile to nothing unless the build defines PLAITS_CPU_PROBE
// (a --cpu-probe hardware build), so `check` and preview builds are unaffected.

#include "string_autopsy_engine.h"

#include <algorithm>

#if defined(PLAITS_CPU_PROBE) && PLAITS_CPU_PROBE
extern "C" void plaits_cpu_probe_section(int index, int begin);
#define AUTOPSY_BEGIN(i) plaits_cpu_probe_section(i, 1)
#define AUTOPSY_END(i) plaits_cpu_probe_section(i, 0)
#else
#define AUTOPSY_BEGIN(i)
#define AUTOPSY_END(i)
#endif

namespace plaits {

using namespace std;
using namespace stmlib;

void StringAutopsyEngine::Init(BufferAllocator* allocator) {
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
  for (int i = 0; i < kAutopsyStrings; ++i) {
    voice_[i].Init(allocator);
    f0_[i] = 0.01f;
  }
  active_string_ = kAutopsyStrings - 1;
  f0_delay_.Init(allocator->Allocate<float>(16));
}

void StringAutopsyEngine::Reset() {
  f0_delay_.Reset();
  for (int i = 0; i < kAutopsyStrings; ++i) {
    voice_[i].Reset();
  }
}

void StringAutopsyEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  AUTOPSY_BEGIN(0);
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    f0_[active_string_] = f0_delay_.Read(14);
    active_string_ = (active_string_ + 1) % kAutopsyStrings;
  }

  const float f0 = NoteToFrequency(parameters.note);
  f0_[active_string_] = f0;
  f0_delay_.Write(f0);

  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);
  const float exciter_size = ApplyMacro(
      1.0f, 0.25f, 4.0f, parameters.macro);

  for (int i = 0; i < kAutopsyStrings; ++i) {
    AUTOPSY_BEGIN(1 + i);
    voice_[i].Render(
        parameters.trigger & TRIGGER_UNPATCHED && i == active_string_,
        parameters.trigger & TRIGGER_RISING_EDGE && i == active_string_,
        parameters.accent,
        f0_[i],
        parameters.harmonics,
        parameters.timbre * parameters.timbre,
        parameters.morph,
        exciter_size,
        temp_buffer_,
        out,
        aux,
        size);
    AUTOPSY_END(1 + i);
  }
  AUTOPSY_END(0);
}

}  // namespace plaits
