// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' FOLD model: a sine wavefolder and a triangle wavefolder crossfaded
// against each other, 4x oversampled and decimated.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderSineTriangle driving
// AnalogOscillator::RenderSineFold and RenderTriangleFold.
//
// LINEAGE. This is the Braids model that Plaits' waveshaping engine descends
// from, and the two are not the same instrument. Waveshaping runs ONE shaper
// -- a wavefolder into a wave shaper, with MORPH sweeping the shape and a
// triangle-to-sine source -- and folds a signal whose amplitude is already
// bounded. FOLD runs TWO fixed folder curves at once and crossfades them, and
// the curves are the sound: tri_fold is sin(pi*(3x + (2x)^3)), which keeps
// adding lobes as it is driven, while sine_fold is a windowed sin(8*pi*x)
// blended into atan(3x) at the edges, so it saturates rather than folding once
// the drive runs past the window. Sweeping between them is a motion the
// Plaits engine has no control for.
//
// NOTE ON THE BRAIDS CONTROLS, and a trap in reading them. TIMBRE sets the
// fold depth for both folders (gain = 2048 + p1 * 30720 >> 15, so 1/16 to full
// scale). COLOR sets the crossfade between them.
//
// The trap: RenderSineTriangle reads `timbre = parameter_[0]` for the depth and
// then crossfades with `BEGIN_INTERPOLATE_PARAMETER_1` / `balance =
// parameter_1 << 1`. The name says parameter_1 but the macro interpolates
// previous_parameter_[1] -> parameter_[1], which is COLOR. Read as if the
// suffix meant "the first parameter", the model looks like it drives depth and
// blend from TIMBRE together and never touches COLOR at all -- and it renders
// perfectly plausibly that way. The first A/B of this port measured the
// difference: the fundamental came out 14.8 dB down with the third harmonic
// on top of it, where Braids has the fundamental strongest and the third
// 3.7 dB below. Getting it right puts h1, h3 and h5 within 0.4 dB.
//
//   HARMONICS  Blend      Braids' COLOR: sine folder through to triangle.
//   TIMBRE     Fold       Braids' TIMBRE: the depth driving both folders.
//   MORPH      Symmetry   a DC offset into the folder ahead of the shaper.
//                         Braids has none; a folder without one can only make
//                         odd harmonics from its symmetric source.
//   MACRO      Drive      pre-fold gain, 0.5x to 2.5x around Braids' 1.0, so
//                         the folder can be pushed past where the module stops.
//
// RATE. RenderSineTriangle does NOT end in `size -= 2`: it is a 96 kHz
// algorithm, and each folder is internally 2x oversampled, so Braids folds at
// 192 kHz. The port runs 4x from 48 kHz to reach the same 192 kHz internal
// rate (SPEC R5), and decimates through Plaits' 8-tap overlap-add
// lut_4x_downsampler_fir rather than Braids' 2-tap box average, which is
// -6.0 dB at 24 kHz and does very little about the folder's upper lobes.
//
// The pitch-dependent depth guard is re-derived rather than transplanted.
// Braids attenuates the triangle folder above MIDI 80 and the sine folder
// above MIDI 92, both reaching zero over roughly a fourth. Those thresholds
// are anti-aliasing guards expressed against a 192 kHz internal rate, which
// the port shares -- so they transfer verbatim, and the decimator differing is
// what the measurement below covers. Transplanting them WITHOUT checking the
// internal rates matched would have been the R5 mistake that cost fluted.
//
// OUT: the crossfaded pair. AUX (mono): the complementary crossfade, so it is
// always the folder OUT is not favouring. In stereo OUT/AUX become L/R and the
// two sides take a small opposing blend offset.
//
// Declared deviations from Braids:
//   - the decimator is the 8-tap overlap-add FIR, not Braids' 2-tap box.
//   - the folder curves are evaluated from 257-entry tables as Braids does,
//     but held as float rather than int16, so the port does not reproduce
//     Braids' per-sample quantisation of the shaper output.
//   - Braids' `>> 1` halving of each oversampled pair is subsumed into the
//     decimator's unity-DC-gain kernel.

#ifndef PLAITS_DSP_ENGINE2_FOLD_ENGINE_H_
#define PLAITS_DSP_ENGINE2_FOLD_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// gain = 2048 + parameter * 30720 >> 15, normalized: 1/16 at TIMBRE 0, full
// scale at TIMBRE 1.
const float kFoldMinDepth = 2048.0f / 32768.0f;
const float kFoldDepthRange = 30720.0f / 32768.0f;

// Braids' guards: attenuation_tri = 32767 - 7 * (pitch - 80 * 128) and
// attenuation_sine = 32767 - 6 * (pitch - 92 * 128), both clamped to 0..32767
// and applied to the depth. Expressed here in MIDI notes.
const float kFoldTriGuardNote = 80.0f;
const float kFoldTriGuardSpan = 32767.0f / (7.0f * 128.0f);
const float kFoldSineGuardNote = 92.0f;
const float kFoldSineGuardSpan = 32767.0f / (6.0f * 128.0f);

// Symmetry travels +/- half of full scale, which is enough to push the source
// entirely into one half of the folder curve.
const float kFoldMaxSymmetry = 0.5f;

// The two channels take opposing blend offsets in stereo. Small, because the
// crossfade is the model's whole gesture and a wide split would make the two
// sides different models rather than a stereo image of one.
const float kFoldStereoBlend = 0.12f;

class FoldEngine : public Engine {
 public:
  FoldEngine() { }
  ~FoldEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool linear_tzfm_capable() const { return true; }
  virtual bool stereo_capable() const { return PLAITS_STEREO_FOLD; }

 private:
  float phase_;
  float frequency_;

  float depth_;
  float blend_;
  float symmetry_;
  float drive_;

  float downsampler_state_;
  float downsampler_state_aux_;

  float dc_input_;
  float dc_input_aux_;

  DISALLOW_COPY_AND_ASSIGN(FoldEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_FOLD_ENGINE_H_
