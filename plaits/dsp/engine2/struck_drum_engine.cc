// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' DRUM: six fixed partials, two of them ring-modulated by filtered
// noise, struck once per trigger.

#include "plaits/dsp/engine2/struck_drum_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' wav_sine is not a unit-amplitude, zero-mean -cos(2*pi*phase) --
// fitted programmatically against braids/resources.cc's 256-entry table,
// wav_sine[i] = 32638 * -cos(2*pi*i/256) + 127 (max residual 1 LSB, e.g.
// entry 0 = 32638*-1+127 = -32511 against the table's -32512). Two
// deviations from unity: the table runs -0.0343 dB quiet (32638/32768 =
// 0.99609, not 1.0) and carries a +127/32768 = +0.00388 DC pedestal on
// every sample. Both are reproduced here, in addition to the quarter-cycle
// offset fold_engine.cc's BraidsSine() helper already carried (also used by
// struck_bell_engine.cc, which is unit-amplitude/zero-mean and so does not
// need this pedestal -- see that engine's own header for why).
const float kBraidsSinePhaseOffset = 0.75f;
const float kBraidsSineAmplitude = 32638.0f / 32768.0f;
const float kBraidsSineDcOffset = 127.0f / 32768.0f;

inline float BraidsSine(float phase) {
  phase += kBraidsSinePhaseOffset;
  if (phase >= 1.0f) {
    phase -= 1.0f;
  }
  // phase is explicitly wrapped above, so avoid Sine()'s redundant wrap in
  // each per-sample partial read.
  return SineNoWrap(phase) * kBraidsSineAmplitude + kBraidsSineDcOffset;
}

// digital_oscillator.cc:1029, Interpolate824(lut_svf_cutoff, cutoff << 16):
// the top 8 bits of `cutoff` (0..32767) index the table, the low 8 carry the
// interpolation fraction. Braids' `cutoff` is an INTEGER and Interpolate824
// returns a truncated uint16, so both are reproduced here: the returned
// coefficient is Braids' own raw Q15 integer, NOT a continuous float. See
// the header's TABLE VERIFICATION and NOISE-CHAIN TRUNCATION notes.
inline int32_t SvfCutoffCoefficient(float cutoff) {
  CONSTRAIN(cutoff, 0.0f, 32767.0f);
  const int cutoff_integer = static_cast<int>(cutoff);
  const int integral = cutoff_integer >> 8;
  const int fraction = cutoff_integer & 0xff;
  const int32_t a = kStruckDrumSvfCutoffTable[integral];
  const int32_t b = kStruckDrumSvfCutoffTable[integral + 1];
  // `a + ((b - a) * (fraction << 8) >> 16)`, i.e. a truncating interpolation.
  return a + ((b - a) * fraction >> 8);
}

// digital_oscillator.cc:1050-1052, one stage of the noise lowpass:
// `state += (input - state) * f >> 15`. The arithmetic right shift FLOORS,
// including for negative products, so the increment is biased down by half a
// unit on average and each stage settles at a NEGATIVE offset of roughly
// 16384/f raw units rather than at zero -- see the header's NOISE-CHAIN
// TRUNCATION note. Keeping state, input, coefficient and product in Braids'
// raw integer domain is exact and avoids software-emulated double arithmetic
// in the per-sample loop on Cortex-M4.
inline int32_t NoisePoleStep(
    int32_t state,
    int32_t input,
    int32_t coefficient) {
  return state + ((input - state) * coefficient >> 15);
}

}  // namespace

void StruckDrumEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void StruckDrumEngine::Reset() {
  for (size_t i = 0; i < kNumStruckDrumPartials; ++i) {
    amplitude_[i] = 0.0f;
    phase_[i] = 0.0f;
  }
  lp0_[0] = lp0_[1] = 0.0f;
  lp1_[0] = lp1_[1] = 0.0f;
  lp2_[0] = lp2_[1] = 0.0f;
  ever_struck_ = false;
}

void StruckDrumEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  // The decay in this engine IS the note's whole loudness envelope,
  // generated internally, exactly as in Braids -- see the header's
  // alreadyEnveloped note.
  *already_enveloped = true;

  const bool stereo = PLAITS_STEREO_STRUCK_DRUM && parameters.stereo;

  const bool unpatched = (parameters.trigger & TRIGGER_UNPATCHED) != 0;
  const bool rising_edge = (parameters.trigger & TRIGGER_RISING_EDGE) != 0;
  // TRIGGER_UNPATCHED has no Braids equivalent; this follows the sibling
  // stock engines' "unpatched = sustain" convention -- see the header note.
  const bool self_strike = unpatched && !ever_struck_;
  const bool strike = rising_edge || self_strike;

  if (strike) {
    // digital_oscillator.cc:995, a re-strike only resets phase if the
    // loudest, longest-ringing partial has already substantially died out
    // -- NOT unconditionally, unlike bell. See PARAMETER MAPPING, Strike.
    const bool reset_phase = amplitude_[0] < kStruckDrumResetPhaseThreshold;
    for (size_t i = 0; i < kNumStruckDrumPartials; ++i) {
      // Braids' RAW (un-normalised) scale -- see THE AMPLITUDE STATE IS
      // ALSO INTEGER-TRUNCATED in the header.
      amplitude_[i] = static_cast<float>(kDrumPartialAmplitude[i]);
      if (reset_phase) {
        phase_[i] = kStruckDrumStrikePhase;
      }
    }
    ever_struck_ = true;
  } else if (!unpatched) {
    // digital_oscillator.cc:994-1015: a strike sets the target amplitude
    // and is mutually exclusive with the decay update below
    // (`if (strike_) {...} else { if (parameter_[0] < 32000) {...} }`) -- a
    // strike call does NOT also decay the amplitude it just set.
    // `unpatched` suppresses decay the same way Braids' own drone threshold
    // does (TRIGGER_UNPATCHED sustain, above), so it gates this whole
    // branch rather than only the inner test.
    //
    // TIMBRE: the decay balance, applied once per call at Braids' own block
    // rate rather than per sample -- see PARAMETER MAPPING and RATE in the
    // header.
    float timbre_clamped = parameters.timbre;
    CONSTRAIN(timbre_clamped, 0.0f, 1.0f);
    const float timbre16 = floorf(timbre_clamped * 32767.0f);
    if (timbre16 < kStruckDrumDroneThreshold) {
      // digital_oscillator.cc:1008-1009, reproduced with Braids' exact
      // integer truncation (both operands are always >= 0, so
      // floor(x / 2^n) is bit-identical to the arithmetic right shift
      // Braids performs).
      const float balance_raw = floorf((32767.0f - timbre16) / 256.0f);
      const float balance = floorf((balance_raw * balance_raw) / 128.0f);

      // MACRO's Decay spread -- see THE FOURTH MACRO in the header. Copied
      // directly from struck_bell_engine.cc's identical mechanism.
      const float spread = ApplyMacro(
          kStruckDrumStockDecaySpread, kStruckDrumMinDecaySpread,
          kStruckDrumMaxDecaySpread, parameters.macro);

      float block_decay[kNumStruckDrumPartials];
      float mean_decay = 0.0f;
      for (size_t i = 0; i < kNumStruckDrumPartials; ++i) {
        const float decay_long = kDrumPartialDecayLong[i];
        const float decay_short = kDrumPartialDecayShort[i];
        // digital_oscillator.cc:1010.
        const float decay16 = decay_long -
            floorf(((decay_long - decay_short) * balance) / 128.0f);
        block_decay[i] = decay16 / 65536.0f;
        mean_decay += block_decay[i];
      }
      mean_decay /= static_cast<float>(kNumStruckDrumPartials);

      // One Braids call is always 250 us of wall-clock time
      // (kStruckDrumBlockSamples in the header). A larger caller block takes
      // the corresponding number of whole integer-truncating decay steps;
      // collapsing them into powf() would change the intermediate floors and
      // pull in a libm routine the production firmware cannot link.
      const size_t block_samples =
          static_cast<size_t>(kStruckDrumBlockSamples);
      size_t steps = (size + block_samples / 2) / block_samples;
      if (steps < 1) {
        steps = 1;
      }

      for (size_t i = 0; i < kNumStruckDrumPartials; ++i) {
        float adjusted = mean_decay + (block_decay[i] - mean_decay) * spread;
        CONSTRAIN(adjusted, 0.0f, 1.0f);
        for (size_t n = 0; n < steps; ++n) {
          // digital_oscillator.cc:1011-1012, `amp[i] * decay >> 16`
          // truncates the STATE, not just the coefficient -- see THE
          // AMPLITUDE STATE IS ALSO INTEGER-TRUNCATED in the header. The
          // +5e-4f nudge guards against a float product landing a hair
          // under the true integer (e.g. 270.99997 instead of 271.0) and
          // flooring one unit low; it is three orders of magnitude below
          // the 1.0-unit boundary this floor actually needs to resolve.
          amplitude_[i] = floorf(amplitude_[i] * adjusted + 5e-4f);
        }
      }
    }
  }

  // COLOR: noise cutoff, harmonics gain and the noise-mode crossfade, all
  // three driven from the SAME parameter_[1] Braids couples together -- see
  // PARAMETER MAPPING and MIX WEIGHTS in the header.
  float harmonics_clamped = parameters.harmonics;
  CONSTRAIN(harmonics_clamped, 0.0f, 1.0f);
  const float color16 = floorf(harmonics_clamped * 32767.0f);

  // digital_oscillator.cc:1023, `(pitch_ - 12*128) + (parameter_[1] >> 2)`.
  // R6's correction is added by hand here because this path is a Braids
  // pitch-space table lookup, not `NoteToFrequency` (which the oscillator
  // partials below use instead, picking the correction up for free).
  const float pitch128 =
      (parameters.note + kStruckDrumPitchCorrection) * 128.0f;
  const float color_shifted = floorf(color16 / 4.0f);  // >> 2
  const float cutoff = pitch128 - 12.0f * 128.0f + color_shifted;
  const int32_t noise_coefficient = SvfCutoffCoefficient(cutoff);

  // digital_oscillator.cc:1033.
  const float harmonics_gain = (color16 < kStruckDrumHarmonicsGainThreshold
      ? (color16 + kStruckDrumHarmonicsGainFloor)
      : 16384.0f) / 16384.0f;

  // digital_oscillator.cc:1034-1035. Computed in double: the intermediate
  // product (up to ~2.1e8) exceeds float's exact-integer range, and this
  // feeds a floor() -- see the header's TABLE VERIFICATION note on why that
  // level of care matters for a truncating computation even when (as here)
  // the quantity being truncated is a smooth timbral control, not a table
  // index.
  const double noise_mode_gain_scaled =
      std::max(0.0, static_cast<double>(color16) - 16384.0) *
      static_cast<double>(kStruckDrumNoiseModeGainScale) / 16384.0;
  const float noise_mode_gain =
      static_cast<float>(std::floor(noise_mode_gain_scaled));
  const float noise_mode_gain_norm = noise_mode_gain / 16384.0f;

  // Per-channel mix weights (see the header's MIX WEIGHTS and OUT/AUX
  // stereo notes). [0] = mono/left, [1] = right (stereo only).
  float mix1[2] = { 0.0f, 0.0f };
  float mix2[2] = { 0.0f, 0.0f };
  {
    float gain_norm = noise_mode_gain_norm -
        (stereo ? kStruckDrumStereoBlend : 0.0f);
    CONSTRAIN(gain_norm, 0.0f, 1.0f);
    mix1[0] = kStruckDrumNoiseModeMixConstant / 16384.0f - gain_norm;
    mix2[0] = gain_norm;
  }
  if (stereo) {
    float gain_norm = noise_mode_gain_norm + kStruckDrumStereoBlend;
    CONSTRAIN(gain_norm, 0.0f, 1.0f);
    mix1[1] = kStruckDrumNoiseModeMixConstant / 16384.0f - gain_norm;
    mix2[1] = gain_norm;
  }

  // MORPH: Spread, scaling all six fixed partial ratios together -- see THE
  // FOURTH MACRO in the header.
  const float spread_scale = parameters.morph * 2.0f;
  float increment[kNumStruckDrumPartials];
  for (size_t i = 0; i < kNumStruckDrumPartials; ++i) {
    const float ratio_semitones =
        (kDrumPartials[i] / 128.0f) * spread_scale;
    increment[i] = NoteToFrequency(parameters.note + ratio_semitones);
  }

  const int num_channels = stereo ? 2 : 1;

  for (size_t s = 0; s < size; ++s) {
    // The noise-lowpass chain: independent per channel in stereo (R14, see
    // particle_burst_engine.h), shared (channel 0 only) in mono.
    for (int channel = 0; channel < num_channels; ++channel) {
      // Braids' raw integer scale throughout (digital_oscillator.cc:1043-1052),
      // truncated every stage exactly as its `>> 15` does.
      int32_t noise = Random::GetSample();
      CONSTRAIN(noise, -16384, 16384);
      lp0_[channel] = NoisePoleStep(lp0_[channel], noise, noise_coefficient);
      lp1_[channel] = NoisePoleStep(lp1_[channel], lp0_[channel],
                                    noise_coefficient);
      lp2_[channel] = NoisePoleStep(lp2_[channel], lp1_[channel],
                                    noise_coefficient);
    }

    // The six tonal partials -- shared between channels, only the noise
    // chain above differs L/R.
    float partial[kNumStruckDrumPartials];
    float harmonics_sum = 0.0f;
    for (size_t i = 0; i < kNumStruckDrumPartials; ++i) {
      phase_[i] += increment[i];
      if (phase_[i] >= 1.0f) {
        phase_[i] -= 1.0f;
      }
      // /65536.0f: amplitude_ is carried in Braids' raw integer-truncated
      // scale (see THE AMPLITUDE STATE IS ALSO INTEGER-TRUNCATED, header).
      partial[i] = BraidsSine(phase_[i]) * (amplitude_[i] / 65536.0f);
      harmonics_sum += partial[i];
    }

    // digital_oscillator.cc:1065-1070: partials[0] direct, the two
    // noise-ring-modulated partials crossfaded by COLOR, the six-partial
    // sum at harmonics_gain.
    // lp2_ is Braids' raw integer scale, so /32768 at the point of use --
    // `partials[1] * lp_state_2 >> 8` is 128x the normalised product, and
    // `>> 9` is 64x.
    const float lp2_left = lp2_[0] / 32768.0f;
    const float noise_mode_1 = partial[1] * lp2_left * 128.0f;
    const float noise_mode_2 = partial[3] * lp2_left * 64.0f;
    const float noise_term_l = noise_mode_1 * mix1[0] + noise_mode_2 * mix2[0];

    float out_sample = partial[0] + noise_term_l + harmonics_sum * harmonics_gain;
    // digital_oscillator.cc:1071, Braids' CLIP to +-32767 -- CONSTRAIN to
    // +-1.0f is the same bound to within 1/32768 (SPEC R2: a hard clip, not
    // a soft one, matches what Braids actually does here).
    CONSTRAIN(out_sample, -1.0f, 1.0f);
    *out++ = out_sample;

    if (stereo) {
      const float lp2_right = lp2_[1] / 32768.0f;
      const float noise_mode_1_r = partial[1] * lp2_right * 128.0f;
      const float noise_mode_2_r = partial[3] * lp2_right * 64.0f;
      const float noise_term_r =
          noise_mode_1_r * mix1[1] + noise_mode_2_r * mix2[1];
      float aux_sample =
          partial[0] + noise_term_r + harmonics_sum * harmonics_gain;
      CONSTRAIN(aux_sample, -1.0f, 1.0f);
      *aux++ = aux_sample;
    } else {
      // Mono AUX: the noise-ring-modulated component alone -- see the
      // header's OUT/AUX note.
      float aux_sample = noise_term_l;
      CONSTRAIN(aux_sample, -1.0f, 1.0f);
      *aux++ = aux_sample;
    }
  }
}

}  // namespace plaits
