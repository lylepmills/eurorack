// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids Renaissance's five STACK_* models merged into one slot.

#include "plaits/dsp/engine2/scale_stack_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void ScaleStackEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  voices_.Init();
}

void ScaleStackEngine::Reset() {
  voices_.Reset();
}

void ScaleStackEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    voices_.Reset();
  }

  int scale = static_cast<int>(parameters.macro * kScaleVoicesNumScales);
  CONSTRAIN(scale, 0, kScaleVoicesNumScales - 1);

  int span = 1 + static_cast<int>(parameters.harmonics * kScaleStackMaxSpan);
  CONSTRAIN(span, 1, kScaleStackMaxSpan);

  float residual = 0.0f;
  const int root_degree = QuantizeToScale(parameters.note, scale, &residual);

  float notes[kScaleVoicesMaxVoices];
  notes[0] = parameters.note;
  for (int v = 1; v < kScaleStackNumVoices; ++v) {
    int degree = v * span;
    CONSTRAIN(degree, 0, kScaleVoicesMaxDegreeOffset);
    notes[v] = ScaleDegreeToNote(root_degree + degree, scale) + residual;
  }

  const float waveform = parameters.morph;
  const float detune = parameters.timbre * kScaleVoicesMaxDetuneCents;
  const float fold = parameters.timbre;

  voices_.Render(
      notes, kScaleStackNumVoices, waveform, detune, fold, out, aux, size);
}

}  // namespace plaits
