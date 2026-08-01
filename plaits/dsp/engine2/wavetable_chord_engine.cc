// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

// Braids Renaissance's dedicated WTCH engine. Kept independent of Diatonic
// Chord so the classical engine retains its exact code and flash footprint.

#include "plaits/dsp/engine2/wavetable_chord_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/engine2/diatonic_chord_engine.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const int8_t kChordExtensions[kDiatonicChordNumChords][
    kDiatonicChordMaxExtensions + 1] = {
  { 1, 7, 0, 0 }, { 2, 5, 7, 0 }, { 1, 6, 0, 0 }, { 2, 5, 6, 0 },
  { 2, 6, 8, 0 }, { 3, 6, 8, 10 }, { 3, 5, 7, 10 }, { 1, 8, 0, 0 },
  { 2, 6, 10, 0 }, { 2, 6, 12, 0 }, { 1, 7, 0, 0 }, { 1, 6, 0, 0 },
  { 2, 6, 8, 0 }, { 2, 6, 8, 0 }, { 3, 8, 10, 6 }, { 0, 0, 0, 0 },
};

inline int ThirdDegree(int chord) {
  return chord < 11 ? 2 : (chord < 15 ? 3 : 1);
}

const int kFifthDegree = 4;

}  // namespace

void WavetableChordEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  voices_.Init();
}

void WavetableChordEngine::Reset() {
  voices_.Reset();
}

void WavetableChordEngine::Render(
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
  int chord = static_cast<int>(
      parameters.harmonics * kDiatonicChordNumChords);
  CONSTRAIN(chord, 0, kDiatonicChordNumChords - 1);

  float residual = 0.0f;
  const int root_degree = QuantizeToScale(parameters.note, scale, &residual);
  float notes[kScaleVoicesMaxVoices];
  int num_voices = 0;
  notes[num_voices++] = parameters.note;
  notes[num_voices++] = ScaleDegreeToNote(
      root_degree + ThirdDegree(chord), scale) + residual;
  notes[num_voices++] = ScaleDegreeToNote(
      root_degree + kFifthDegree, scale) + residual;
  const int num_extensions = kChordExtensions[chord][0];
  for (int i = 0; i < num_extensions; ++i) {
    int degree = kChordExtensions[chord][1 + i];
    CONSTRAIN(degree, 0, kScaleVoicesMaxDegreeOffset);
    notes[num_voices++] = ScaleDegreeToNote(
        root_degree + degree, scale) + residual;
  }

  const float detune = parameters.timbre * kScaleVoicesMaxDetuneCents;
  voices_.RenderWavetable(
      notes, num_voices, parameters.morph, detune, out, aux, size);
}

}  // namespace plaits
