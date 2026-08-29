// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Natural Voice: WORLD-analyzed speech resynthesis (the "hd" format from
// research/natural_speech). Words are stored as 23-byte frames at 40 Hz -
// residual gain, F0 contour, five voicing bands, and eighteen 8-bit
// log-area-ratios - decoded through an order-18 lattice at a 16 kHz
// internal rate with MELP-style mixed excitation. (A product-VQ variant at
// 7 B/frame existed; the 2026-08-17 out-of-distribution listening test
// retired it - male and non-English voices snap audibly to the codebook.)

#ifndef PLAITS_DSP_ENGINE2_NATURAL_VOICE_ENGINE_H_
#define PLAITS_DSP_ENGINE2_NATURAL_VOICE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "stmlib/dsp/hysteresis_quantizer.h"

namespace plaits {

class NaturalVoiceEngine : public Engine {
 public:
  NaturalVoiceEngine() { }
  ~NaturalVoiceEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();

  // Supplied by Voice, exactly as the stock Speech engines are: when TRIG is
  // patched and the matching CV jack is not, the unpatched attenuverter
  // becomes the control. FM attenuverter -> prosody depth, MORPH
  // attenuverter -> playback speed. A standalone SDK build never calls
  // these, so the defaults are "as recorded" (see Reset).
  inline void set_prosody_amount(float bipolar) {
    prosody_amount_ = 1.0f + bipolar;      // 0 = monotone, 1 = as recorded
  }
  inline void set_speed(float bipolar) {
    speed_bipolar_ = bipolar;
  }
  void LoadUserData(const uint8_t* user_data) { }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);

  // OUT/AUX become a true L/R pair when the build enables it. Both paths
  // appear in both channels (see Render), so this widens the voice rather
  // than splitting it, and a mono sum does not cancel.
  virtual bool stereo_capable() const { return PLAITS_STEREO_NATURAL_VOICE; }

 private:
  enum { kOrder = 18, kBands = 5 };

  struct Biquad {
    float b0, b1, b2, a1, a2, x1, x2, y1, y2;
    void Set(float b0_, float b1_, float b2_, float a1_, float a2_) {
      b0 = b0_; b1 = b1_; b2 = b2_; a1 = a1_; a2 = a2_;
      x1 = x2 = y1 = y2 = 0.0f;
    }
    inline float Process(float x) {
      float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1; x1 = x; y2 = y1; y1 = y;
      return y;
    }
  };

  void DesignBands();
  void DecodeFrame(int frame_index, float gamma);
  void UpdateBandWeights(int band);
  float InternalTick(float f0_phase_inc, float* whisper);

  // Frame targets (decoded).
  float k_target_[kOrder];
  float v_target_[kBands];
  float gain_target_;
  float f0_st_target_;
  bool voiced_;

  // Smoothed states (internal rate).
  float k_[kOrder];
  float v_[kBands];
  float gain_;
  float f0_st_;
  int smooth_countdown_;

  // Cached band crossfade weights, refreshed with v_ (see InternalTick):
  // wp_comp_ = 1 - sqrt(v), wn_cal_ = sqrt(1 - v) * noise calibration.
  float wp_comp_[kBands];
  float wn_cal_[kBands];

  // Lattice states: main voice and the whisper (noise-only) aux path.
  float lattice_[kOrder + 1];
  float whisper_lattice_[kOrder + 1];

  // Excitation.
  // Complementary crossover lowpasses; the five bands are their successive
  // differences, so they sum to exactly the input (see DesignBands).
  Biquad pulse_lp_[kBands - 1];
  Biquad noise_lp_[kBands - 1];
  float noise_cal_[kBands];
  float period_phase_;      // in internal samples of the current period
  float period_samples_;
  int wavelet_pos_;
  float period_amp_;
  float denormal_guard_;
  float pole_damp_;
  float jitter_mul_;
  float flutter_phase_[3];
  int flutter_countdown_;
  float flutter_value_;

  // Playback.
  float prosody_amount_;
  float prosody_now_;
  float speed_bipolar_;
  int bank_;
  int words_in_bank_;
  int word_;
  int playback_frame_;      // absolute frame index, -1 = idle
  float frame_phase_;
  bool word_done_;
  int last_decoded_frame_;
  float last_decoded_gamma_;
  stmlib::HysteresisQuantizer2 bank_quantizer_;
  stmlib::HysteresisQuantizer2 word_quantizer_;

  // Output-rate resampling (2-point, like the stock speech path).
  float clock_phase_;
  float sample_[2];
  float next_sample_[2];

  DISALLOW_COPY_AND_ASSIGN(NaturalVoiceEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_NATURAL_VOICE_ENGINE_H_
