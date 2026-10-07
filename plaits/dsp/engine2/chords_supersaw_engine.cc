// Copyright 2026 Dirk Hoppmann.
// SPDX-License-Identifier: MIT
//
// Modified from Chords (mutable-instruments/chords@1.0.0) for Plaits Lab.
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
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
// -----------------------------------------------------------------------------
//
// Supersaw Chords: chord bank and inversion driving detuned saw unison stacks.

#include "plaits/dsp/engine2/chords_supersaw_engine.h"

#include <algorithm>

#include "plaits/dsp/dsp.h"
#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

// MORPH at full travel spreads neighbouring unison ranks this far apart, in
// semitones. The outermost ranks sit three steps from the centre, so the full
// stack spans a little over two semitones at the top of the knob.
const float kSupersawMaxDetune = 0.35f;

// Outer ranks are pulled down slightly so the stack keeps a centre of pitch
// instead of reading as a cluster of equals.
const float kSupersawRankTilt = 0.12f;

// The waveform is fixed rather than on a control. The sine and square paths
// cost about one oscillator per chord tone more than the plain ramp, and on the
// module that was the difference between fitting and blinking red. To fix it
// somewhere else, name another shape and position here -- SINE_TO_SAW at 0.0 is
// a sine, SAW_TO_SQUARE at 1.0 a square -- and measure again before flashing.
const SupersawShape kSupersawFixedShape = SUPERSAW_SHAPE_SAW;
const float kSupersawFixedShapeAmount = 0.0f;

// Which detune positions a stack of 1, 2 or 3 uses. One oscillator plays the
// centre clean; two take the outer pair, the widest a pair can be; three take
// all of them.
const int kStackPositions[kSupersawUnison + 1][kSupersawUnison] = {
  { 1, 1, 1 },
  { 1, 0, 0 },
  { 0, 2, 0 },
  { 0, 1, 2 },
};

// TIMBRE drives the mix into stmlib's soft limiter.
//
// The makeup curve is measured, not derived. How hard the limiter compresses
// depends on the signal reaching it as much as on the drive setting, so no
// reciprocal of the drive holds the level flat. These nine points are the
// gains that returned the model's RMS to its undriven value, read off a sweep
// with makeup disabled, and they are interpolated per block. The calibration
// was taken at one chord with the full stack, so the level holds to about a
// decibel elsewhere rather than exactly.
const float kSupersawMaxDrive = 5.0f;
const int kSupersawMakeupPoints = 9;
const float kSupersawMakeup[kSupersawMakeupPoints] = {
  1.0000f, 0.7026f, 0.5539f, 0.4746f, 0.4264f,
  0.3944f, 0.3718f, 0.3552f, 0.3425f
};



const float kSupersawLevel = 0.2f;

// Where each oscillator of a chord tone sits in the spread, as a fraction of
// what MORPH is currently asking for. Two things are deliberately uneven here.
//
// Within a tone the two outer positions are not mirror images, so each beats
// against the centre at its own rate instead of the pair sounding as one
// chorus. Across tones the patterns differ and share no small whole-number
// ratios, so the four notes of the chord never come back into step with one
// another -- an evenly spread stack pulses, this one shimmers.
//
// The root (note 0) is held tighter than the rest: spread costs the most
// definition down there and buys the least width.
//
// Column 1 is the centre of each tone, columns 0 and 2 its outer pair. A fourth
// column is kept for a larger stack; kStackPositions picks the ones in use.
const float kDetuneSpread[kChordNumNotes][4] = {
  { -0.62f, 0.0f, 0.45f, 0.88f },
  { -0.85f, 0.0f, 1.00f, -0.46f },
  { -1.00f, 0.0f, 0.62f, 0.31f },
  { -0.72f, 0.0f, 0.94f, -0.35f },
};


// 1 / sqrt(n) for n = 0..7, spelled out so the engine needs no math header.
// Indexed by the live stack size, so the level holds as MACRO changes it.
const float kInverseRoot[8] = {
  0.0f, 1.0f, 0.70710678f, 0.57735027f,
  0.5f, 0.4472136f, 0.40824829f, 0.37796447f
};

void ChordsSupersawEngine::Init(BufferAllocator* allocator) {
  for (int note = 0; note < kChordNumNotes; ++note) {
    for (int rank = 0; rank < kSupersawUnison; ++rank) {
      voice_[note][rank].Init();
    }
  }
  chords_.Init(allocator);

  morph_lp_ = 0.0f;
  timbre_lp_ = 0.0f;
  active_unison_ = kSupersawUnison;

  ScatterPhases();
}

void ChordsSupersawEngine::Reset() {
  chords_.Reset();
}

// Detuned saws that all start at phase zero sum into one loud impulse and only
// drift apart afterwards. Scattering the phases gives the stack its width from
// the first sample of the attack.
//
// The scatter is a fixed table rather than random so that a restart is
// repeatable: the sync fallback restarts the engine through this same trigger
// path on every sync edge, and random phases there never lock to the master.
const float kScatter[kChordNumNotes][kSupersawUnison] = {
  { 0.00f, 0.41f, 0.77f }, { 0.23f, 0.62f, 0.09f },
  { 0.53f, 0.88f, 0.31f }, { 0.71f, 0.17f, 0.46f },
};

