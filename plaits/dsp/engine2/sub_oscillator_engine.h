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
// pulse. Minimum == stock, so the detent and everything above it are Braids.
//
// A DC blocker is present (a narrow pulse is inherently offset), so the
// registered gains are NEGATIVE and the limiter is engaged (R1).
//
// OUT: the mix. AUX: the sub alone at full level -- not the mix-scaled sub,
// which would be silent at the centre of MORPH exactly where a player would
// go looking for it.

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

// DC blocker pole, a corner near 7.6 Hz -- far below the lowest sub this
// engine reaches, so it removes the offset without shaping the pulse.
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
