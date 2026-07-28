// Copyright 1995-2023 Perry R. Cook and Gary P. Scavone.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// A lip-reed brass instrument: an outward-striking valve driving a waveguide
// bore, after Cook's TBone/HosePlayer and STK's `Brass`.
//
// This is a REDESIGN, not a port, and it has to be. STK's `Brass` does not
// sustain: built standalone and measured across nine (sample rate, pitch)
// combinations it sounds in exactly one -- 22.05 kHz at 440 Hz -- and is silent
// at its own default 44.1 kHz. At 48 kHz it emits a 0.019-peak blip for the
// first 100 ms and then exact silence. Its lip filter is an all-pole resonator
// with a DC gain near 50, so a steady mouth pressure drives the squared lip
// position past the model's clamp of 1.0, the valve pins fully open, the output
// becomes a constant, the DC blocker removes it, and the bore never fills. It
// works only while the ADSR attack transient is still ringing the lip.
//
// WHY A LIP IS WORTH A SLOT WHEN THE CATALOG ALREADY HAS A REED AND A BOW.
// `reed-pipe` drives a bore with an INWARD-striking reed, which closes as
// pressure rises and therefore locks hard to the bore. A lip is
// OUTWARD-striking: it opens as pressure rises, and it can be tuned AWAY from
// the bore. That difference is the whole expressive range of a brass player --
// lipping a note flat, overblowing to the next partial, the unstable crack in
// between -- and it is not reachable from the reed engine at any setting.
//
// ============================ WHAT THE RESEARCH FOUND ======================
//
// 1. THE JUNCTION. A waveguide junction is `p_plus = p_minus + Zc*u` with the
//    total pressure `p = p_plus + p_minus`. Earlier attempts used STK's mixing
//    form (`p = area*mouth + (1-area)*bore`) and fed the valve from the
//    REFLECTED wave rather than the total junction pressure. That is not a
//    scattering junction, it has no mechanism locking the valve to the bore,
//    and it oscillated chaotically at a frequency independent of the delay.
//
// 2. A NON-INVERTING LOOP NEEDS AN IN-LOOP DC BLOCKER. A physical open end
//    inverts, giving odd harmonics only; a brass instrument's flare and
//    mouthpiece shift its modes into a near-complete harmonic series, which one
//    delay cannot do. The adaptation is a non-inverting loop -- but a
//    non-inverting loop's lowest mode is DC, and without the blocker the whole
//    model parks there and the delay length stops mattering.
//
// 3. THE NONLINEARITY MUST BE GENTLE OR THE LIP CANNOT SELECT A PARTIAL. With
//    the lips slamming shut every cycle, the model always locks to the bore's
//    FUNDAMENTAL no matter where the lip is tuned -- the hard nonlinearity
//    generates a full harmonic series and the fundamental is the attractor.
//    Opening the valve's rest position and stiffening it (a0 0.60, k 3.0) lets
//    the lip resonance decide instead, which is what makes HARMONICS work.
//
// 4. THE LOCK ZONES ARE NARROW AND THE GAPS BETWEEN THEM ARE HUGE. Swept
//    finely, partial n captures for lip/f_bore in roughly [0.90n, 1.01n] --
//    about 40% of a linear lip sweep is SILENT. So HARMONICS does not map to
//    lip frequency; it maps to a continuous PARTIAL INDEX, and the lip is
//    placed inside that partial's zone. The knob then never falls in a gap, and
//    the small sharpening across each segment is real lipping.
//
// 5. THE ZONES WIDEN WITH MOUTH PRESSURE (blowing harder makes a note easier to
//    hold, as on the real thing), so the SOFTEST playing sets the safe
//    placement.
//
// 6. PARTIAL 1 IS THE BAD ONE -- always sharp, erratic zone. True of real brass
//    too, where the pedal tone is a special effect and the written fundamental
//    is the SECOND partial. So the bore is tuned an OCTAVE BELOW the played
//    note and the usable range starts at its second partial. Lipping up then
//    gives the bugle series above the played note: unison, fifth, octave,
//    tenth, twelfth.
//
// 7. THE LIP PULLS THE PITCH SHARP by an amount that fits `-4.8 + 67.2/n`
//    cents almost exactly. Only one partial sounds at a time, so it is nulled
//    by lengthening the bore for the selected partial. Measured over 312 points
//    spanning notes 34-78, both breath extremes and the whole knob: mean 6.3
//    cents, worst 25.8, and about 1% of points crack to a neighbouring partial
//    -- which real players also do.
//
// 8. THE OUTPUT TAP IS THE MOUTHPIECE, not the bell. Taking the radiated
//    signal (the part of the wave the bell does not reflect) is physically
//    right and measured a 30 dB level slide across the keyboard, because a
//    fixed bell corner radiates low notes poorly. The bell corner here tracks
//    the played note instead -- a V/oct input should give the same instrument
//    at every pitch -- but even so the radiated tap humps by 17 dB.
//    The pressure inside the mouthpiece is flat to 0.7 dB from note 40 up and
//    carries the bore's full standing wave, so that is OUT and the valve flow
//    is AUX.
//
//    BOTH TAPS MUST BE DC-BLOCKED, and this is easy to miss: blowing produces
//    a steady flow as well as an oscillation, so the raw taps are mostly bias.
//    Measured before blocking, OUT read RMS 27923 with a DC component of
//    26548 -- it passed the audition and control-response gates, which do not
//    look at DC, and would have reached a module as a fat offset with a tone
//    buried in it.

