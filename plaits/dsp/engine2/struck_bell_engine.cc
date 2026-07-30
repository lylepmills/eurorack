// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' BELL: eleven fixed inharmonic partials, struck once per trigger.

#include "plaits/dsp/engine2/struck_bell_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' wav_sine is -cos(2*pi*phase), not Plaits' sin(2*pi*phase)
// lut_sine (verified: wav_sine peaks -32512 at phase 0, +32766 at phase
// 0.5). Phase-shift copied verbatim from fold_engine.cc; the amplitude
// scale and DC pedestal below are struck-bell-specific -- see the wav_sine
// note in the header for the measurement that keeps them.
// wav_sine[i] = 32638 * -cos(2*pi*i/256) + 127, fit programmatically against
// the real 257-entry table (braids/resources.cc:1461) to within 1 LSB (the
// residual is the table's own dither noise -- braids/resources/waveforms.py's
// scale() runs an order-2 noise-shaped quantizer, not a plain round()). Ran
// the A/B with and without this: WITH it, AC RMS residual drops from
// +0.01..+0.06 dB (every case hot) to -0.00..+0.03 dB, and spectrum error
// drops in all seven cases (e.g. decay-long 0.10 -> 0.03 dB) -- kept; see the
// header for the full before/after table.
const float kBellWavSineScale = 32638.0f / 32768.0f;
const float kBellWavSinePedestal = 127.0f / 32768.0f;

}  // namespace

void StruckBellEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  stereo_allpass_.Init();
  Reset();
}

void StruckBellEngine::Reset() {
  for (size_t i = 0; i < kNumBellPartials; ++i) {
    amplitude_[i] = 0;
    phase_sin_[i] = 0.0f;
    phase_cos_[i] = 1.0f;
  }
  ever_struck_ = false;
}

void StruckBellEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  // The decay in this engine IS the note's whole loudness envelope,
  // generated internally exactly as Braids' module has no separate VCA
  // stage -- see the header note on voice.cc's lpg_bypass.
  *already_enveloped = true;

  const bool stereo = PLAITS_STEREO_STRUCK_BELL && parameters.stereo;

  const bool unpatched = (parameters.trigger & TRIGGER_UNPATCHED) != 0;
  const bool rising_edge = (parameters.trigger & TRIGGER_RISING_EDGE) != 0;
  // TRIGGER_UNPATCHED has no Braids equivalent; this follows the sibling
  // stock engines' "unpatched = sustain" convention -- see the header note.
  const bool self_strike = unpatched && !ever_struck_;
  const bool strike = rising_edge || self_strike;

  if (strike) {
    // MORPH's Brightness tilt applies only at strike -- see THE FOURTH
    // MACRO in the header. At MORPH == 0.5, tilt == 0 and every gain below
    // is exactly 1.0, i.e. Braids' own fixed amplitudes untouched.
    const float tilt = (parameters.morph - 0.5f) * 2.0f;
    const float half_span = (kNumBellPartials - 1) * 0.5f;
    for (size_t i = 0; i < kNumBellPartials; ++i) {
      const float normalized_index =
          (static_cast<float>(i) - half_span) / half_span;
      float gain = 1.0f + tilt * normalized_index * kBellBrightnessDepth;
      CONSTRAIN(gain, 0.0f, 3.0f);
      // digital_oscillator.cc:873. The amplitude state is Braids' own int32
      // Q15 counter, not a normalised float -- see AMPLITUDE STATE in the
      // header. At MORPH == 0.5 the gain is exactly 1.0f, so this is the
      // table entry unchanged.
      amplitude_[i] = static_cast<int32_t>(kBellPartialAmplitudes[i] * gain);
      // digital_oscillator.cc:874, `(1L << 30)` of a 32-bit phase
      // accumulator -- a quarter cycle, not phase zero (see the header).
      phase_sin_[i] = 1.0f;
      phase_cos_[i] = 0.0f;
    }
    ever_struck_ = true;
  }

  // TIMBRE: the decay balance, applied once per call at Braids' own block
  // rate rather than per sample -- see PARAMETER MAPPING and RATE in the
  // header for the derivation. `unpatched` suppresses decay the same way
  // Braids' own drone threshold does (TRIGGER_UNPATCHED sustain, above).
  const float timbre16 = floorf(parameters.timbre * 32767.0f);
  if (!unpatched && timbre16 < kBellDroneThreshold) {
    // digital_oscillator.cc:898-899, reproduced with Braids' exact integer
    // truncation (both operands are always >= 0, so floor(x / 2^n) is
    // bit-identical to the arithmetic right shift Braids performs).
    const float balance_raw = floorf((32767.0f - timbre16) / 256.0f);
    const float balance = floorf((balance_raw * balance_raw) / 128.0f);

    // MACRO's Decay spread -- see THE FOURTH MACRO in the header.
    const float spread = ApplyMacro(
        kBellStockDecaySpread, kBellMinDecaySpread, kBellMaxDecaySpread,
        parameters.macro);

    // The decay multipliers stay in Braids' Q16 integer domain: the
    // truncation below only reproduces if the multiplier is the same integer
    // Braids computes.
    int32_t block_decay[kNumBellPartials];
    int32_t decay_total = 0;
    for (size_t i = 0; i < kNumBellPartials; ++i) {
      const float decay_long = kBellPartialDecayLong[i];
      const float decay_short = kBellPartialDecayShort[i];
      // digital_oscillator.cc:900.
      const float decay16 = decay_long -
          floorf(((decay_long - decay_short) * balance) / 128.0f);
      block_decay[i] = static_cast<int32_t>(decay16);
      decay_total += block_decay[i];
    }
    const float mean_decay =
        static_cast<float>(decay_total) / static_cast<float>(kNumBellPartials);

    // One Braids call to RenderStruckBell is always 24 samples at 96 kHz =
    // 250 us, and Plaits' kBlockSize is the same 250 us, so the common case
    // is exactly one step per call; a larger caller block takes
    // proportionally more STEPS rather than a fractional power, because the
    // truncation below is not a smooth exponential and powf() of it would
    // not land on the same integers (see AMPLITUDE STATE in the header).
    const size_t block_samples = static_cast<size_t>(kBellBlockSamples);
    size_t steps = (size + block_samples / 2) / block_samples;
    if (steps < 1) {
      steps = 1;
    }

    // Written as a deviation FROM each partial's own decay rather than
    // toward it, so at the stock spread of exactly 1.0f the correction term
    // is exactly 0.0f and decay16 is bit-identical to Braids' integer --
    // no rounding slack in the path every stock render takes. (Doubles would
    // also do it, but they are software-emulated on the M4.)
    const float pull = 1.0f - spread;
    for (size_t i = 0; i < kNumBellPartials; ++i) {
      float adjusted = static_cast<float>(block_decay[i]) +
          (mean_decay - static_cast<float>(block_decay[i])) * pull;
      CONSTRAIN(adjusted, 0.0f, 65536.0f);
      const int32_t decay16 = static_cast<int32_t>(adjusted + 0.5f);
      for (size_t n = 0; n < steps; ++n) {
        // digital_oscillator.cc:901-902, reproduced exactly: the state is an
        // int32 and `>> 16` is an arithmetic shift, so every block truncates
        // DOWN and the partial reaches EXACT zero in finite time. A float
        // `amplitude *= decay / 65536` instead decays smoothly and never
        // reaches zero -- measurably longer (+2.4 dB AC RMS over a 10 s
        // strike at TIMBRE 0.93). The int64 intermediate only matters when
        // MORPH pushes the strike amplitude past Braids' own table range.
        amplitude_[i] = static_cast<int32_t>(
            (static_cast<int64_t>(amplitude_[i]) * decay16) >> 16);
      }
    }
  }

  // COLOR: the odd/even detune, recomputed for all eleven partials every
  // call (a declared deviation from Braids' 3-per-call stagger -- see
  // PARAMETER MAPPING in the header) using Braids' own integer truncation.
  const float color16 = floorf(parameters.harmonics * 32767.0f);
  const float detune_units = floorf(color16 / 128.0f);
  const float detune_semitones = detune_units / 128.0f;

  float increment_sin[kNumBellPartials];
  float increment_cos[kNumBellPartials];
  // Braids' int32 Q15 amplitude, read out as the float gain the sample loop
  // wants. Held constant across the block exactly as Braids holds it.
  float partial_gain[kNumBellPartials];
  for (size_t i = 0; i < kNumBellPartials; ++i) {
    partial_gain[i] = static_cast<float>(amplitude_[i]) * (1.0f / 32768.0f);
    const float ratio_semitones = kBellPartials[i] / 128.0f;
    const float sign = (i & 1) ? 1.0f : -1.0f;
    const float base_note = parameters.note + ratio_semitones +
        sign * detune_semitones;
    const float increment = NoteToFrequency(base_note);
    increment_sin[i] = Sine(increment);
    increment_cos[i] = Sine(increment + 0.25f);
    const float rotation_norm = 1.0f / Sqrt(
        increment_sin[i] * increment_sin[i] +
        increment_cos[i] * increment_cos[i]);
    increment_sin[i] *= rotation_norm;
    increment_cos[i] *= rotation_norm;
  }

  for (size_t s = 0; s < size; ++s) {
    float sum_l = 0.0f;
    float upper = 0.0f;
    for (size_t i = 0; i < kNumBellPartials; ++i) {
      const float phase_sin = phase_sin_[i];
      const float phase_cos = phase_cos_[i];
      phase_sin_[i] = phase_sin * increment_cos[i] +
          phase_cos * increment_sin[i];
      phase_cos_[i] = phase_cos * increment_cos[i] -
          phase_sin * increment_sin[i];
      const float contribution =
          (-phase_cos_[i] * kBellWavSineScale + kBellWavSinePedestal) *
          partial_gain[i];
      sum_l += contribution;
      // The five highest partials (+12 st and up) -- AUX's mono voice, see
      // the header's OUT/AUX note.
      if (i >= kNumBellPartials - 5) {
        upper += contribution;
      }
    }

    // digital_oscillator.cc:914, Braids' CLIP to +-32767 -- CONSTRAIN to
    // +-1.0f is the same bound to within 1/32768 (SPEC R2: a hard clip, not
    // a soft one, matches what Braids actually does here).
    float out_sample = sum_l * kBellOutputScale;
    CONSTRAIN(out_sample, -1.0f, 1.0f);
    *out++ = out_sample;

    float aux_sample = stereo
        ? stereo_allpass_.Process(out_sample)
        : upper * kBellOutputScale;
    CONSTRAIN(aux_sample, -1.0f, 1.0f);
    *aux++ = aux_sample;
  }
}

}  // namespace plaits
