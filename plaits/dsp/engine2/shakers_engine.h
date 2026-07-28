// Copyright 1995-2023 Perry R. Cook and Gary P. Scavone.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// STK's Shakers -- Perry Cook's PhISEM -- as one Plaits engine.
//
// PhISEM (Physically Informed Stochastic Event Modeling) does not model an
// instrument's geometry. It models the STATISTICS of many small objects
// colliding inside a container: a shake energy that decays exponentially, a
// per-sample collision probability proportional to that energy and to the
// object count, and a handful of fixed resonances the collisions excite. Two
// dozen recognisable instruments come out of one algorithm and about eight
// numbers each, which is why sixteen of them fit in one slot.
//
// WHAT IT IS NOT. `particle-noise` also fires sparse events into resonators,
// but its resonances are randomly spread to make a cloud -- it is a texture
// generator. PhISEM's resonances are measured off real objects (a maraca is one
// pole at 3200 Hz; a coke can is a Helmholtz mode at 370 plus four metal modes),
// and its energy/collision statistics are what make a shaken gourd read as a
// shaken gourd rather than as filtered noise. The catalog has three analog drum
// circuits and no acoustic percussion at all; this is that.
//
// TWO MECHANISMS, not one. Most instruments are "shake": energy decays, and
// collisions inject noise into the resonators. Two are RATCHETS (guiro,
// wrench) -- a sawtooth energy ramp that resets per tooth, which is why they
// have a distinct rasping rhythm rather than a decay. One is WATER DROPS, whose
// resonators are individually retuned and swept as each drop is born. Upstream
// special-cases all three inside `tick`, and so does this.
//
// SAMPLE-RATE CORRECTION IS NOT OPTIONAL HERE. Every decay constant, filter
// radius and collision probability in STK is a raw per-sample number tuned at
// 44.1 kHz. Ported unchanged to Plaits' 48 kHz they would all be ~8.8% wrong in
// the same direction: everything rings 8.8% shorter in real time and shakes
// 8.8% denser. Decays and radii are raised to the power 44100/48000, and the
// object count is scaled by the same ratio, so the instruments sound like
// themselves. The frequencies were always in Hz and need no correction.
//
// PITCH. A maraca does not have a pitch, but a Plaits user has a note input and
// expects it to do something. The note transposes every resonance of every
// instrument around MIDI 60, which is musically the right answer for the tuned
// ones (angklung is a real bamboo scale) and a size control for the rest --
// a small gourd at the top, an oil drum at the bottom.

#ifndef PLAITS_DSP_ENGINE2_SHAKERS_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SHAKERS_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kShakersNumInstruments = 16;

// Angklung has seven tubes; mug-and-coins is four mug modes plus three coin
// modes. Nothing here needs more.
const int kShakersMaxResonances = 7;

// STK's constants were tuned at 44.1 kHz.
const float kShakersReferenceRate = 44100.0f;

// The note that plays an instrument at its measured frequencies.
const float kShakersReferenceNote = 60.0f;

// Below this the shake is over and the engine returns silence, as upstream.
const float kShakersMinEnergy = 1e-8f;

// Upstream's MAX_SHAKE.
const float kShakersMaxShake = 1.0f;

// A ratchet's energy ramp, per tooth.
const float kShakersGuiroRatchetDelta = 0.0001f;
const float kShakersWrenchRatchetDelta = 0.00015f;

// Water drop resonators sweep upward as each drop is born.
const float kShakersWaterFreqSweep = 1.0001f;

// LEVEL MATCHING, and it is the difference between an instrument selector and a
// hazard. Measured across the shake/decay/object space, the sixteen instruments
// span 46 dB as upstream tunes them -- a cabasa is 12.7 dB above the mean and a
// water drop 33.5 dB below it -- because STK expects each to be used on its own
// with its own mixer fader, not swept under one knob. Each preset carries a
// measured makeup gain toward this target, clamped to [0.15, 20] so the two
// sparsest are lifted as far as their crest factor allows rather than being
// slammed. Worst remaining deviation 6.5 dB, from 46.
const float kShakersLevelTarget = 0.035f;

struct ShakerPreset {
  const char* name;
  int num_resonances;
  const float* frequencies;
  const float* radii;
  const float* gains;
  float num_objects;
  float system_decay;
  float sound_decay;
  float gain;
  float decay_scale;
  float vary_factor;
  // Which resonances jitter their centre frequency on each collision. A bit
  // mask over resonance index; 0 means none.
  uint8_t vary_mask;
  // Equalizer numerator, applied to the summed resonator output.
  float eq_b0;
  float eq_b1;
  float eq_b2;
  // 0 shake, 1 ratchet, 2 water.
  uint8_t mechanism;
  float ratchet_delta;
  // Measured level-matching gain. See the note on kShakersLevelTarget.
  float makeup;
};

extern const ShakerPreset kShakerPresets[kShakersNumInstruments];

class ShakersEngine : public Engine {
 public:
  ShakersEngine() { }
  ~ShakersEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: the resonated instrument against the bare collisions.
  virtual bool stereo_capable() const { return true; }

 private:
  void SetInstrument(int instrument);

  int instrument_;
  float shake_energy_;
  float sound_level_;
  float current_gain_;
  float num_objects_;
  float system_decay_;

  // Per-resonance biquad state. Two poles, no zeros -- the equalizer supplies
  // the zeros once for the sum, exactly as upstream does it.
  float a1_[kShakersMaxResonances];
  float a2_[kShakersMaxResonances];
  float y1_[kShakersMaxResonances];
  float y2_[kShakersMaxResonances];
  float gain_[kShakersMaxResonances];
  float normalization_[kShakersMaxResonances];
  float frequency_[kShakersMaxResonances];
  float radius_[kShakersMaxResonances];

  float eq_x1_;
  float eq_x2_;

  // Ratchet mechanism.
  float ratchet_count_;
  float ratchet_delta_;

  // Water mechanism.
  float water_frequency_[kShakersMaxResonances];

  float note_ratio_;

  DISALLOW_COPY_AND_ASSIGN(ShakersEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SHAKERS_ENGINE_H_
