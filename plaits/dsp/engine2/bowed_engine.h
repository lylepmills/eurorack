// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' BOWD: a bowed-string waveguide with a stick-slip friction exciter.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderBowed. It ends in
// `size -= 2`, so it is a 48 kHz algorithm writing a 96 kHz stream through a
// 2x linear interpolator -- which means every rate constant, the bridge
// filter and the body biquad transfer verbatim, and only the output stage has
// to be re-derived. Nothing in the stock palette is a continuous stick-slip
// friction voice.
//
// MORPH is new: Braids welds the nut reflection to -1.0 and this opens it.
// MACRO is new: it moves the body resonance +-1 octave around the stock pole.
//
// OUT: the bridge pickup through the body filter. AUX: the neck pickup
// through its own copy of the same body filter, so the two are a genuine
// pair of pickup positions at matched gain rather than one filtered voice
// against one raw one.
//
// MEMORY: the delay lines are int8 exactly as in Braids -- 1024 + 4096 = 5 KB
// of the 16 KB arena. Braids quantizes every write to int8 anyway (R8 keeps
// that, it is audibly part of the model), so storing float would spend 4x the
// memory to hold values that have already been rounded. Keeping Braids' own
// lengths means the octave-fold floor is Braids' 11.4 Hz, NOT the 17.2 Hz a
// float line halved to fit the arena would give -- so the residual fidelity
// gap the spec raised as an open question does not arise.
//
// Declared deviations from Braids:
//   - int8 writes FLOOR and SATURATE (R8). Braids floors and WRAPS; wrap
//     inside a feedback loop is a stability hazard with no analogue to its
//     int32 accumulator bounds.
//   - lut_bowing_friction (2,018 B) is replaced by min(1, 1/(d+0.75)^4),
//     which is the curve the table holds: exact at both ends (32768 at d=0,
//     64 at d=4). Flash, not speed -- a table lookup is not slower here.
//   - lut_bowing_envelope (1,504 B) is replaced by three line segments. The
//     table's 600-step rise is linear to under one LSB and its tail is
//     constant; the 120-step decay between them is within 0.15%.
//   - the bridge tap is CLAMPED rather than allowed to go degenerate. In
//     Braids a bridge delay under one sample wraps the modulo and reads
//     1024 samples back instead; from about MIDI 85 upward at HARMONICS 0
//     that is what the model actually does, and it is not worth reproducing.
//   - the output stage is re-derived. Braids' `(out + previous) >> 1` then
//     `out` is a 2x linear-interpolating UPSAMPLER writing 96 kHz, not a
//     filter; its baseband effect is -1.4 dB at 12 kHz. Re-implementing it
//     literally at 48 kHz would give -3.0 dB there plus a hard null at
//     24 kHz -- darker than BOWD, the opposite of the intent -- so it is a
//     one-pole matched to the real response instead.
//   - SoftClip replaces CLIP, with the make-up gain inside it (R3), and the
//     output carries a 1.6x make-up Braids does not, because BOWD runs at
//     about -15 dBFS and the palette expects near-full-scale engines.
//
// ON MATCHING THIS ONE AGAINST HARDWARE: bowed is a nonlinear self-oscillator,
// not an oscillator. The port renders about 8 cents sharp -- the standard
// kCorrectedSampleRate offset every engine here carries -- and at MIDI 45 that
// is half a percent of a 434-sample loop. In a stick-slip feedback system that
// is enough to settle into a DIFFERENT limit cycle, so third-octave spectra
// land 3-5 dB apart even when every coefficient agrees. Pitch, level and
// gross spectral tilt track; sample- or bin-level agreement is not a
// meaningful target for this engine and chasing it will mislead.

#ifndef PLAITS_DSP_ENGINE2_BOWED_ENGINE_H_
#define PLAITS_DSP_ENGINE2_BOWED_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' kWGBridgeLength / kWGNeckLength, both powers of two so the wrap is
// a mask rather than a modulo.
const size_t kBowedBridgeLength = 1024;
const size_t kBowedNeckLength = 4096;

// Bridge reflection one-pole: kBridgeLPGain 14008 and kBridgeLPPole1 18022,
// over 32768. DC gain 0.4275 / (1 - 0.55) = 0.95, the reflection loss.
const float kBowedBridgeLpGain = 14008.0f / 32768.0f;
const float kBowedBridgeLpPole = 18022.0f / 32768.0f;

// Body biquad: r = sqrt(2959/4096), a1 = 6948/4096 = 2*r*cos(theta),
// gain 6553/32768. theta = 0.06578 rad, about 502 Hz at 48 kHz.
const float kBowedBodyGain = 6553.0f / 32768.0f;
const float kBowedBodyRadius = 0.849949f;
const float kBowedStockTheta = 0.0657752f;

// `parameter_0 = 172 - (TIMBRE >> 8)` in [45, 172], scaled by 1/32.
const float kBowedPressureMin = 45.0f;
const float kBowedPressureMax = 172.0f;

// `parameter_1 = 6 + (HARMONICS >> 9)` in [6, 69], over 256.
const float kBowedBowPositionMin = 6.0f / 256.0f;
const float kBowedBowPositionMax = 69.0f / 256.0f;

// Bowing envelope, as fractions of full scale: 6553/32768 peak after a
// 600-step linear rise, decaying over 120 steps to a 5242/32768 sustain.
const float kBowedEnvelopePeak = 6553.0f / 32768.0f;
const float kBowedEnvelopeSustain = 5242.0f / 32768.0f;
const float kBowedEnvelopeRise = 599.0f;
const float kBowedEnvelopeHold = 720.0f;

// The one-pole standing in for Braids' 2x interpolating upsampler: -1.5 dB at
// 12 kHz against its -1.4 dB.
const float kBowedTilt = 0.15f;

class BowedEngine : public Engine {
 public:
  BowedEngine() { }
  ~BowedEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: two pickups on one string, decorrelated at matched gain.
  virtual bool stereo_capable() const { return true; }

 private:
  int8_t* bridge_line_;
  int8_t* neck_line_;

  uint32_t delay_pointer_;
  float excitation_;
  float bridge_lp_;

  float body_y0_;
  float body_y1_;
  float body_aux_y0_;
  float body_aux_y1_;

  float tilt_state_;
  float tilt_state_aux_;

  DISALLOW_COPY_AND_ASSIGN(BowedEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_BOWED_ENGINE_H_
