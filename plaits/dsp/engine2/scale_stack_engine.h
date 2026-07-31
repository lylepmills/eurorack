// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids Renaissance's five STACK_* models merged into one slot.
//
// Upstream is DigitalOscillator::RenderStack in Tom Burns' Braids Renaissance
// (boourns/eurorack-renaissance): five oscillators starting at the played note
// and spaced an equal number of SCALE DEGREES apart, so one knob sweeps from a
// unison cluster out through stacked seconds, thirds, fourths and beyond --
// and every rung stays in the scale, which is what makes the sweep musical
// rather than an interval ramp. As with `diatonic-chord`, the five display
// models differ only in waveform, so four of them fold onto MORPH and the
// wavetable one is dropped for the same reason (see diatonic_chord_engine.h).
//
// WHAT SEPARATES THIS FROM `swarm` AND `triple`. Those detune in cents around
// a note; this quantises every voice onto a scale, so wide spans are chords
// rather than clusters and the whole stack transposes diatonically under the
// keyboard. At span 1 in a whole-tone scale it is an interval stack no cent
// detune reaches; at span 2 in a major scale it is a thirteenth chord. All five
// Renaissance waveforms, including WTx6's wavetable scan, share MORPH.
//
// ⚠️ AS IN `diatonic-chord`, upstream's offsets are read as ABSOLUTE. RenderStack
// pre-accumulates its own spans (`acc += span; offsets[i] = acc`) into span,
// 2*span, 3*span, 4*span -- and then renderChord accumulates them AGAIN, which
// is what actually reaches the DAC: span, 3*span, 6*span, 10*span. At the top
// of the knob that puts the fourth voice 160 scale degrees up, far past the
// audible range, so the model quietly loses voices as the knob rises. The
// pre-accumulation is unambiguous about what was intended, and this port does
// the intended thing. Voices that still land out of range at wide spans are
// muted rather than aliased, and the mix does not compensate -- a stack that
// runs off the top of the range is supposed to thin out.

#ifndef PLAITS_DSP_ENGINE2_SCALE_STACK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SCALE_STACK_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine2/scale_voices.h"

namespace plaits {

// Braids' kStackSize was 6 but RenderStack only ever filled five.
const int kScaleStackNumVoices = 5;

// `span = 1 + (parameter_[1] >> 11)` upstream, i.e. 1..16 scale degrees.
const int kScaleStackMaxSpan = 16;

class ScaleStackEngine : public Engine {
 public:
  ScaleStackEngine() { }
  ~ScaleStackEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: the stack against its own root.
  virtual bool stereo_capable() const { return true; }

 private:
  ScaleVoiceBank voices_;

  DISALLOW_COPY_AND_ASSIGN(ScaleStackEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SCALE_STACK_ENGINE_H_
