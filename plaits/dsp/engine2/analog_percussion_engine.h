// Copyright 2026 Dylan Bolink.
// SPDX-License-Identifier: MIT
//
// Analog Percussion: two decaying sine modes and a noise click — the TR-808's tuned
// percussion. Its toms, congas, rimshot and claves are four points in one
// pitch/decay plane, not four instruments.

#ifndef PLAITS_DSP_ENGINE2_ANALOG_PERCUSSION_ENGINE_H_
#define PLAITS_DSP_ENGINE2_ANALOG_PERCUSSION_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

#ifndef PLAITS_STEREO_ANALOG_PERCUSSION
#define PLAITS_STEREO_ANALOG_PERCUSSION 1
#endif

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/utils/random.h"

namespace plaits {

const float kPercussionStockCycles = 30.0f;
const float kPercussionMinCycles = 4.0f;
const float kPercussionMaxCycles = 200.0f;

const float kPercussionDecibelSpan = 4.6f;

const float kPercussionStockRatio = 3.57f;
const float kPercussionMinRatio = 1.0f;
const float kPercussionMaxRatio = 12.0f;

const float kPercussionOvertoneMax = 10.0f;
const float kPercussionShapeStart = 0.55f;
const float kPercussionOvertoneCowbell = 1.5f;
const float kPercussionCowbellRatio = 1.481f;

const float kPercussionBandRatio = 1.10f;
const float kPercussionShapeBias = 0.0f;

const float kPercussionFastTauMs = 29.0f;
const float kPercussionFastLevel = 1.0f;

const float kPercussionSlowDuck = 0.45f;
const float kPercussionVcaDrive = 1.0f;
const float kPercussionShapeDrive = 9.0f;

const float kPercussionShapeFullHz = 900.0f;
const float kPercussionShapeSilentHz = 2400.0f;

const float kPercussionBandQ = 1.4f;

const float kPercussionClickCycles = 2.5f;

// The click is noise through a HIGHPASS, not a bandpass. The 808's rimshot
// carries broadband content evenly from about 800 Hz to 11 kHz, which a band
// centred anywhere cannot cover; a highpass over the fundamental can.
const float kPercussionClickRatio = 2.0f;
const float kPercussionClickQ = 0.7f;

// Stereo placement. The fundamental and the click stay centred and only the
// second mode is spread.
const float kPercussionOvertonePan = 0.75f;
const float kPercussionClickPan = 0.25f;
const float kPercussionFundamentalPan = 0.30f;
const float kPercussionTwinPan = 0.70f;
const float kPercussionCentre = 0.707106781f;


const float kPercussionStereoDetuneCents = 12.0f;
const float kPercussionTwinPhaseOffset = 0.25f;

// Two equal sines panned apart carry twice the power of one centred, so the
// pair is trimmed back to leave stereo and mono at the same level.
const float kPercussionTwinTrim = 0.707106781f;


const float kPercussionSubRatio = 0.5f;
const float kPercussionClickDroneTrim = 0.18f;

const int kPercussionIdle = 0;

class AnalogPercussionEngine : public Engine {
 public:
  AnalogPercussionEngine() { }
  ~AnalogPercussionEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void LoadUserData(const uint8_t* user_data) { }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_ANALOG_PERCUSSION; }

 private:
  inline float Shape(float x) {
    if (shape_wave_ <= 0.0f) {
      return x;
    }
    const float shaped =
        (stmlib::SoftClip(x * shape_drive_ + shape_bias_) - shape_dc_) *
        shape_norm_;
    return x + (shaped - x) * shape_wave_;
  }

  float phase_[2];
  float phase_increment_[2];
  float mode_decay_[2];
  float mode_env_[2];

  float shape_amount_;
  float shape_wave_;
  float shape_drive_;
  float shape_bias_;
  float shape_dc_;
  float shape_norm_;

  stmlib::Svf shape_band_;
  stmlib::Svf shape_band_aux_;

  // Stereo only: a detuned copy of the fundamental, sharing its envelope.
  float twin_phase_;
  float twin_increment_;

  // AUX in mono: an octave-down strike sharing the trigger. The firmware's own
  // sub-osc option renders a bare oscillator into AUX and bypasses the LPG, so
  // it drones; this one has an envelope.
  float sub_phase_;
  float sub_increment_;
  float sub_decay_;
  float sub_env_;

  float fast_env_;
  float fast_decay_;

  float click_env_;
  float click_decay_;
  float click_level_;

  // Held across the block so a knob step lands as a ramp rather than an edge.
  float prev_gain_;
  float prev_gain_aux_;

  // Free-running (TRIG unpatched) holds the modes at a constant level instead
  // of decaying them, the same drone the stock drums fall back to.
  float sustain_gain_;

  stmlib::Svf click_filter_;

  DISALLOW_COPY_AND_ASSIGN(AnalogPercussionEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_ANALOG_PERCUSSION_ENGINE_H_