void ChordsSupersawEngine::ScatterPhases() {
  for (int note = 0; note < kChordNumNotes; ++note) {
    for (int rank = 0; rank < kSupersawUnison; ++rank) {
      voice_[note][rank].set_phase(kScatter[note][rank]);
    }
  }
}

void ChordsSupersawEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    ScatterPhases();
  }

  ONE_POLE(morph_lp_, parameters.morph, 0.1f);
  ONE_POLE(timbre_lp_, parameters.timbre, 0.1f);

  chords_.set_chord(parameters.harmonics, parameters.chord_set_option);

  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);

  const bool stereo = PLAITS_STEREO_CHORDS_SUPERSAW && parameters.stereo;
  float center_samples[kMaxBlockSize];
  if (stereo) {
    fill(&center_samples[0], &center_samples[size], 0.0f);
  }

  const float f0 = NoteToFrequency(parameters.note) * 0.998f;
  // MORPH: the detune spread, squared so the first part of the travel covers
  // slow beating and the top reaches a full supersaw.
  const float detune_semitones = morph_lp_ * morph_lp_ * kSupersawMaxDetune;

  // MACRO: the stack size, one to three oscillators per chord tone. It is set
  // and left rather than played, and it is also the way to buy CPU back on the
  // module if a patch pushes the meter up.
  int unison =
      1 + static_cast<int>(parameters.macro * (kSupersawUnison - 0.001f));
  CONSTRAIN(unison, 1, kSupersawUnison);
  // A rank that has just been dialled away is rendered once more at zero gain,
  // so its own interpolator ramps it down across the block instead of cutting.
  const int rendered_ranks = unison > active_unison_ ? unison : active_unison_;
  active_unison_ = unison;

  // Detuned saws drift out of phase with one another, so the stack sums
  // incoherently and grows like sqrt(n) rather than n.
  const float unison_gain = kSupersawLevel * kInverseRoot[unison];

  // No inversion: the chord is voiced in root position and the fifth
  // oscillator slot the crossfade needed is gone entirely. That slot and the
  // partial-amplitude corner it created were the model's worst case, so
  // dropping it buys more saws than it costs in voicing.
  for (int note = 0; note < kChordNumNotes; ++note) {
    const float note_f0 = f0 * chords_.ratio(note);
    // The root goes to AUX and is added back into OUT below, so OUT carries
    // the whole chord and AUX carries a usable root voice.
    float* mono_destination = note == 0 ? aux : out;

    for (int rank = 0; rank < rendered_ranks; ++rank) {
      const float offset =
          kDetuneSpread[note][kStackPositions[unison][rank]];
      const float frequency =
          note_f0 * SemitonesToRatio(offset * detune_semitones);
      const float distance = offset < 0.0f ? -offset : offset;
      const float rank_gain = 1.0f - kSupersawRankTilt * distance;

      float* destination;
      if (stereo) {
        // The detune spread becomes the image: ranks below the centre pitch
        // go left, ranks above go right, and the centre rank feeds both.
        destination = offset < 0.0f
            ? out
            : (offset > 0.0f ? aux : center_samples);
      } else {
        destination = mono_destination;
      }

      voice_[note][rank].Render(
          kSupersawFixedShape,
          kSupersawFixedShapeAmount,
          frequency,
          rank < unison ? unison_gain * rank_gain : 0.0f,
          destination,
          size);
    }
  }

  if (stereo) {
    // The same near/far crossfeed the stock stereo path uses, so a hard-panned
    // stack still reads as one instrument rather than two.
    for (size_t i = 0; i < size; ++i) {
      const float left_group = out[i];
      const float right_group = aux[i];
      const float center = center_samples[i] * 0.707106769f;
      out[i] = left_group * 0.935f + right_group * 0.335f + center;
      aux[i] = right_group * 0.935f + left_group * 0.335f + center;
    }
  } else {
    for (size_t i = 0; i < size; ++i) {
      const float selected = aux[i];
      out[i] += selected;
      aux[i] = selected * 3.0f;
    }
  }

  // TIMBRE: drive into the soft limiter. It compresses rather than folding
  // above its knee, so the stack thickens and the detune starts beating
  // against itself instead of just sitting side by side.
  if (timbre_lp_ > 0.0f) {
    const float drive = 1.0f + timbre_lp_ * kSupersawMaxDrive;
    float position = timbre_lp_ * (kSupersawMakeupPoints - 1.001f);
    MAKE_INTEGRAL_FRACTIONAL(position);
    const float a = kSupersawMakeup[position_integral];
    const float b = kSupersawMakeup[position_integral + 1];
    const float makeup = a + (b - a) * position_fractional;
    for (size_t i = 0; i < size; ++i) {
      out[i] = SoftLimit(out[i] * drive) * makeup;
      aux[i] = SoftLimit(aux[i] * drive) * makeup;
    }
  }

}

}  // namespace plaits
