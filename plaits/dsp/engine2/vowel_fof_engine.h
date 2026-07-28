// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' VFOF: a bank of five very narrow state-variable filters excited by
// a band-limited saw, sweeping five vowels across five vocal registers.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderVowelFof. It ends
// in `size -= 2`, so it is a 48 kHz algorithm and all three of its half-rate
// compensations drop out here: the `phase_increment_ << 1`, the `+ (12 << 7)`
// octave offset (lut_svf_cutoff is generated at 96 kHz while the bank runs
// once per two output samples), and the output averager.
//
// THE FINDING THAT JUSTIFIES THE ENGINE, confirmed in the source:
//   out += svf_bp[i] * amplitudes[0] >> 17;
// reads `amplitudes[0]`, not `amplitudes[i]` -- and column 0 of the amplitude
// table is 16384 in all 25 rows. So 100 of the 125 amplitude entries have
// been dead since 2013 and every formant is voiced flat. MACRO restores them:
// at the detent the bank is flat, which is what hardware does; above it the
// formant balance the table always intended.
//
// The amplitudes are LINEAR with 16384 as unity, not semitone attenuations,
// so the tilt is a linear interpolation between flat and true rather than a
// ratio law. Built the other way it would be shaping the wrong quantity.
//
// TIMBRE and MORPH are SWAPPED relative to Braids so that register and vowel
// land on the same knobs as `speech`. Two vocal engines in one palette with
// inverted axes is worse than a naming deviation.
//
// HARMONICS is new: Braids' excitation is a bare saw, and this crossfades it
// toward noise so the bank can whisper. The noise gets a per-formant makeup
// of 1/sqrt(f) because a Q = 64 bandpass has bandwidth fc/64 -- 9.4 Hz at a
// bass F1 against 77 Hz at a soprano F5, an 8:1 energy spread that one
// constant cannot level.
//
// OUT: the bank under the current tilt. AUX: the same five filter outputs
// summed with the formant weighting REVERSED, so it leans on the upper
// formants. That is a different weighted sum of state the engine has already
// computed, not a second bank -- a second five-filter bank is a full extra
// render path and z-filter measured what those cost.

#ifndef PLAITS_DSP_ENGINE2_VOWEL_FOF_ENGINE_H_
#define PLAITS_DSP_ENGINE2_VOWEL_FOF_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/oscillator.h"

namespace plaits {

const int kVowelFofNumFormants = 5;
const int kVowelFofGridSize = 5;

// Braids' `notch = in - (svf_bp >> 6)`: Q = 64.
const float kVowelFofDamping = 1.0f / 64.0f;

// `out += svf_bp[i] * amplitudes[0] >> 17` with amplitudes[0] = 16384 is a
// gain of 0.125 per formant.
const float kVowelFofOutputScale = 0.125f;

// Plaits' saw is bipolar and 2.0 peak-to-peak where Braids' `phase >> 17` saw
// is 0..1, so it arrives 6 dB hot.
const float kVowelFofExcitationScale = 0.5f;

class VowelFofEngine : public Engine {
 public:
  VowelFofEngine() { }
  ~VowelFofEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: two weightings of one filter bank.
  virtual bool stereo_capable() const { return true; }

 private:
  Oscillator excitation_;
  float svf_lp_[kVowelFofNumFormants];
  float svf_bp_[kVowelFofNumFormants];
  uint32_t noise_state_;

  DISALLOW_COPY_AND_ASSIGN(VowelFofEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_VOWEL_FOF_ENGINE_H_
