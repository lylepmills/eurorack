// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

// Braids Renaissance's dedicated WTx6 engine. Kept independent of Scale Stack
// so the classical engine retains its exact code and flash footprint.

#include "plaits/dsp/engine2/wavetable_scale_stack_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

#include "plaits/build_config.h"
#include "plaits/dsp/engine2/scale_stack_engine.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void WavetableScaleStackEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  voices_.Init();
}

void WavetableScaleStackEngine::Reset() {
  voices_.Reset();
}

void WavetableScaleStackEngine::Render(
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

  const float detune = parameters.timbre * kScaleVoicesMaxDetuneCents;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  if (parameters.frequency_offset) {
    voices_.RenderWavetableFrequencyOffset(
        notes,
        kScaleStackNumVoices,
        parameters.morph,
        detune,
        parameters.frequency_offset,
        out,
        aux,
        size);
    return;
  }
#endif
  voices_.RenderWavetable(
      notes, kScaleStackNumVoices, parameters.morph, detune, out, aux, size);
}

}  // namespace plaits
