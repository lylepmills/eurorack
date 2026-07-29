// Copyright 2012 Olivier Gillet.
// Copyright 2015 Tim Churches.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Bees-in-the-Trees' four BYTEBEAT models merged into one slot.
//
// Upstream is Tim Churches' alternative Braids firmware ("Bees-in-the-Trees",
// timchurches/Mutated-Mutables), which added BYTEBEAT0..3 to Braids as
// DigitalOscillator::RenderBytebeat0..3. A bytebeat is an integer expression
// over a free-running counter t whose low byte is read as the sample; the four
// expressions are carried over verbatim, including their original attributions:
//
//   0  royal-paw.com/2012/01/bytebeats-in-c-and-python-generative-symphonies-
//      from-extremely-small-programs/  ("atmospheric, hopeful")
//   1  stephth, via youtube.com/watch?v=tCRPUv8V22o at 3:38
//   2  reddit.com/r/bytebeat/comments/20km9l/cool_equations/
//   3  the second entry at xifeng.weebly.com/bytebeats.html
//
// Churches' own firmware is MIT (he added his copyright line to Emilie
// Gillet's header in every file he touched), so the expressions arrive here
// under the same terms the rest of this port does.
//
// THREE THINGS ARE DELIBERATELY NOT FAITHFUL, and each is a defect upstream
// rather than a taste difference:
//
// 1. PITCH. Braids computed `bytepitch = (16384 - pitch_) >> 11` and advanced
//    t once every `bytepitch` samples. pitch_ spans 0..16383 in 1/128ths of a
//    semitone, so that integer takes only NINE distinct values across the whole
//    keyboard -- the model is nearly unplayable, the mapping is inverse-linear
//    rather than exponential, and at the top of the range bytepitch reaches 0,
//    making `phase_ % bytepitch` a division by zero. Here t advances by a
//    fractional increment of `f0 * kBytebeatTicksPerCycle` per sample, so the
//    engine tracks 1V/oct properly. Below one tick per sample this is the same
//    zero-order hold Braids performed; above it, ticks are skipped, which is
//    the same decimation Braids did at bytepitch == 1 and is where the harsher
//    high register comes from.
// 2. `t % p1` IN FORMULA 3. p1 was `parameter_[1] >> 8`, i.e. 0..127, and zero
//    is reachable -- a modulo by zero at the bottom of the COLOR knob. Clamped
//    to 1 here.
// 3. THE TRIGGER RESTARTS THE STREAM. Churches deliberately left t running
//    across a model change ("Don't reset the bytebeat counter to allow
//    continuity when switch models"), which suits a Braids model with no gate
//    input. In Plaits a trigger opens the LPG and the attack has to be
//    repeatable, so a rising edge restarts t -- at 1024, not 0, because three
//    of the four expressions gate on `t >> 10` and are exactly silent below it.
//    The offset is read off the expressions rather than chosen.
//
// MACRO is the mask width. Every expression ends in `& 0xFF` read as a signed
// byte; that 8 is the only arbitrary constant in a bytebeat, so MACRO opens it
// from 2 bits (a squared-off two-level stream) to 12 (higher bits of the
// expression leak in, and the stream turns wilder and drops in register).
// Stock 8 sits exactly at the detent.

#ifndef PLAITS_DSP_ENGINE2_BYTEBEAT_ENGINE_H_
#define PLAITS_DSP_ENGINE2_BYTEBEAT_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kBytebeatNumFormulas = 4;

// t advances at f0 * 256, so the low byte of a term like `t * 3` wraps at a
// rate locked to the played note. The perceived fundamental of these
// expressions sits a small integer multiple above f0 rather than on it -- they
// are not oscillators and have no single period -- but the tracking is exact.
const float kBytebeatTicksPerCycle = 256.0f;

// Formulas 0, 1 and 3 gate on `t >> 10` (or `t >> 8`) and produce a constant
// zero below it. Restarting there rather than at 0 removes ~9 ms of silence
// from the front of every triggered note.
const uint32_t kBytebeatRestartTick = 1024;

// AUX runs the same expression at half the tick rate.
const float kBytebeatAuxRatio = 0.5f;

// A signed-byte bytebeat carries a large and content-dependent DC term, and
// the audio-health gate rejects it. Corner near 7.6 Hz.
const float kBytebeatDcPole = 0.999f;

class BytebeatEngine : public Engine {
 public:
  BytebeatEngine() { }
  ~BytebeatEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: the stream against its own sub-octave.
  virtual bool stereo_capable() const { return true; }

 private:
  float tick_phase_;
  float aux_tick_phase_;
  uint32_t t_;
  uint32_t aux_t_;
  float dc_in_;
  float dc_out_;
  float dc_aux_in_;
  float dc_aux_out_;

  DISALLOW_COPY_AND_ASSIGN(BytebeatEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_BYTEBEAT_ENGINE_H_
