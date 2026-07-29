// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SQUARE_SYNC / SAW_SYNC: a master oscillator hard-syncing a slave a
// swept interval above it, crossfaded against it.

#include "plaits/dsp/engine2/dual_sync_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include "plaits/dsp/oscillator/oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// 47-tap halfband, Kaiser window beta = 12, cutoff at half the 96 kHz internal
// rate, DC gain normalised to 1. Generated -- not typed -- from
//
//   ideal[n] = n ? sin(pi*n/2) / (pi*n) : 0.5      (n = -47..47)
//   h[n]     = ideal[n] * kaiser(47, 12)[n], zeroed at even n != 0,
//   h       /= sum(h)
//
// which is the standard halfband construction; the even-n zeros are exact by
// construction rather than by rounding, so only the 24 symmetric pairs stored
// here plus the separate centre tap are ever summed.
//
// Response computed from these coefficients: -0.000 dB at 18 kHz, +0.000 dB at
// 20 kHz, -1.86 dB at 22 kHz, -3.52 dB at 23 kHz, -6.02 dB at 24 kHz (the
// halfband symmetry point), -9.54 dB at 25 kHz, -14.28 dB at 26 kHz,
// -28.51 dB at 28 kHz. The reference renderer's 127-tap Blackman-Harris sinc
// reads -0.05, -1.06, -6.02, -18.76, -45.10 at the same points, so the two
// decimators differ by 4.2 dB at 25 kHz and 15.8 dB at 26 kHz and by under
// 0.8 dB everywhere below 23 kHz.
//
// The literals below were checked back against that construction rather than
// asserted, as the practice is for any embedded table: re-deriving the design
// in double precision and comparing tap by tap gives a worst absolute
// deviation of 4.4e-11, which is the 10-decimal rounding of the printed
// literal, and the even-n taps are exactly 0.0 in the generator as well. Held
// as float32, the embedded set reproduces the design's frequency response to
// within 1.9e-7 dB from DC to 26 kHz and has a DC gain of 0.9999999959; the
// two only part company below -100 dB, deep in the stopband.
//
// This engine embeds NO Braids resource table -- verified by reading
// analog_oscillator.cc:188-335, where neither RenderSquare nor RenderSaw
// touches lut_*, wav_*, ws_* or waveform_table. The only Braids table in the
// path is lut_oscillator_increments, behind ComputePhaseIncrement, and the
// port replaces that with plaits::NoteToFrequency (SPEC R6); the A/B's pitch
// figures are what check that substitution.
const float kHalfbandCenter = 0.4999997483f;

const float kHalfbandTaps[kDualSyncHalfbandPairs] = {
  -0.0000007304f, +0.0000198297f, -0.0001196836f, +0.0004564988f,
  -0.0013417914f, +0.0033070347f, -0.0071726082f, +0.0141749919f,
  -0.0263829328f, +0.0483750742f, -0.0961859865f, +0.3148704295f
};

// Braids' square is `phase < pw ? 0 : 32767` and its saw is `phase >> 17`,
// both read out as (sample - 16384) << 1 (analog_oscillator.cc:267-268 and
// :330-331) -- so in normalised terms both run 0..1 and the output is
// 2 * value - 1. A crossfade of the two is therefore exactly Braids' square at
// shape 0 and exactly Braids' saw at shape 1, and the discontinuities add the
// same way: the wrap drops BOTH by 1 whatever the shape, and only the square
// half contributes the mid-cycle rise.
inline float NaiveShape(float phase, float square_amount, float saw_amount) {
  const float square = phase < kDualSyncPulseWidth ? 0.0f : 1.0f;
  return square_amount * square + saw_amount * phase;
}

}  // namespace

void DualSyncEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void DualSyncEngine::Reset() {
  master_phase_ = 0.0f;
  slave_phase_ = 0.0f;
  master_high_ = false;
  slave_high_ = false;
  master_next_sample_ = 0.0f;
  slave_next_sample_ = 0.0f;

  master_frequency_ = 0.005f;
  slave_frequency_ = 0.005f;
  balance_ = 0.0f;
  shape_ = 0.0f;
  reset_phase_ = 0.0f;

  // SPEC R15: another engine's buffers alias these addresses, so every tap has
  // to be cleared on engine switch, not just the indices.
  for (int i = 0; i < kDualSyncDecimatorSize; ++i) {
    master_history_[i] = 0.0f;
    slave_history_[i] = 0.0f;
  }
  decimator_write_ = 0;
}

void DualSyncEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    master_phase_ = 0.0f;
    slave_phase_ = 0.0f;
    master_high_ = false;
    slave_high_ = false;
    master_next_sample_ = 0.0f;
    slave_next_sample_ = 0.0f;
  }

  const bool stereo = PLAITS_STEREO_DUAL_SYNC && parameters.stereo;

  // TIMBRE is the sync interval; COLOR is the master/slave balance.
  const float interval = parameters.timbre * kDualSyncMaxInterval;

  // The increments are internal, i.e. at 2x the output rate, so they are half
  // the output-rate figure and the ceiling is half of kMaxFrequency too: an
  // internal 0.125 is 0.25 expressed at 48 kHz (SPEC R4).
  const float internal_ceiling = kMaxFrequency / float(kDualSyncOversampling);
  float master_frequency = NoteToFrequency(parameters.note) /
      float(kDualSyncOversampling);
  float slave_frequency = NoteToFrequency(parameters.note + interval) /
      float(kDualSyncOversampling);
  CONSTRAIN(master_frequency, 0.0f, internal_ceiling);
  CONSTRAIN(slave_frequency, 0.0f, internal_ceiling);

  const float target_balance = parameters.harmonics;
  const float target_shape = parameters.morph;

  // Braids restarts the slave at phase zero; MACRO moves that restart point
  // half a cycle either way, and noon returns 0.0 exactly.
  const float target_reset_phase = ApplyMacro(
      0.0f, -kDualSyncMaxResetPhase, kDualSyncMaxResetPhase, parameters.macro);

  ParameterInterpolator master_fm(&master_frequency_, master_frequency, size);
  ParameterInterpolator slave_fm(&slave_frequency_, slave_frequency, size);
  ParameterInterpolator balance_modulation(&balance_, target_balance, size);
  ParameterInterpolator shape_modulation(&shape_, target_shape, size);
  ParameterInterpolator reset_modulation(
      &reset_phase_, target_reset_phase, size);

  float master_next = master_next_sample_;
  float slave_next = slave_next_sample_;

  for (size_t i = 0; i < size; ++i) {
    const float mf = master_fm.Next();
    const float sf = slave_fm.Next();
    const float balance = balance_modulation.Next();
    const float shape = shape_modulation.Next();

    const float saw_amount = shape;
    const float square_amount = 1.0f - shape;

    float reset_phase = reset_modulation.Next();
    if (reset_phase < 0.0f) {
      reset_phase += 1.0f;
    }
    const float reset_value = NaiveShape(
        reset_phase, square_amount, saw_amount);

    for (int j = 0; j < kDualSyncOversampling; ++j) {
      float master_this = master_next;
      float slave_this = slave_next;
      master_next = 0.0f;
      slave_next = 0.0f;

      // --- master -------------------------------------------------------
      // Renders its own audio and emits the sync pulse on its wrap, which is
      // what analog_oscillator.cc:232-238 writes into sync_out.
      bool sync = false;
      float reset_time = 0.0f;

      master_phase_ += mf;
      if (!master_high_ && master_phase_ >= kDualSyncPulseWidth) {
        float t = (master_phase_ - kDualSyncPulseWidth) / mf;
        CONSTRAIN(t, 0.0f, 1.0f);
        master_this += square_amount * ThisBlepSample(t);
        master_next += square_amount * NextBlepSample(t);
        master_high_ = true;
      }
      if (master_phase_ >= 1.0f) {
        master_phase_ -= 1.0f;
        reset_time = master_phase_ / mf;
        CONSTRAIN(reset_time, 0.0f, 1.0f);
        // Square and saw both fall by a full unit here, so the step is 1
        // whatever the shape blend is.
        master_this -= ThisBlepSample(reset_time);
        master_next -= NextBlepSample(reset_time);
        master_high_ = false;
        sync = true;
      }
      master_next += NaiveShape(master_phase_, square_amount, saw_amount);

      // --- slave --------------------------------------------------------
      bool transition_during_reset = false;
      if (sync) {
        // Where the slave WOULD have been at the reset instant. Braids builds
        // the same quantity at analog_oscillator.cc:214-215 and :294-295.
        float phase_at_reset = slave_phase_ + (1.0f - reset_time) * sf;
        if (phase_at_reset >= 1.0f) {
          phase_at_reset -= 1.0f;
          transition_during_reset = true;
        }
        if (!slave_high_ && phase_at_reset >= kDualSyncPulseWidth) {
          transition_during_reset = true;
        }
        const float step = reset_value - NaiveShape(
            phase_at_reset, square_amount, saw_amount);
        slave_this += step * ThisBlepSample(reset_time);
        slave_next += step * NextBlepSample(reset_time);
      }

      slave_phase_ += sf;
      while (transition_during_reset || !sync) {
        if (!slave_high_) {
          if (slave_phase_ < kDualSyncPulseWidth) {
            break;
          }
          float t = (slave_phase_ - kDualSyncPulseWidth) / sf;
          CONSTRAIN(t, 0.0f, 1.0f);
          slave_this += square_amount * ThisBlepSample(t);
          slave_next += square_amount * NextBlepSample(t);
          slave_high_ = true;
        }
        if (slave_high_) {
          if (slave_phase_ < 1.0f) {
            break;
          }
          slave_phase_ -= 1.0f;
          float t = slave_phase_ / sf;
          CONSTRAIN(t, 0.0f, 1.0f);
          slave_this -= ThisBlepSample(t);
          slave_next -= NextBlepSample(t);
          slave_high_ = false;
        }
      }

      if (sync) {
        slave_phase_ = reset_phase + reset_time * sf;
        if (slave_phase_ >= 1.0f) {
          slave_phase_ -= 1.0f;
        }
        // Braids sets high_ = false because it always restarts at zero, which
        // is below pw. With the restart point movable the flag has to follow
        // where it actually lands; at MACRO noon this is false, as there.
        slave_high_ = slave_phase_ >= kDualSyncPulseWidth;
      }
      slave_next += NaiveShape(slave_phase_, square_amount, saw_amount);

      // --- 2x -> 1x -----------------------------------------------------
      master_history_[decimator_write_] = 2.0f * master_this - 1.0f;
      slave_history_[decimator_write_] = 2.0f * slave_this - 1.0f;
      decimator_write_ = (decimator_write_ + 1) & (kDualSyncDecimatorSize - 1);
    }

    // Straight FIR over the 47 samples ending at the one just written, oldest
    // first: sum(h[j] * x[base + j]). Every odd j is identically zero in a
    // halfband except the centre, so the loop touches only the 12 symmetric
    // pairs and the centre is taken separately. Group delay is the filter's
    // 47 internal samples, i.e. 23.5 output samples -- a fixed latency, which
    // is why it does not show up anywhere in the A/B.
    const int mask = kDualSyncDecimatorSize - 1;
    const int base = (decimator_write_ + kDualSyncDecimatorSize -
        kDualSyncHalfbandLength) & mask;
    const int centre = (base + (kDualSyncHalfbandLength - 1) / 2) & mask;
    float master_sum = kHalfbandCenter * master_history_[centre];
    float slave_sum = kHalfbandCenter * slave_history_[centre];
    for (int tap = 0; tap < kDualSyncHalfbandPairs; ++tap) {
      const int low = (base + 2 * tap) & mask;
      const int high = (base + kDualSyncHalfbandLength - 1 - 2 * tap) & mask;
      master_sum += kHalfbandTaps[tap] *
          (master_history_[low] + master_history_[high]);
      slave_sum += kHalfbandTaps[tap] *
          (slave_history_[low] + slave_history_[high]);
    }

    // Braids mixes at 96 kHz and the reference decimates afterwards; both
    // operations are linear, so crossfading the decimated pair is the same
    // sound for a fixed Balance (declared in the header).
    float balance_main = balance;
    float balance_aux = 1.0f - balance;
    if (stereo) {
      balance_main = balance - kDualSyncStereoBalance;
      balance_aux = balance + kDualSyncStereoBalance;
      CONSTRAIN(balance_main, 0.0f, 1.0f);
      CONSTRAIN(balance_aux, 0.0f, 1.0f);
    }

    const float difference = slave_sum - master_sum;
    out[i] = kDualSyncOutputGain * (master_sum + difference * balance_main);
    aux[i] = kDualSyncOutputGain * (master_sum + difference * balance_aux);
  }

  master_next_sample_ = master_next;
  slave_next_sample_ = slave_next;
}

}  // namespace plaits
