// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' four "digital filter" models (ZLPF, ZPKF, ZBPF, ZHPF) in one slot.

#include "plaits/dsp/engine2/z_filter_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' wav_sine and Plaits' lut_sine DO NOT SHARE A PHASE ORIGIN.
// wav_sine[0] = -32512, [64] = 126, [128] = 32766 -- it is a quarter period
// behind lut_sine, which starts at 0.0 and rises. Reading a Braids phase
// straight through Sine() therefore renders every resonator 90 degrees out,
// which sounds plausible in isolation and is completely wrong against
// hardware. sin(2*pi*(x - 0.25)) == Sine(x + 0.75), and InterpolateWrap folds
// the argument back, so the offset is free.
//
// It is the PHASE only. The table's AMPLITUDE and OFFSET are BraidsSineFixed's
// job -- see below.
inline float BraidsSine(float phase) {
  return Sine(phase + 0.75f);
}

// wav_sine as Braids actually stores it. Fitted programmatically against all
// 257 entries of braids/resources.cc: wav_sine[i] == 32639 * -cos(2*pi*i/256)
// + 127 to within 1.7 LSB. It is NOT a unit sine -- it carries a -0.034 dB
// gain and, decisively, a +127 LSB (+0.00388 full-scale) DC OFFSET on every
// sample. That offset is what drives the pulse integrator below into its
// rails; reading a zero-mean sine there is what cost this engine 5.70 dB.
//
// Interpolate824 (stmlib/utils/dsp.h:94) returns `a + ((b - a) * frac >> 16)`,
// an arithmetic shift, so the table read FLOORS. The bias of 32768 makes the
// argument non-negative, which turns the truncating cast into a floor without
// a call into libm. It is the same ~-0.5 LSB bias, not the same operation:
// Interpolate824 floors only the interpolation delta it adds to an exact int16
// table entry, while this floors the whole ideal value read from a 512-point
// LUT. The two differ by under 2 LSB in 32768, which is why the loop still
// measures identical to the reference at 96 kHz.
inline int32_t BraidsSineFixed(float phase) {
  return static_cast<int32_t>(
      BraidsSine(phase) * 32639.0f + (32768.0f + 127.0f)) - 32768;
}

// The burst envelope raised to a power between 1 and 4, without libm. The
// weights select (and interpolate between) x, x^2, x^3 and x^4, so an integer
// exponent is exact and MACRO's detent value of 1 returns x untouched.
inline float BendWindow(float x, const float* weights) {
  const float x2 = x * x;
  const float x3 = x2 * x;
  const float x4 = x2 * x2;
  return x * weights[0] + x2 * weights[1] + \
      x3 * weights[2] + x4 * weights[3];
}

// One model's output combination. Braids' four models differ here and in the
// phase they re-seed their resonators to; this is the half that is free once
// the resonators have been advanced.
inline float CombineModel(
    int filter_type,
    float carrier,
    float window,
    float pulse,
    float integrator,
    float balance) {
  float saw_tri_signal;
  float square_signal;
  if (filter_type & 2) {
    // Bandpass and highpass: the resonator is rung symmetrically and the
    // pulse train is used raw.
    saw_tri_signal = carrier * window;
    square_signal = pulse;
  } else {
    // Lowpass and peaking: the resonator is offset positive before windowing
    // and the integrated pulse supplies the body.
    saw_tri_signal = window * (carrier + 1.0f) - 1.0f;
    square_signal = filter_type == 1
        ? 0.5f * (pulse + integrator)
        : integrator;
  }
  return saw_tri_signal + (square_signal - saw_tri_signal) * balance;
}

}  // namespace

void ZFilterEngine::Init(BufferAllocator* allocator) {
  // No delay lines or tables: the engine is pure state, so it takes nothing
  // from the shared 16 KiB arena.
  (void) allocator;
  model_quantizer_.Init(4, 0.05f, false);
  Reset();
}

