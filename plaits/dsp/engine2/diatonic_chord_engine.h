// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids Renaissance's five CHORD_* models merged into one slot.
//
// Upstream is DigitalOscillator::RenderDiatonicChord in Tom Burns' Braids
// Renaissance (boourns/eurorack-renaissance): a chord built by stacking SCALE
// DEGREES on the played note rather than by looking up an interval table. The
// five display models -- sine, triangle, saw, square and wavetable -- differ
// only in the waveform, so four of them collapse onto MORPH the same way
// `triple` collapses its four.
//
// WHY THIS IS NOT A DUPLICATE OF `chords`. Plaits' own chord engine reads a
// table of intervals in cents, and Plaits Palette lets you author that table --
// so a fixed chord shape transposed under the note is already reachable. What
// is not reachable is a chord whose QUALITY changes with the note: here the
// third is "two scale degrees up", so playing the tonic gives a major seventh
// and playing the sixth gives a minor seventh, from one knob position, with no
// table entry for either. That is the whole reason the model exists and the
// only thing that justifies the slot.
//
// THE WAVETABLE MODEL IS DROPPED, deliberately. Renaissance's fifth model scans
// Braids' `mini_wave_line` through `wt_waves`; Plaits ships its own `wavetable`
// engine over the same ground, Palette can load custom wavetables into it, and
// carrying Braids' wave data here would cost more flash than the other four
// models put together. Four models, one slot.
//
// ⚠️ UPSTREAM ARITHMETIC IS NOT REPRODUCED, because it does not match its own
// labels. Renaissance's `diatonic_chords` table reads as ABSOLUTE scale degrees
// -- row 4 is {2, 6, 8} and is labelled "9th", which is degrees 6 and 8, a
// seventh and a ninth; that is right. But renderChord consumes the array
// CUMULATIVELY (`index = index + noteOffset[i-1]`), which turns that row into
// degrees 2, 8 and 16 and puts the top voice more than two octaves up. Two
// independent things say absolute is what was meant: every label in the table
// only parses as absolute, and RenderStack pre-accumulates its own offsets into
// absolute values before handing them to the same function -- which the
// cumulative read then squares into span, 3*span, 6*span, 10*span. This port
// treats the offsets as absolute. Two further upstream defects fall out with
// it: `len = diatonic_chords[ext][0] + 3` reads up to index 6 of a 4-wide row,
// and writes up to offsets[7] in a 6-element array.
//
// Anyone wanting the shipped-Renaissance voicings rather than the intended ones
// is asking for a different engine, and it should be a different engine.

#ifndef PLAITS_DSP_ENGINE2_DIATONIC_CHORD_ENGINE_H_
#define PLAITS_DSP_ENGINE2_DIATONIC_CHORD_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine2/scale_voices.h"

namespace plaits {

const int kDiatonicChordNumChords = 16;

// Root, third and fifth, plus up to three extension degrees.
const int kDiatonicChordMaxExtensions = 3;

class DiatonicChordEngine : public Engine {
 public:
  DiatonicChordEngine() { }
  ~DiatonicChordEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: the chord against its own root.
  virtual bool stereo_capable() const { return true; }

 private:
  ScaleVoiceBank voices_;

  DISALLOW_COPY_AND_ASSIGN(DiatonicChordEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_DIATONIC_CHORD_ENGINE_H_
