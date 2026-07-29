// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SUB models: a shaped main oscillator against a square sub.

#include "plaits/dsp/engine2/sub_oscillator_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void SubOscillatorEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  main_oscillator_.Init();
  sub_oscillator_.Init();
}

void SubOscillatorEngine::Reset() {
  main_oscillator_.Init();
  sub_oscillator_.Init();
  dc_in_ = 0.0f;
  dc_out_ = 0.0f;
  dc_aux_in_ = 0.0f;
  dc_aux_out_ = 0.0f;
}

void SubOscillatorEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Reset();
  }

  const float frequency = NoteToFrequency(parameters.note);

  // HARMONICS merges Braids' two models: a square at one end, a variable saw
  // at the other, and every blend between. VariableShapeOscillator's waveshape
  // is TRIANGLE at 0, saw at 0.5 and square at 1 -- running it to 0 lands on a
  // triangle Braids has no SUB model for, and reads as the port going dark.
  const float waveshape = 1.0f - 0.5f * parameters.harmonics;

  // TIMBRE is the main oscillator's shape parameter, sweeping the pulse from
  // symmetric to thin. Two declared deviations live on this line, both DSP
  // defects reported rather than fixed -- see the header for the measurements:
  //  * the coefficient. Braids' low fraction is
  //    (32768 - min(p1*32767, 32000)) / 65536: slope ~0.5, floor 0.01172. The
  //    0.48/0.02 pair below diverges from it multiplicatively as the pulse
  //    narrows, worst (+4.4 dB) at TIMBRE 0.977, not at 1.0.
  //  * the effect is confined to the SQUARE_SUB half of HARMONICS' travel: at
  //    HARMONICS=1.0 (SAW_SUB) `waveshape` above is exactly 0.5,
  //    VariableShapeOscillator's pure-saw point, where pw drives nothing.
  // Note also that Render() re-clamps this to [2f, 1-2f], so above ~MIDI 61
  // the FREQUENCY sets the floor rather than TIMBRE; Braids has no such term.
  const float pw = 0.5f - 0.48f * parameters.timbre;

  // MORPH is Braids' COLOR: which side of centre picks the sub octave, and
  // the distance from centre sets its level. Zero sub exactly at noon.
  const float offset = parameters.morph - 0.5f;
  const float blend = kSubOscillatorMaxBlend * 2.0f * fabsf(offset);
  const float sub_octave = offset < 0.0f
      ? kSubOscillatorLowOctave
      : kSubOscillatorHighOctave;
  const float sub_frequency = frequency * SemitonesToRatio(sub_octave);

  // MACRO narrows the sub's pulse below noon and leaves Braids' square above.
  const float sub_pw = ApplyMacro(0.5f, 0.12f, 0.5f, parameters.macro);

  // VariableShapeOscillator::Render WRITES rather than accumulates, so the
  // sub needs its own scratch. Sized off kMaxBlockSize, never kBlockSize.
  float sub_buffer[kMaxBlockSize];

  main_oscillator_.Render(frequency, pw, waveshape, out, size);
  // The sub stays a square at every setting; only its width moves.
  sub_oscillator_.Render(sub_frequency, sub_pw, 1.0f, sub_buffer, size);

  for (size_t i = 0; i < size; ++i) {
    const float mixed = out[i] + (sub_buffer[i] - out[i]) * blend;
    // AUX carries the sub on its own, at full level rather than scaled by the
    // blend -- otherwise it would go silent at the centre of MORPH, which is
    // the first place anyone would look for it.
    const float sub = sub_buffer[i];

    // A narrow pulse carries a large DC term by construction: at the top of
    // TIMBRE the main duty reaches a few percent and the offset approaches
    // full scale. Braids leaves it, and so did the spec, but it thumps the
    // LPG at exactly those settings and the SDK's audio-health gate rejects
    // it outright. The corner is ~7.6 Hz -- which is NOT an order of magnitude
    // below the lowest sub this engine can produce. The two-octave sub reaches
    // 7.6 Hz at note 21 and clears the corner by only 3.6x at note 45; at
    // note 24 it sits at 8.18 Hz and the blocker takes 2.7 dB and 43 degrees
    // off it (measured -1.11 dB AC RMS against Braids). See the header's
    // declared deviations, including the per-trigger DC step Reset() causes.
    dc_out_ = mixed - dc_in_ + kSubOscillatorDcPole * dc_out_;
    dc_in_ = mixed;
    out[i] = dc_out_;

    dc_aux_out_ = sub - dc_aux_in_ + kSubOscillatorDcPole * dc_aux_out_;
    dc_aux_in_ = sub;
    aux[i] = dc_aux_out_;
  }
}

}  // namespace plaits