void ZFilterEngine::Reset() {
  phase_ = 0.0f;
  mod_increment_ = 0.0f;
  previous_half_ = false;
  modulator_phase_ = 0.0f;
  square_phase_ = 0.0f;
  integrator_ = 0;
  // Braids memsets its resonator state, so the polarity latch starts LOW. Only
  // the sync/trigger handler raises it.
  polarity_ = false;
  out_decimator_ = 0.0f;
  aux_decimator_ = 0.0f;
}

void ZFilterEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
    previous_half_ = false;
    modulator_phase_ = 0.0f;
    square_phase_ = 0.0f;
    integrator_ = 0;
    polarity_ = true;
  }

  // The internal rate is 2x the output rate, matching RenderDigitalFilter's
  // native 96 kHz. Omitting the halving renders an octave high.
  const float f0 = NoteToFrequency(parameters.note) * 0.5f;

  // TIMBRE is Braids' (parameter_[0] - 2048) >> 1 in 1/128-semitone units:
  // eight semitones below the note to ten octaves above it. Braids clamps the
  // shifted PITCH to 16383 (MIDI 127.99) and only then takes the increment.
  float shifted_pitch = parameters.note + kZFilterCutoffOffset + \
      kZFilterCutoffRange * parameters.timbre;
  if (shifted_pitch > kZFilterMaxPitch) {
    shifted_pitch = kZFilterMaxPitch;
  }
  const float target_mod_increment = NoteToFrequency(shifted_pitch) * 0.5f;

  const int filter_type = model_quantizer_.Process(parameters.harmonics);
  // AUX runs the complementary model: LP<->HP, PK<->BP.
  const int aux_filter_type = 3 - filter_type;

  // Braids' `(parameter_[1] < 16384 ? parameter_[1] : ~parameter_[1]) << 2`
  // over a 15-bit parameter is a triangle peaking at the knob's midpoint --
  // verified 0 -> 65532 -> 0 with the peak at parameter_[1] = 16384.
  const float balance = 1.0f - fabsf(2.0f * parameters.morph - 1.0f);
  const bool use_triangle = parameters.morph >= 0.5f;

  // MACRO bends both burst envelopes from Braids' flat ramp toward w^4. Below
  // noon it is inert by construction (minimum == stock), so the whole bend is
  // skipped there.
  const float bend = ApplyMacro(1.0f, 1.0f, 4.0f, parameters.macro);
  const bool bend_active = bend > 1.0001f;
  float bend_weights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
  if (bend_active) {
    const float k = bend - 1.0f;
    int index = static_cast<int>(k);
    CONSTRAIN(index, 0, 2);
    const float fraction = k - static_cast<float>(index);
    bend_weights[0] = 0.0f;
    bend_weights[index] = 1.0f - fraction;
    bend_weights[index + 1] = fraction;
  }

  const float modulator_reset = kZFilterPhaseReset[filter_type];
  const float square_reset = kZFilterPhaseReset[(filter_type & 1) + 2];

  ParameterInterpolator mod_increment_modulation(
      &mod_increment_, target_mod_increment, size * 2);

  for (size_t i = 0; i < size; ++i) {
    float out_sub[2];
    float aux_sub[2];
    for (int s = 0; s < 2; ++s) {
      const float mod_increment = mod_increment_modulation.Next();

      phase_ += f0;
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      }
      const bool half = phase_ >= 0.5f;
      // Braids' `phase_ < phase_increment_` (the carrier wrapped) and
      // `(phase_ << 1) < (phase_increment_ << 1)` (the doubled phase wrapped,
      // so both half-cycles). f0 can never exceed 0.125 at this rate, so no
      // crossing is ever skipped.
      const bool half_cycle = half != previous_half_;
      const bool cycle_start = half_cycle && !half;
      previous_half_ = half;

      // Braids leans on uint32 phase wrapping. In float the phase has to be
      // wrapped explicitly: at the top of TIMBRE the modulator advances ~1024
      // cycles per carrier period, and an unwrapped phase walks straight off
      // the end of lut_sine. One subtract suffices -- the increment is clamped
      // well below 1.0 -- and Sine() wraps again internally as a backstop.
      modulator_phase_ += mod_increment;
      if (modulator_phase_ >= 1.0f) {
        modulator_phase_ -= 1.0f;
      }
      square_phase_ += mod_increment;
      if (square_phase_ >= 1.0f) {
        square_phase_ -= 1.0f;
      }
      if (cycle_start) {
        modulator_phase_ = modulator_reset;
      }
      if (half_cycle) {
        polarity_ = !polarity_;
        square_phase_ = square_reset;
      }

      const float saw = 1.0f - phase_;
      const float double_ramp = half
          ? 2.0f - 2.0f * phase_
          : 1.0f - 2.0f * phase_;
      const float triangle = half
          ? 2.0f - 2.0f * phase_
          : 2.0f * phase_;

      float window = use_triangle ? triangle : saw;
      float double_saw = double_ramp;
      if (bend_active) {
        window = BendWindow(window, bend_weights);
        double_saw = BendWindow(double_saw, bend_weights);
      }

      // The windowed resonator carries wav_sine's gain and offset too. It is
      // -48 dB there and measures nothing on its own, but it is Braids'
      // arithmetic and it is one multiply-add.
      const float carrier =
          static_cast<float>(BraidsSineFixed(modulator_phase_)) * \
          (1.0f / 32768.0f);

      // THE PULSE PATH RUNS IN FIXED POINT, because its integrator's
      // equilibrium is set by quantisation and not by the ideal signal.
      // Braids (digital_oscillator.cc:379-385):
      //
      //   uint16_t double_saw = ~(phase_ >> 15);
      //   int32_t pulse = (square_carrier * double_saw) >> 16;
      //   if (polarity) pulse = -pulse;
      //   square_integrator += (pulse * integrator_gain) >> 16;
      //   CLIP(square_integrator)
      //
      // with integrator_gain = modulator_phase_increment >> 14, which over a
      // 32-bit phase is floor(mod_increment * 2^18). Every one of those steps
      // matters: square_carrier's +127 offset survives the multiply as a DC
      // term that the polarity latch flips every half carrier cycle, the two
      // arithmetic shifts floor, and CLIP rectifies the result against both
      // rails. Run it in floats and the accumulator simply never gets there.
      const uint16_t double_saw_fixed =
          static_cast<uint16_t>(double_saw * 65535.0f);
      const uint16_t integrator_gain =
          static_cast<uint16_t>(mod_increment * 262144.0f);
      const int32_t square_carrier = BraidsSineFixed(square_phase_);

      int32_t pulse_fixed =
          (square_carrier * static_cast<int32_t>(double_saw_fixed)) >> 16;
      if (polarity_) {
        pulse_fixed = -pulse_fixed;
      }
      integrator_ += (pulse_fixed * static_cast<int32_t>(integrator_gain)) >> 16;
      CLIP(integrator_)

      const float pulse = static_cast<float>(pulse_fixed) * (1.0f / 32768.0f);
      const float integrator =
          static_cast<float>(integrator_) * (1.0f / 32768.0f);

      out_sub[s] = CombineModel(
          filter_type, carrier, window, pulse, integrator, balance);
      aux_sub[s] = CombineModel(
          aux_filter_type, carrier, window, pulse, integrator, balance);
    }

    // [0.25, 0.5, 0.25] decimation to the output rate: a null at 48 kHz for
    // the cost of -1.4 dB at 12 kHz and -6.0 dB at 24 kHz. The residual grit
    // near Nyquist is part of the model's character, and a sharper filter
    // costs flash this engine does not need to spend.
    out[i] = 0.25f * out_decimator_ + 0.5f * out_sub[0] + 0.25f * out_sub[1];
    aux[i] = 0.25f * aux_decimator_ + 0.5f * aux_sub[0] + 0.25f * aux_sub[1];
    out_decimator_ = out_sub[1];
    aux_decimator_ = aux_sub[1];
  }
}

}  // namespace plaits
