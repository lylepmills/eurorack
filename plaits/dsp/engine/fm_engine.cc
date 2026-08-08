// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Classic 2-op FM found in Braids, Rings and Elements.
//
// OUT: FM carrier. AUX: sub-oscillator (half frequency, PM'd by the carrier).
// alt firmware, stereo mode: the carrier is panned to 0.4 and the sub to 0.6 -
// a gentle octave-spread kept near centre.

#include "plaits/dsp/engine/fm_engine.h"

#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/build_config.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/dsp/downsampler/4x_downsampler.h"

namespace plaits {

using namespace stmlib;

void FMEngine::Init(BufferAllocator* allocator) {
  carrier_phase_ = 0;
  modulator_phase_ = 0;
  sub_phase_ = 0;

  previous_carrier_frequency_ = a0;
  previous_modulator_frequency_ = a0;
  previous_amount_ = 0.0f;
  previous_feedback_ = 0.0f;
  previous_sample_ = 0.0f;
  sub_fir_ = 0.0f;
  carrier_fir_ = 0.0f;
  previous_oversampling_mode_ = 0;
}

void FMEngine::Reset() {
  
}

void FMEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  if (parameters.hard_sync) {
    RenderInternal<true>(parameters, out, aux, size, already_enveloped);
  } else {
#endif
    RenderInternal<false>(parameters, out, aux, size, already_enveloped);
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  }
#endif
}

