// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' CYMBAL: six comparator-summed square oscillators through a
// resonant bandpass, crossfaded against clocked sample-and-held noise
// through a resonant highpass.

#include "plaits/dsp/engine2/cymbal_engine.h"

#include <cmath>

#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

namespace plaits {

using namespace stmlib;

namespace {

// The Chamberlin recursion braids/svf.h:78-97 runs in integer, reproduced
// here in float. Both variants clip `lp`/`bp` STATE after every step
// (svf.h:93,96), on top of the caller's own clip of whichever output is
// actually read (digital_oscillator.cc:2505,2509) -- matching
// kick_engine.cc's own port of the same recursion rather than
// stmlib::NaiveSvf, whose only accurate-frequency path fixes damp at
// `1 / resonance` and so cannot hold Braids' two REALIZED damp constants
// this engine needs (see cymbal_engine.h TABLES).
inline float ChamberlinBandpass(
    float* lp, float* bp, float in, float f, float damp) {
  const float notch = in - *bp * damp;
  *lp += f * (*bp);
  CONSTRAIN(*lp, -1.0f, 1.0f);
  const float hp = notch - *lp;
  *bp += f * hp;
  CONSTRAIN(*bp, -1.0f, 1.0f);
  return *bp;
}

inline float ChamberlinHighpass(
    float* lp, float* bp, float in, float f, float damp) {
  const float notch = in - *bp * damp;
  *lp += f * (*bp);
  CONSTRAIN(*lp, -1.0f, 1.0f);
  float hp = notch - *lp;
  *bp += f * hp;
  CONSTRAIN(*bp, -1.0f, 1.0f);
  CONSTRAIN(hp, -1.0f, 1.0f);
  return hp;
}

}  // namespace

void CymbalEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void CymbalEngine::Reset() {
  for (int p = 0; p < 6; ++p) {
    partial_phase_[p] = 0.0f;
  }
  bp_lp_ = 0.0f;
  bp_bp_ = 0.0f;
  for (int channel = 0; channel < 2; ++channel) {
    noise_clock_[channel] = 0.0f;
    hp_lp_[channel] = 0.0f;
    hp_bp_[channel] = 0.0f;
  }
  // Channel 0 starts where Braids' own `hat.rng_state` starts: 0, from
  // DigitalOscillator::Init()'s `memset(&state_, 0, ...)`
  // (digital_oscillator.h:247). Channel 1 -- the stereo-only stream -- MUST
  // start somewhere else: the two channels' clocks are driven by the same
  // root note and step in lockstep, so an LCG seeded identically would stay
  // bit-identical forever and the "second, independent raw-noise stream"
  // would be the first one twice. Any non-zero seed decorrelates it (the LCG
  // has full 2^32 period); this one is the golden-ratio constant. Seeding at
  // Reset costs the mono path nothing -- channel 0's sequence is untouched
  // (SPEC R14).
  rng_state_[0] = 0u;
  rng_state_[1] = 0x9e3779b9u;
  xfade_ = 0.5f;
}

void CymbalEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  // RenderCymbal has no envelope of its own -- see cymbal_engine.h's opening
  // note. Plaits' own voice-level DECAY/TRIGGER envelope does the enveloping
  // a Cymbal patch is played with, the same as Fold and Particle Burst.
  *already_enveloped = false;

  const bool stereo = PLAITS_STEREO_CYMBAL && parameters.stereo;
  const int num_channels = stereo ? 2 : 1;

  // Root note: digital_oscillator.cc:2468, `(40 << 7) + (pitch_ >> 1)` --
  // 40 semitones plus HALF the played note. NoteToFrequency folds in
  // kCorrectedSampleRate (SPEC R6); see cymbal_engine.h PITCH.
  const float frequency0 = NoteToFrequency(
      kCymbalRootOffsetSemitones + parameters.note * 0.5f);

  // MORPH: Spread. 1.0x (MORPH 0.5) is the module's own voicing
  // (digital_oscillator.cc:2472-2476).
  const float spread = parameters.morph * kCymbalSpreadMax;

  float increment[6];
  increment[0] = frequency0;
  increment[1] = frequency0 * (1.0f + (kCymbalPartialRatio1 - 1.0f) * spread);
  increment[2] = frequency0 * (1.0f + (kCymbalPartialRatio2 - 1.0f) * spread);
  increment[3] = frequency0 * (1.0f + (kCymbalPartialRatio3 - 1.0f) * spread);
  increment[4] = frequency0 * (1.0f + (kCymbalPartialRatio4 - 1.0f) * spread);
  increment[5] = frequency0 * (1.0f + (kCymbalPartialRatio5 - 1.0f) * spread);

  // digital_oscillator.cc:2477: the raw-noise clock fires 24x per root cycle.
  const float noise_clock_increment = frequency0 * kCymbalNoiseClockMultiplier;

  // TIMBRE: cutoff, shared by both filters (digital_oscillator.cc:2480-2481).
  // The SVF runs 2x oversampled (below) at Braids' OWN 96 kHz operating
  // rate, so the coefficient is computed directly against that rate with
  // Braids' own 1/8 cap and reads Braids' own literal 12 kHz ceiling --
  // see cymbal_engine.h RATE. `sinf` is called directly, matching
  // kick_engine.cc.
  const float note_index = parameters.timbre * kCymbalToneIndexRange;
  float cutoff_hz = 440.0f * SemitonesToRatio(note_index - 69.0f);
  const float cutoff_cap_hz = kCymbalOversampledRate * kCymbalCutoffCapFraction;
  if (cutoff_hz > cutoff_cap_hz) {
    cutoff_hz = cutoff_cap_hz;
  }
  const float f_norm = cutoff_hz / kCymbalOversampledRate;
  const float f = 2.0f * sinf(float(M_PI) * f_norm);