#ifndef PLAITS_DSP_ENGINE2_BRASS_ENGINE_H_
#define PLAITS_DSP_ENGINE2_BRASS_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Bore length at the lowest note, longest slide, with the pull compensation:
// (48000 / 27.5) * 2^(28.8/1200) * 1.5, rounded up.
const int kBrassDelaySize = 2688;

// Below this the engine folds up by octaves rather than allocating for a
// register no brass instrument occupies.
const float kBrassLowestNote = 33.0f;  // A1, 55 Hz.

// The played note is the bore's SECOND partial (finding 6).
const int kBrassFirstPartial = 2;
const int kBrassLastPartial = 7;

// A partial is dropped rather than offered if it would sit above this.
const float kBrassHighestPartial = 3000.0f;

// Where inside a partial's lock zone the lip sits, as a multiple of that
// partial's frequency, from the flat edge of the knob segment to the sharp one.
// Measured (findings 4 and 5); the span is the audible lipping.
const float kBrassZoneBase = 0.935f;
const float kBrassZoneSpan = 0.025f;

// The lip's pitch pull, in cents, as a function of partial number: it fits
// kBrassPullOffset + kBrassPullScale / n across every note measured.
const float kBrassPullOffset = -4.8f;
const float kBrassPullScale = 67.2f;

// Valve and bore constants, from the parameter search. The lip Q is not a taste
// choice -- at zeta 0.12 the model does not oscillate at all.
const float kBrassLipZeta = 0.04f;
const float kBrassLipStiffness = 3.0f;
const float kBrassRestOpening = 0.60f;
const float kBrassMaxOpening = 1.80f;
const float kBrassBoreImpedance = 0.15f;
const float kBrassReflection = 0.995f;

// Breath turbulence, which starts the oscillation and is true of the real
// thing. STK's own Clarinet, Flute and Saxofony all do this; its Brass does
// not, which is part of why its Brass does not speak.
const float kBrassNoiseGain = 0.02f;

class BrassEngine : public Engine {
 public:
  BrassEngine() { }
  ~BrassEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: the flow at the bell against the pressure in the mouthpiece.
  virtual bool stereo_capable() const { return true; }

 private:
  float* delay_line_;
  int delay_write_;

  // Lip: an outward-striking mass-spring-damper valve.
  float lip_x_;
  float lip_v_;

  float bell_lp_;
  float junction_;
  float dc_x1_;
  float dc_y1_;
  float out_dc_x1_;
  float out_dc_y1_;
  float aux_dc_x1_;
  float aux_dc_y1_;

  DISALLOW_COPY_AND_ASSIGN(BrassEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_BRASS_ENGINE_H_