template<bool process_hard_sync>
void FMEngine::RenderInternal(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  uint32_t hard_sync = process_hard_sync ? parameters.hard_sync : 0;

  const uint8_t oversampling_mode = parameters.fm_oversampling_mode;
  const bool two_x = oversampling_mode == 2;
  const bool reduced_sub = oversampling_mode != 0;
  if (oversampling_mode != previous_oversampling_mode_) {
    const bool previous_two_x = previous_oversampling_mode_ == 2;
    if (two_x != previous_two_x) {
      const float rate_correction = two_x ? 2.0f : 0.5f;
      previous_carrier_frequency_ *= rate_correction;
      previous_modulator_frequency_ *= rate_correction;
    }
    // The 4x overlap-add history is not meaningful after changing either the
    // carrier or AUX decimation topology. A single reset is quieter than
    // leaking an old-rate tail into the first sample of the new bank.
    sub_fir_ = 0.0f;
    carrier_fir_ = 0.0f;
    previous_oversampling_mode_ = oversampling_mode;
  }

  // NoteToFrequency is normalized to the 48 kHz output rate. Four internal
  // steps therefore transpose by -24 semitones; the 2x audition uses -12.
  const float note = parameters.note - (two_x ? 12.0f : 24.0f);
  
  const float stock_ratio = Interpolate(
      lut_fm_frequency_quantizer,
      parameters.harmonics,
      128.0f);
  const float ratio = ApplyMacro(
      stock_ratio,
      stock_ratio - 12.0f,
      stock_ratio + 12.0f,
      parameters.macro);
  
  float modulator_note = note + ratio;
  float target_modulator_frequency = NoteToFrequency(modulator_note);
  CONSTRAIN(target_modulator_frequency, 0.0f, 0.5f);

  // Reduce the maximum FM index for high pitched notes, to prevent aliasing.
  float hf_taming = 1.0f - (modulator_note - 72.0f) * 0.025f;
  CONSTRAIN(hf_taming, 0.0f, 1.0f);
  hf_taming *= hf_taming;
  
  const float target_carrier_frequency = NoteToFrequency(note);
  ParameterInterpolator carrier_frequency(
      &previous_carrier_frequency_, target_carrier_frequency, size);
  ParameterInterpolator modulator_frequency(
      &previous_modulator_frequency_, target_modulator_frequency, size);
  ParameterInterpolator amount_modulation(
      &previous_amount_,
      2.0f * parameters.timbre * parameters.timbre * hf_taming,
      size);
  ParameterInterpolator feedback_modulation(
      &previous_feedback_, 2.0f * parameters.morph - 1.0f, size);
  Downsampler carrier_downsampler(&carrier_fir_);
  Downsampler sub_downsampler(&sub_fir_);

  // Equal-power pan gains for the stereo octave-spread, computed once at
  // control rate. The carrier sits slightly left of centre, the sub slightly
  // right.
  float carrier_left = 0.0f;
  float carrier_right = 0.0f;
  float sub_left = 0.0f;
  float sub_right = 0.0f;
  if ((PLAITS_STEREO_TWO_OP_FM && parameters.stereo)) {
    StereoPanGains(0.4f, &carrier_left, &carrier_right);
    StereoPanGains(0.6f, &sub_left, &sub_right);
  }

#if PLAITS_BUILD_LINEAR_TZFM
  if (parameters.linear_fm) {
    const float modulator_ratio = target_modulator_frequency /
        (target_carrier_frequency > 1.0e-9f
            ? target_carrier_frequency
            : 1.0e-9f);
    const float* linear_fm = parameters.linear_fm;
    while (size--) {
      if (process_hard_sync) {
        if (hard_sync & 1) {
          carrier_phase_ = 0;
          modulator_phase_ = 0;
          sub_phase_ = 0;
          previous_sample_ = 0.0f;
        }
        hard_sync >>= 1;
      }
      const float max_uint32 = 4294967296.0f;
      const float amount = amount_modulation.Next();
      const float feedback = feedback_modulation.Next();
      const float phase_feedback =
          feedback < 0.0f ? 0.5f * feedback * feedback : 0.0f;
      const float modulator_fb =
          feedback > 0.0f ? 0.25f * feedback * feedback : 0.0f;

      // Four internal samples are rendered per output sample, so both the
      // carrier and its audio-rate displacement are expressed per internal
      // step. Bound the modulator base to 1/3 Nyquist once here: its negative-
      // feedback multiplier is in [0.5, 1.5], which then guarantees every
      // signed inner increment remains representable without four costly
      // floating-point clamp/compare sequences in the oversampling loop.
      const float root_offset = *linear_fm++ * (two_x ? 0.5f : 0.25f);
      float carrier = carrier_frequency.Next() + root_offset;
      float modulator =
          modulator_frequency.Next() + root_offset * modulator_ratio;
      CONSTRAIN(carrier, -0.5f, 0.499999f);
      CONSTRAIN(modulator, -0.333332f, 0.333332f);
      const int32_t carrier_increment =
          static_cast<int32_t>(max_uint32 * carrier);
      float carrier_sum = 0.0f;
      float sub_sum = 0.0f;

      const size_t oversampling = two_x ? 2 : kOversampling;
      for (size_t j = 0; j < oversampling; ++j) {
        const int32_t modulator_increment = static_cast<int32_t>(
            max_uint32 * modulator *
            (1.0f + previous_sample_ * phase_feedback));
        modulator_phase_ += modulator_increment;
        carrier_phase_ += carrier_increment;
        sub_phase_ += carrier_increment / 2;
        const float modulator_sample = SinePM(
            modulator_phase_, modulator_fb * previous_sample_);
        const float carrier_sample = SinePM(
            carrier_phase_, amount * modulator_sample);
        ONE_POLE(previous_sample_, carrier_sample, 0.05f);
        if (two_x) {
          carrier_sum += 0.5f * carrier_sample;
        } else {
          carrier_downsampler.Accumulate(j, carrier_sample);
        }
        if (!reduced_sub || two_x || (j & 1)) {
          const float sub_sample = SinePM(
              sub_phase_, amount * carrier_sample * 0.25f);
          if (reduced_sub) {
            sub_sum += 0.5f * sub_sample;
          } else {
            sub_downsampler.Accumulate(j, sub_sample);
          }
        }
      }

      const float c = two_x ? carrier_sum : carrier_downsampler.Read();
      const float s = reduced_sub ? sub_sum : sub_downsampler.Read();
      if ((PLAITS_STEREO_TWO_OP_FM && parameters.stereo)) {
        *out++ = c * carrier_left + s * sub_left;
        *aux++ = c * carrier_right + s * sub_right;
      } else {
        *out++ = c;
        *aux++ = s;
      }
    }
    return;
  }
#endif

  // Keep Emilie's unsigned path whenever audio-rate linear FM is inactive.
  // The default mode preserves its 4x renderer byte-for-byte; the audition
  // modes below vary only the oversampling topology. Two-op FM is already the
  // most expensive factory engine, so paying signed-frequency overhead merely
  // because the firmware supports TZFM can push a stock patch over deadline.
  while (size--) {
    if (process_hard_sync) {
      if (hard_sync & 1) {
        // Reset the complete oscillator relationship at the output-sample
        // boundary. Keep the downsampler histories intact so the reset is
        // filtered by the existing 4x reconstruction path.
        carrier_phase_ = 0;
        modulator_phase_ = 0;
        sub_phase_ = 0;
        previous_sample_ = 0.0f;
      }
      hard_sync >>= 1;
    }
    const float max_uint32 = 4294967296.0f;
    const float amount = amount_modulation.Next();
    const float feedback = feedback_modulation.Next();
    float phase_feedback = feedback < 0.0f ? 0.5f * feedback * feedback : 0.0f;
    const float modulator_fb =
        feedback > 0.0f ? 0.25f * feedback * feedback : 0.0f;
    const uint32_t carrier_increment = static_cast<uint32_t>(
        max_uint32 * carrier_frequency.Next());
    float _modulator_frequency = modulator_frequency.Next();
    float carrier_sum = 0.0f;
    float sub_sum = 0.0f;

    const size_t oversampling = two_x ? 2 : kOversampling;
    for (size_t j = 0; j < oversampling; ++j) {
      modulator_phase_ += static_cast<uint32_t>(
          max_uint32 * _modulator_frequency *
          (1.0f + previous_sample_ * phase_feedback));
      carrier_phase_ += carrier_increment;
      sub_phase_ += carrier_increment >> 1;
      float modulator = SinePM(
          modulator_phase_, modulator_fb * previous_sample_);
      float carrier = SinePM(carrier_phase_, amount * modulator);
      ONE_POLE(previous_sample_, carrier, 0.05f);
      if (two_x) {
        carrier_sum += 0.5f * carrier;
      } else {
        carrier_downsampler.Accumulate(j, carrier);
      }
      if (!reduced_sub || two_x || (j & 1)) {
        const float sub = SinePM(sub_phase_, amount * carrier * 0.25f);
        if (reduced_sub) {
          sub_sum += 0.5f * sub;
        } else {
          sub_downsampler.Accumulate(j, sub);
        }
      }
    }

    const float c = two_x ? carrier_sum : carrier_downsampler.Read();
    const float s = reduced_sub ? sub_sum : sub_downsampler.Read();
    if ((PLAITS_STEREO_TWO_OP_FM && parameters.stereo)) {
      *out++ = c * carrier_left + s * sub_left;
      *aux++ = c * carrier_right + s * sub_right;
    } else {
      *out++ = c;
      *aux++ = s;
    }
  }
}

}  // namespace plaits
