// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' four TRIPLE models -- square, saw, triangle and sine x3 -- merged
// into one slot.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderTriple: three
// oscillators, the upper two detuned through a 65-entry interval ladder, mixed
// at 21/64 each. The four display models differ only in the base waveform, so
// MORPH turns that into a continuous axis and four models fit in one slot --
// the best ratio in the port after z-filter.
//
// WHAT THIS OVERLAPS, so the A/B is fair: `virtual-analog`'s first control is
// literally a detune across musical intervals, `swarm` is a detuned cloud, and
// the `chords` chord table is builder-configurable in cents with entries like
// { 0, 1, 1199, 1200 } already in it. Four voices at arbitrary cent intervals
// are reachable today at zero marginal flash. What is NOT reachable is the
// gesture: two interval knobs you sweep, rather than a table you author.
//
// UNISON AT NOON IS LOAD-BEARING -- but not by the route this comment used to
// give. What makes unison reachable in BOTH the module and the port is index 32
// with a zero crossfade: every parameter from 16384 to 16639 resolves i1 == i2
// == 32 and detunes by exactly nothing, a 256-wide plateau sitting just above
// the centre. The 255/256 crossfade one step BELOW it -- p = 16383, which is
// what a knob at 0.5 actually produces -- does NOT land near unison in Braids.
// macro_oscillator.cc:207 finishes the crossfade with an integer `>> 16`, so
// -4 + ((0 - -4) * 65280 >> 16) floors to -1/128 semitone = -0.781 cents, not
// the -0.012 cents exact arithmetic gives.
//
// OPEN DEVIATION, measured not accepted: LadderDetune below evaluates that same
// expression in float and does not floor it, so the port's detune runs up to
// 0.769 cents sharp of the module's -- worst at the knob centre, and on both
// upper voices at once, where it turns Braids' slow chorus (0.05 Hz of beating
// at 110 Hz, 0.40 Hz at 880 Hz) into a hard unison. tests/ab.json measures it
// by pairing each knob-centre case with the same sound one rung up, where both
// implementations agree to the bit: saw-unison's +0.41 dB AC RMS, 0.84 dB
// spectrum and +1.68 dB in the 320-640 Hz band become -0.01 dB, 0.24 dB and
// -0.03 dB in saw-unison-exact, and square-high's +1.24 dB becomes +0.01 dB in
// square-high-exact. square-high is left failing its tolerance on purpose.
//
// The index and crossfade arithmetic is otherwise reproduced rather than tidied,
// because a cleaner re-parameterisation breaks the plateau silently.
//
// MACRO's range is min == stock, NOT bidirectional. A pulse of duty d and one
// of duty 1-d have identical harmonic magnitudes, and a triangle with slope
// asymmetry pw and 1-pw are exact time reverses -- so a bidirectional range
// would give a knob whose two halves are spectral mirror images at half the
// resolution. It is also inert in the sine region, which is stated rather
// than papered over: the two-segment phase skew there is only C-zero, has no
// BLEP of any kind, and runs on three voices at once.

#ifndef PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_H_
#define PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/variable_shape_oscillator.h"

namespace plaits {

const int kTripleNumVoices = 3;

// Braids mixes each voice at 21/64.
const float kTripleVoiceGain = 21.0f / 64.0f;

// MORPH spends its first three quarters on square -> saw -> triangle through
// the variable-shape oscillator, then crossfades to sine.
const float kTripleSineRegion = 0.75f;

// A narrow pulse is inherently offset, so MACRO's top end carries real DC.
// Corner near 7.6 Hz, well below anything three detuned voices produce.
const float kTripleDcPole = 0.999f;

class TripleEngine : public Engine {
 public:
  TripleEngine() { }
  ~TripleEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: the three-voice mix against the undetuned root.
  virtual bool stereo_capable() const { return true; }

 private:
  VariableShapeOscillator voice_[kTripleNumVoices];
  float sine_phase_[kTripleNumVoices];
  float dc_in_;
  float dc_out_;
  float dc_aux_in_;
  float dc_aux_out_;

  DISALLOW_COPY_AND_ASSIGN(TripleEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_H_
