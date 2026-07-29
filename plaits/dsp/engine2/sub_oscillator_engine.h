// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SUB models: a shaped main oscillator against a square sub, one or
// two octaves down.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderSub, which covers
// two display models -- SQUARE_SUB and SAW_SUB -- differing only in whether
// the main oscillator is a square or a variable saw. HARMONICS merges them
// into one continuous axis, which is the whole reason two Braids models fit
// in one palette slot.
//
// MORPH is Braids' COLOR verbatim, and it is worth knowing its shape: the sub
// level is a V. Fully counter-clockwise gives a half-and-half mix with the sub
// two octaves down, the CENTRE gives no sub at all, and fully clockwise gives
// a half-and-half mix one octave down. The sub is at its loudest at BOTH ends.
//
// MACRO is new: Braids welds the sub to a plain square, and this narrows its
// pulse. The DETENT and everything ABOVE it are Braids (ApplyMacro's maximum
// equals its stock value); turning MACRO down from noon narrows the sub
// toward 12% pulse width, which Braids cannot do.
//
// A DC blocker is present (a narrow pulse is inherently offset), so the
// registered gains are NEGATIVE and the limiter is engaged (R1).
//
// OUT: the mix. AUX: the sub alone at full level -- not the mix-scaled sub,
// which would be silent at the centre of MORPH exactly where a player would
// go looking for it.
//
// Declared deviations (tests/ab.json), reproducible via `ab_engine.py`. All
// three are DSP-level -- reported, not fixed, per BRAIDS_PORT_PHASE2_GUIDE.md.
//
//  - The TIMBRE -> pulse-width LAW is wrong, across the whole top of the axis.
//    Braids' low fraction is (32768 - min(p1 * 32767, 32000)) / 65536: a slope
//    of ~0.5 down to a clamped floor of 0.01172 reached at p1 = 0.9766
//    (AnalogOscillator::RenderSquare, analog_oscillator.cc:194-196, 206). This
//    engine uses `pw = 0.5 - 0.48*timbre` (sub_oscillator_engine.cc) -- a 0.48
//    slope and a 0.02 floor. Because the two agree at 0.5 and diverge
//    multiplicatively as the pulse narrows, the error grows through the whole
//    upper quarter of the travel and PEAKS AT THE CLAMP KNEE, not at the top:
//    +1.11 dB AC RMS / 1.51 dB spectrum at TIMBRE 0.88, +4.44 dB / 5.13 dB at
//    0.977, and back to +2.56 dB / 2.98 dB at 1.0 (all measured, note 45).
//    Braids' 32000 clamp is NOT the cause -- at TIMBRE 0.95, below the clamp
//    threshold, the port is already +2.53 dB out. It is the 0.48 coefficient.
//    An unswept TIMBRE renders -0.35 dB / 1.70 dB over its full range.
//  - The same law's floor also moves with PITCH here and does not in Braids.
//    VariableShapeOscillator::Render CONSTRAINs pw to [2f, 1-2f]
//    (variable_shape_oscillator.h:107-111); RenderSquare has no frequency term
//    at all. From roughly MIDI 61 upward, 2f exceeds Braids' 0.01172 floor and
//    the port simply cannot reach Braids' narrowest pulse; from roughly MIDI
//    71 the CONSTRAIN, not TIMBRE, sets the width. At note 84, TIMBRE 1.0:
//    +8.35 dB AC RMS / 5.04 dB spectrum, f0 estimated at 1045 Hz for Braids
//    against 262 Hz for this engine. The same note at TIMBRE 0.5 is clean
//    (+0.18 dB / 0.40 dB), so it is the floor and not a pitch or rate error.
//  - SAW_SUB: TIMBRE has NO audible effect in this engine at any setting.
//    HARMONICS=1.0 (SAW_SUB) puts VariableShapeOscillator's waveshape at
//    exactly 0.5, its pure-saw point, where square_amount and triangle_amount
//    (variable_shape_oscillator.h:137-138) are both exactly zero -- so `pw`
//    (fed from TIMBRE) never reaches the output. Braids' RenderVariableSaw
//    (analog_oscillator.cc:337-422) has no such degeneracy: its naive waveform
//    is `(phase_>>18) + ((phase_-pw)>>18)`, two ramps pw apart, and that
//    spacing tracks parameter_[0] across the whole range. Confirmed directly
//    by rendering TIMBRE=0.0 and TIMBRE=1.0 here and diffing: mean abs sample
//    difference 5.9e-6 over 3 s, and at most 1 LSB (-110 dBFS) past the first
//    ~2700 samples, while the equivalent Braids renders differ by +6.01 dB AC
//    RMS and a full octave of apparent pitch. Note this is wrong at BOTH ends,
//    not just the top: Braids floors parameter_ at 1024, so even at TIMBRE 0
//    its ramps are 1/64 cycle apart and it carries a comb null on harmonic 32
//    (+10.94 dB in the 2560-5120 Hz band against this engine's plain ramp),
//    which the 1.28 dB aggregate spectrum figure very nearly buries.
//  - The DC blocker is NOT free at the bottom of the range, and it is not the
//    only thing the pole costs. A 0.999 pole at kCorrectedSampleRate puts the
//    corner at 7.6 Hz. The two-octave sub reaches that at MIDI 45+9 = note 33
//    and goes UNDER it below note 21 -- so at note 24, an ordinary bass note,
//    the sub sits at 8.18 Hz, 1.07x the corner, losing 2.7 dB and 43 degrees
//    that Braids (which has no blocker) keeps: measured -1.11 dB AC RMS /
//    0.83 dB spectrum. Even at the A/B's own note 45 the sub clears the corner
//    by only 3.6x. Separately, Render() calls Reset() on TRIGGER_RISING_EDGE,
//    which zeroes dc_in_/dc_out_, so every trigger re-arms the blocker from
//    scratch: at TIMBRE 1.0 that injects a ~0.54 full-scale DC step decaying
//    over ~20 ms on EVERY note -- the same thump the blocker exists to remove.
//    Not covered by ab.json (its cases render one note); measured directly
//    with `--trigger-hz 2`.

#ifndef PLAITS_DSP_ENGINE2_SUB_OSCILLATOR_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SUB_OSCILLATOR_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/variable_shape_oscillator.h"

namespace plaits {

// Braids picks the sub octave from which side of centre COLOR sits on.
const float kSubOscillatorLowOctave = -24.0f;
const float kSubOscillatorHighOctave = -12.0f;

// `sub_gain = (p1 < 16384 ? 16383 - p1 : p1 - 16384) << 1` as a Mix balance
// peaks at 32766/65536, so the sub never exceeds an equal blend.
const float kSubOscillatorMaxBlend = 0.5f;

// DC blocker pole, a corner near 7.6 Hz. That is clear of the sub over most
// of the keyboard but NOT all of it: the two-octave sub crosses 7.6 Hz at
// note 21 and clears it by only 3.6x at note 45. See the declared deviations
// above for the measured cost at note 24.
const float kSubOscillatorDcPole = 0.999f;

class SubOscillatorEngine : public Engine {
 public:
  SubOscillatorEngine() { }
  ~SubOscillatorEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: the mix against the bare sub, decorrelated at matched gain.
  virtual bool stereo_capable() const { return true; }

 private:
  VariableShapeOscillator main_oscillator_;
  VariableShapeOscillator sub_oscillator_;

  float dc_in_;
  float dc_out_;
  float dc_aux_in_;
  float dc_aux_out_;

  DISALLOW_COPY_AND_ASSIGN(SubOscillatorEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SUB_OSCILLATOR_ENGINE_H_