  // MACRO: Resonance, scaling both hardwired damp constants together
  // (cymbal_engine.h TABLES/THE FOURTH MACRO).
  const float resonance_scale = ApplyMacro(
      1.0f, kCymbalResonanceMinScale, kCymbalResonanceMaxScale,
      parameters.macro);
  const float damp_bp = kCymbalDampBandpassStock * resonance_scale;
  const float damp_hp = kCymbalDampHighpassStock * resonance_scale;

  ParameterInterpolator xfade_modulation(&xfade_, parameters.harmonics, size);

  float partial_phase[6];
  for (int p = 0; p < 6; ++p) {
    partial_phase[p] = partial_phase_[p];
  }
  float bp_lp = bp_lp_;
  float bp_bp = bp_bp_;

  float noise_clock[2];
  uint32_t rng_state[2];
  float hp_lp[2];
  float hp_bp[2];
  for (int channel = 0; channel < num_channels; ++channel) {
    noise_clock[channel] = noise_clock_[channel];
    rng_state[channel] = rng_state_[channel];
    hp_lp[channel] = hp_lp_[channel];
    hp_bp[channel] = hp_bp_[channel];
  }

  for (size_t i = 0; i < size; ++i) {
    // 2x oversampled: the SVF coefficient (f, above) targets Braids' own
    // 96 kHz operating point, so the recursion has to run at that same
    // internal rate to land on the same (f, damp) OPERATING POSITION rather
    // than a stability-forced substitute -- see cymbal_engine.h RATE. Each
    // of the two substeps advances every phase by HALF the per-output-sample
    // increment; the two substeps' filtered outputs are averaged (a plain
    // 2-tap box decimator, a declared deviation from Braids -- see the
    // header) into one output-rate sample.
    float hat_noise_sum = 0.0f;
    for (int j = 0; j < 2; ++j) {
      // The six comparator phases, shared by both channels (cymbal_engine.h's
      // stereo note explains why the metallic side is never duplicated).
      for (int p = 0; p < 6; ++p) {
        partial_phase[p] += increment[p] * 0.5f;
        if (partial_phase[p] >= 1.0f) {
          partial_phase[p] -= 1.0f;
        }
      }

      int comparator_sum = 0;
      for (int p = 0; p < 6; ++p) {
        if (partial_phase[p] >= 0.5f) {
          ++comparator_sum;
        }
      }
      // digital_oscillator.cc:2495-2503: sum (0..6), re-centre by -3, scale
      // by 5461/32768.
      const float raw_metallic =
          static_cast<float>(comparator_sum - 3) * kCymbalMetallicScale;

      hat_noise_sum += ChamberlinBandpass(
          &bp_lp, &bp_bp, raw_metallic, f, damp_bp);
    }
    const float hat_noise = hat_noise_sum * 0.5f;

    const float xfade = xfade_modulation.Next();

    for (int channel = 0; channel < num_channels; ++channel) {
      float noise_filtered_sum = 0.0f;
      for (int j = 0; j < 2; ++j) {
        // digital_oscillator.cc:2484-2487: the clock wraps (possibly more
        // than once at very high root notes, hence `while` rather than
        // `if`) and each wrap draws a fresh LCG word.
        noise_clock[channel] += noise_clock_increment * 0.5f;
        while (noise_clock[channel] >= 1.0f) {
          noise_clock[channel] -= 1.0f;
          rng_state[channel] = rng_state[channel] * 1664525u + 1013904223u;
        }
        // digital_oscillator.cc:2507: the TOP 16 bits of the LCG word, held
        // between wraps, re-centred and normalized.
        const float raw_noise =
            (static_cast<float>((rng_state[channel] >> 16) & 0xffffu) -
             32768.0f) / 32768.0f;

        noise_filtered_sum += ChamberlinHighpass(
            &hp_lp[channel], &hp_bp[channel], raw_noise * 0.5f, f, damp_hp);
      }
      const float noise_filtered = noise_filtered_sum * 0.5f;

      if (channel == 0) {
        if (stereo) {
          float xfade_left = xfade - kCymbalStereoBlend;
          CONSTRAIN(xfade_left, 0.0f, 1.0f);
          out[i] = hat_noise + (noise_filtered - hat_noise) * xfade_left;
        } else {
          // digital_oscillator.cc:2511, Braids' own output, verbatim.
          out[i] = hat_noise + (noise_filtered - hat_noise) * xfade;
          // AUX: the complementary crossfade (fold_engine.h's idiom).
          aux[i] = hat_noise + (noise_filtered - hat_noise) * (1.0f - xfade);
        }
      } else {
        float xfade_right = xfade + kCymbalStereoBlend;
        CONSTRAIN(xfade_right, 0.0f, 1.0f);
        aux[i] = hat_noise + (noise_filtered - hat_noise) * xfade_right;
      }
    }
  }

  for (int p = 0; p < 6; ++p) {
    partial_phase_[p] = partial_phase[p];
  }
  bp_lp_ = bp_lp;
  bp_bp_ = bp_bp;
  for (int channel = 0; channel < num_channels; ++channel) {
    noise_clock_[channel] = noise_clock[channel];
    rng_state_[channel] = rng_state[channel];
    hp_lp_[channel] = hp_lp[channel];
    hp_bp_[channel] = hp_bp[channel];
  }
}

}  // namespace plaits
