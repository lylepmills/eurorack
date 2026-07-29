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
// UNISON AT NOON IS LOAD-BEARING -- but not by the route this comment first
// gave. What makes unison reachable is index 32 with a zero crossfade: every
// parameter from 16384 to 16703 detunes by exactly nothing, a plateau sitting
// just above the centre. The 255/256 crossfade one step BELOW it -- p = 16383,
// which is what a knob at 0.5 actually produces -- does NOT land near unison.
// macro_oscillator.cc:207 finishes the crossfade with an integer `>> 16`, so
// -4 + ((0 - -4) * 65280 >> 16) floors to -1/128 semitone = -0.781 cents, not
// the -0.012 cents exact arithmetic gives. Both the module and this port now
// land on the -0.781.
//
// FIXED 2026-07, and audible: LadderDetune evaluated that same expression in
// FLOAT and never floored it, so the port's detune ran up to 0.769 cents sharp
// of the module's -- worst at exactly the knob centre, and on both upper voices
// at once, which turned Braids' slow chorus at noon (0.05 Hz of beating at
// 110 Hz, 0.40 Hz at 880 Hz) into a hard unison. It now does the crossfade in
// Braids' own int16 1/128-semitone units and shifts where Braids shifts;
// swept exhaustively, all 32768 parameter values agree with the module exactly,
// and the zero-detune plateau lines up at 16384..16703 on both sides.
// The A/B moved with it: square-high +1.24 dB AC RMS / 1.49 dB spectrum ->
// -0.01 / 0.50, saw-unison +0.41 / 0.84 -> -0.01 / 0.27, and saw-unison's
// +1.68 dB excess in the 320-640 Hz band -> -0.03 dB.
//
// The index and crossfade arithmetic is reproduced rather than tidied, because
// a cleaner re-parameterisation breaks the plateau silently. Where Braids
// shifts, this shifts.
//
// NO OUTPUT FILTER, because RenderTriple has none. An earlier revision ran the
// mix and the aux through a one-pole DC blocker at 0.999 (corner ~7.6 Hz) whose
// justification here claimed the corner sat "well below anything three detuned
// voices produce". That was false: the ladder's bottom rung is -24 semitones,
// so an A0 root puts a voice at 6.875 Hz, essentially ON the corner: -3.5 dB on
// that voice's fundamental, -0.9 dB on a C0, -0.3 dB on an A0. Measured by
// re-applying the same one-pole to the current render, the blocker cost
// 0.53 dB of AC RMS on ab.json's saw-low-dc. That is the whole of what its
// removal returns -- the case's -1.16 dB headline was that 0.53 plus the
// 0.63 dB octave-coincidence residual in KNOWN RESIDUAL below, which the
// blocker never caused and which is exactly what the case still reads. What
// the blocker was actually for -- the DC a narrow pulse
// carries at the top of MACRO -- is now subtracted in closed form inside the
// voice loop, where it is exactly square_amount * (1 - 2 * pw). That term is
// bit-exactly zero at pw = 0.5, which is the whole lower half of MACRO and
// therefore everything Braids can reach, and zero again through saw, triangle
// and sine. The A/B is byte-identical with the correction present and with no
// correction at all; the health gate's narrow-pulse scenario goes from 0.2662
// DC unfiltered to -0.00086.
//
// MACRO's range is min == stock, NOT bidirectional. A pulse of duty d and one
// of duty 1-d have identical harmonic magnitudes, and a triangle with slope
// asymmetry pw and 1-pw are exact time reverses -- so a bidirectional range
// would give a knob whose two halves are spectral mirror images at half the
// resolution. It is also inert in the sine region, which is stated rather
// than papered over: the two-segment phase skew there is only C-zero, has no
// BLEP of any kind, and runs on three voices at once.
//
// HOW AN EXISTING PATCH CHANGES. The ladder fix is not rounding; it is audible,
// and it is loudest at the setting people actually leave the knobs on.
//   * Both interval knobs near noon used to give a dead-still unison. It now
//     beats slowly, because both upper voices sit 0.781 cents flat instead of
//     0.012 -- roughly one beat every 20 s at A2, one every 2.5 s at A5. That
//     is the module's sound and the reason the model is worth having, but a
//     patch built on the old lock-step unison will now drift and thicken. The
//     dead-still unison has NOT been lost: it is the plateau just above centre
//     (knob 0.5005 upward), which is where it is on the module too.
//   * Everywhere else on the ladder the detune moves by at most 0.77 cents,
//     downward, and snaps to a 1/128-semitone grid. Between rungs a slow sweep
//     of either interval knob now steps rather than glides. Both are inaudible
//     as pitch on a single voice and audible as beat rate against the root.
//   * Low notes get their bottom back. With the DC blocker gone, a root at or
//     below A0 -- or any patch using the -24-semitone rungs under a low root --
//     regains the sub-20 Hz energy the filter was removing: +0.53 dB of AC RMS
//     on ab.json's A0 case, essentially all of it beneath 20 Hz. (Not the full
//     -1.16 dB that case used to read -- 0.63 dB of that was never the filter.)
//     The AUX root voice loses the blocker too, which is worth +0.3 dB at an A0
//     root and +0.9 dB at a C0, and nothing above A1. Expect more cone movement
//     and more DC into whatever follows the module.
//   * MACRO above its detent is unchanged in sound but no longer drifts: the
//     offset is removed exactly instead of being chased by a 7.6 Hz filter, so
//     narrow-pulse patches no longer thump on a fast MACRO move.
//
// KNOWN RESIDUAL, not caused by either fix and not fixable in this file: at
// EXACT OCTAVE detunes the port reads 0.6-1.1 dB quieter than the module with
// matching spectral shape (ab.json's saw-wide, -1.12 dB, tolerance 1.5). The
// voices' harmonics coincide there, so the sum depends on the phase they settle
// into, and Plaits' stock VariableShapeOscillator::Init seeds
// slave_frequency_ = 0.01f -- the first block's ParameterInterpolator ramp
// leaves a lasting +39.0 deg on a 27.5 Hz voice against +31.9 deg on a 110 Hz
// one. Braids ramps its own phase_increment_ from a different seed. It is
// inherited from the shared oscillator, not written here.

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

  DISALLOW_COPY_AND_ASSIGN(TripleEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_H_
