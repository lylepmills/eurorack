// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' VFOF: a bank of five narrow SVFs excited by a band-limited saw.

#include "plaits/dsp/engine2/vowel_fof_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/build_config.h"
#include "plaits/dsp/engine2/vowel_fof_data.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' InterpolateFormantParameter: bilinear across the register x vowel
// grid, per formant. The tables are vendored in Braids' own
// [register][vowel][formant] order, so x -- the OUTER index -- is the REGISTER
// and y is the vowel, exactly as `InterpolateFormantParameter(table,
// parameter_[1], parameter_[0], i)` reads them on hardware.
inline float InterpolateFormant(
    const int16_t* table, float voice_register, float vowel, int formant) {
  const float x = voice_register * static_cast<float>(kVowelFofGridSize - 1);
  const float y = vowel * static_cast<float>(kVowelFofGridSize - 1);
  int xi = static_cast<int>(x);
  int yi = static_cast<int>(y);
  CONSTRAIN(xi, 0, kVowelFofGridSize - 2);
  CONSTRAIN(yi, 0, kVowelFofGridSize - 2);
  const float xf = x - static_cast<float>(xi);
  const float yf = y - static_cast<float>(yi);
  const int stride = kVowelFofGridSize * kVowelFofNumFormants;
  const float a = static_cast<float>(
      table[xi * stride + yi * kVowelFofNumFormants + formant]);
  const float b = static_cast<float>(
      table[(xi + 1) * stride + yi * kVowelFofNumFormants + formant]);
  const float c = static_cast<float>(
      table[xi * stride + (yi + 1) * kVowelFofNumFormants + formant]);
  const float d = static_cast<float>(
      table[(xi + 1) * stride + (yi + 1) * kVowelFofNumFormants + formant]);
  const float ab = a + (b - a) * xf;
  const float cd = c + (d - c) * xf;
  return ab + (cd - ab) * yf;
}

// One formant of the bank: excite, run the Chamberlin SVF, return the bandpass
// tap. Factored out only so the two aux-mode loops below cannot drift apart --
// it is `inline` and both call sites expand it, so the split costs flash for a
// second copy but nothing at run time.
inline float RenderFormant(
    int k,
    float saw,
    float noise,
    float noise_makeup,
    float breath,
    float f,
    float* svf_lp,
    float* svf_bp) {
  const float in = saw + (noise * noise_makeup - saw) * breath;
  const float notch = in - svf_bp[k] * kVowelFofDamping;
  svf_lp[k] += f * svf_bp[k];
  CONSTRAIN(svf_lp[k], -1.0f, 1.0f);
  const float hp = notch - svf_lp[k];
  svf_bp[k] += f * hp;
  CONSTRAIN(svf_bp[k], -1.0f, 1.0f);
  // The two CONSTRAINs above ARE Braids' two `CLIP(...)`s: the state is bounded
  // to +-1 inside the resonant loop, every sample, at full scale. Braids then
  // reads that clipped state straight into the sum (`out += svf_bp[i] *
  // amplitudes[0] >> 17`) with no saturator, so there is none here either. An
  // earlier revision returned SoftClip(svf_bp[k]) on top of the CONSTRAIN,
  // which is a second nonlinearity the module does not have; it attenuates
  // well inside full scale (-0.6 dB at 0.5, -2.2 dB at 1.0) and cost 0.6-1.3 dB
  // of level.
  return svf_bp[k];
}

}  // namespace

void VowelFofEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  excitation_.Init();
  Reset();
}

void VowelFofEngine::Reset() {
  for (int i = 0; i < kVowelFofNumFormants; ++i) {
    svf_lp_[i] = 0.0f;
    svf_bp_[i] = 0.0f;
  }
  noise_state_ = 0x21f2ac31;
}

void VowelFofEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Reset();
  }

  const float frequency = NoteToFrequency(parameters.note);

  // MACRO restores the formant amplitudes Braids leaves flat. At the detent
  // the tilt is zero and every formant is voiced at unity, exactly as the
  // amplitudes[0] read does on hardware.
  const float tilt = ApplyMacro(0.0f, -0.5f, 1.0f, parameters.macro);

  const bool stereo = PLAITS_STEREO_VOWEL_FOF && parameters.stereo;

  float f[kVowelFofNumFormants];
  float raw_amplitude[kVowelFofNumFormants];
  float amplitude[kVowelFofNumFormants];
  float amplitude_aux[kVowelFofNumFormants];
  float noise_makeup[kVowelFofNumFormants];

  for (int i = 0; i < kVowelFofNumFormants; ++i) {
    // MORPH is the REGISTER (the tables' outer index) and TIMBRE the VOWEL,
    // exactly as `parameter_[1]` / `parameter_[0]` read them in Braids.
    const float note = InterpolateFormant(
        kVowelFofFrequency, parameters.morph, parameters.timbre, i) *
        (1.0f / 128.0f);
    const float formant_frequency = NoteToFrequency(note);
    // Chamberlin f = 2 sin(pi fc / fs); Sine(x) is sin(2 pi x), so the
    // argument is half the normalized frequency.
    float coefficient = 2.0f * Sine(0.5f * formant_frequency);
    CONSTRAIN(coefficient, 0.0f, 1.0f);
    f[i] = coefficient;

    const float raw = InterpolateFormant(
        kVowelFofAmplitude, parameters.morph, parameters.timbre, i) *
        (1.0f / 16384.0f);
    raw_amplitude[i] = raw;
    // Linear interpolation between flat (Braids) and the table's own balance.
    amplitude[i] = 1.0f + (raw - 1.0f) * tilt;

    // A Q = 64 bandpass has bandwidth fc/64, so noise through a high formant
    // carries far more energy than through a low one. Level it by 1/sqrt(f).
    noise_makeup[i] = Sqrt(0.02f / max(coefficient, 1e-4f));
  }

  const float breath = parameters.harmonics;

  // The stereo AUX weighting is the table's own balance REVERSED, at full
  // strength, and it does NOT depend on MACRO. That independence is the whole
  // point: `tilt` is exactly 0.0f at the detent (ApplyMacro returns `stock +
  // (max - stock) * (amount - 1)` with amount exactly 1 there), so an earlier
  // revision that scaled the reversal by `tilt` made L and R bit-identical in
  // the state the voice ships in -- a stereo engine that was mono by default.
  // OUT is untouched by any of this, so switching aux mode never changes it.
  if (stereo) {
    for (int i = 0; i < kVowelFofNumFormants; ++i) {
      amplitude_aux[i] = kVowelFofStereoMakeup *
          raw_amplitude[kVowelFofNumFormants - 1 - i];
    }
  }

  // Mono AUX carries the source that drives the bank, so it needs ONE noise
  // makeup rather than the per-formant ones -- there is no formant to level it
  // against. The mean of the five keeps the saw-to-noise crossfade at roughly
  // constant loudness across HARMONICS, which is what the per-formant makeups
  // do inside the bank.
  float source_makeup = 0.0f;
  if (!stereo) {
    for (int i = 0; i < kVowelFofNumFormants; ++i) {
      source_makeup += noise_makeup[i];
    }
    source_makeup *= 1.0f / static_cast<float>(kVowelFofNumFormants);
  }

  // Render the exciter for the whole block in one call. Asking for a single
  // sample at a time makes the oscillator rebuild its parameter interpolators
  // once per sample rather than once per block: 77% of the CPU budget against
  // 72%. The five-filter bank is the rest and is irreducible.
  float excitation[kMaxBlockSize];
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  excitation_.RenderLinearFm<OSCILLATOR_SHAPE_SAW>(
      frequency, 0.0f, parameters.frequency_offset, excitation, size);
#else
  excitation_.Render<OSCILLATOR_SHAPE_SAW>(
      frequency, 0.0f, excitation, size);
#endif

  // The two aux modes get their OWN sample loop rather than sharing one with a
  // branch inside it. The branch would sit in the FIVE-iteration formant loop,
  // where gcc 4.8 re-evaluates it per tap rather than hoisting: measured, the
  // shared-loop form cost 388.7 instructions/sample against this one's 367.9 --
  // 75% of budget against 71%, so the shared form turned a saving into a
  // REGRESSION against the 72% this engine started at, on the tightest engine
  // in the port. Keep the two loops separate.
  if (stereo) {
    for (size_t i = 0; i < size; ++i) {
      // Braids' unipolar 0..1 saw. Plaits' Oscillator builds the same 0..1 saw
      // and emits `2 * this_sample - 1`, so this undoes exactly that.
      const float saw = kVowelFofExcitationScale * (excitation[i] + 1.0f);

      noise_state_ = noise_state_ * 1664525u + 1013904223u;
      const float noise = static_cast<float>(noise_state_ >> 9) * \
          (1.0f / 8388608.0f) - 1.0f;

      float sum = 0.0f;
      float sum_aux = 0.0f;
      for (int k = 0; k < kVowelFofNumFormants; ++k) {
        const float tap = RenderFormant(
            k, saw, noise, noise_makeup[k], breath, f[k], svf_lp_, svf_bp_);
        sum += tap * amplitude[k];
        sum_aux += tap * amplitude_aux[k];
      }

      out[i] = kVowelFofOutputScale * sum;
      aux[i] = kVowelFofOutputScale * sum_aux;
    }
  } else {
    for (size_t i = 0; i < size; ++i) {
      // `source` is the DC-free form, which is what AUX wants -- a DC offset on
      // an output is a headroom loss and nothing else. `saw` adds the offset
      // back to recover Braids' unipolar 0..1 saw for the bank, where the DC is
      // load-bearing (it is what parks the lowpass state in its clipping
      // region).
      const float source = excitation[i] * kVowelFofExcitationScale;
      const float saw = source + kVowelFofExcitationScale;

      noise_state_ = noise_state_ * 1664525u + 1013904223u;
      const float noise = static_cast<float>(noise_state_ >> 9) * \
          (1.0f / 8388608.0f) - 1.0f;

      float sum = 0.0f;
      for (int k = 0; k < kVowelFofNumFormants; ++k) {
        const float tap = RenderFormant(
            k, saw, noise, noise_makeup[k], breath, f[k], svf_lp_, svf_bp_);
        sum += tap * amplitude[k];
      }

      out[i] = kVowelFofOutputScale * sum;
      // Mono AUX: the glottal source, ahead of the bank -- the saw crossfaded
      // to noise by HARMONICS, at one neutral makeup. The raw-exciter idiom
      // (inharmonic-string, modal-resonator, particle-noise), and the natural
      // sibling of speech, whose AUX is its secondary path.
      aux[i] = kVowelFofSourceGain * \
          (source + (noise * source_makeup - source) * breath);
    }
  }
}

}  // namespace plaits
