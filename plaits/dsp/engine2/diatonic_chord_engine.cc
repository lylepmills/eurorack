// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids Renaissance's five CHORD_* models merged into one slot.

#include "plaits/dsp/engine2/diatonic_chord_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Renaissance's `diatonic_chords`, read as absolute scale degrees above the
// root (see the header for why). Column 0 is the number of extension degrees
// that follow; the third and fifth are implicit and come from kThirdDegree.
const int8_t kChordExtensions[kDiatonicChordNumChords][
    kDiatonicChordMaxExtensions + 1] = {
  { 1, 7, 0, 0 },    // octave
  { 2, 5, 7, 0 },    // octave add6
  { 1, 6, 0, 0 },    // seventh
  { 2, 5, 6, 0 },    // seventh add6
  { 2, 6, 8, 0 },    // ninth
  { 3, 6, 8, 10 },   // eleventh
  { 3, 5, 7, 10 },   // eleventh add6
  { 1, 8, 0, 0 },    // add9
  { 2, 6, 10, 0 },   // seventh add11
  { 2, 6, 12, 0 },   // seventh add13
  { 1, 7, 0, 0 },    // octave sus4
  { 1, 6, 0, 0 },    // seventh sus4
  { 2, 6, 8, 0 },    // ninth sus4
  { 2, 6, 8, 0 },    // ninth sus4, wider
  { 3, 8, 10, 6 },   // eleventh sus4
  { 0, 0, 0, 0 },    // sus2 triad
};

// The degree standing in for the third. Renaissance switches it by chord
// index: a third for the first eleven, a fourth for the sus4 block, a second
// for the last.
inline int ThirdDegree(int chord) {
  if (chord < 11) {
    return 2;
  } else if (chord < 15) {
    return 3;
  } else {
    return 1;
  }
}

const int kFifthDegree = 4;

}  // namespace

void DiatonicChordEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  voices_.Init();
}

void DiatonicChordEngine::Reset() {
  voices_.Reset();
}

void DiatonicChordEngine::Render(
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

  const float waveform = parameters.morph;
  const float detune = parameters.timbre * kScaleVoicesMaxDetuneCents;
  const float fold = parameters.timbre;

  voices_.Render(
      notes, num_voices, waveform, detune, fold, out, aux, size);
}

}  // namespace plaits
