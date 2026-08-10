// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SUB models: a shaped main oscillator against a square sub.

#include "plaits/dsp/engine2/sub_oscillator_engine.h"

#include "plaits/build_config.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/oscillator/oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void SubOscillatorEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void SubOscillatorEngine::Reset() {
  phase_ = 0.0f;
  phase_sub_ = 0.0f;
  next_sample_square_ = 0.0f;
  next_sample_saw_ = 0.0f;
  next_sample_sub_ = 0.0f;
  high_square_ = false;
  high_saw_ = false;
  high_sub_ = false;
  previous_pw_square_ = 0.5f;
  previous_pw_saw_ = 0.25f;
  previous_pw_sub_ = 0.5f;
  frequency_ = 0.01f;
  pw_square_ = 0.5f;
  pw_saw_ = 0.25f;
  pw_sub_ = kSubOscillatorSubPwStock;
  colour_ = kSubOscillatorColourCentre;
  shape_ = 0.0f;
}

void SubOscillatorEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  // A trigger deliberately does NOTHING here. Braids'
  // MacroOscillator::Strike() touches only digital_oscillator_
  // (macro_oscillator.h:80-82), so its SUB models free-run through a note-on,
  // and the oscillators are continuous across one. Resetting them would put a
  // step into a waveform that carries a large duty-dependent offset, which is
  // exactly the LPG thump this engine used to have.

  float frequency = NoteToFrequency(parameters.note);
  if (frequency >= kMaxFrequency) {
    frequency = kMaxFrequency;
  }

  // TIMBRE reaches Braids as a 15-bit integer, and each render function then
  // clamps THAT integer -- the square CEILINGS it at 32000, the saw FLOORS it
  // at 1024 (analog_oscillator.cc:194-196, :344-346). Those clamps, not the
  // linear law, set each end of the audible travel, so they are reproduced on
  // the integer rather than on the fraction it turns into.
  float timbre = parameters.timbre;
  CONSTRAIN(timbre, 0.0f, 1.0f);
  const int32_t parameter = static_cast<int32_t>(
      timbre * kSubOscillatorParameterFullScale);

  const int32_t square_parameter = min(
      parameter, kSubOscillatorSquareParameterMax);
  // `pw = (32768 - parameter_) << 16`: the LOW fraction, 0.5 down to 0.011719.
  const float target_pw_square = static_cast<float>(32768 - square_parameter) *
      kSubOscillatorParameterScale;

  const int32_t saw_parameter = max(
      parameter, kSubOscillatorSawParameterMin);
  // `pw = parameter_ << 16`: the spacing between the two ramps, 1/64 to ~1/2.
  const float target_pw_saw = static_cast<float>(saw_parameter) *
      kSubOscillatorParameterScale;

  // COLOR is quantised the same way, and BOTH of its jobs read the integer:
  // the octave from `parameter_[1] < 16384` (macro_oscillator.cc:236) and the
  // blend from the V around 16384 (:247-248).
  float morph = parameters.morph;
  CONSTRAIN(morph, 0.0f, 1.0f);
  const int32_t colour = static_cast<int32_t>(
      morph * kSubOscillatorParameterFullScale);

  const float sub_octave =
      static_cast<float>(colour) < kSubOscillatorColourCentre
          ? kSubOscillatorLowOctave
          : kSubOscillatorHighOctave;
  // The sub tracks the main oscillator's own phase increment, exactly as
  // Braids' second AnalogOscillator tracks the first's pitch.
  const float sub_ratio = SemitonesToRatio(sub_octave);

  // MACRO narrows the sub's pulse below noon and leaves Braids' square above.
  const float target_pw_sub = ApplyMacro(
      kSubOscillatorSubPwStock,
      kSubOscillatorSubPwMin,
      kSubOscillatorSubPwStock,
      parameters.macro);

  // HARMONICS is the port's own axis: 0 is RenderSquare, 1 is
  // RenderVariableSaw, and in between the two crossfade off the shared phase.
  float shape = parameters.harmonics;
  CONSTRAIN(shape, 0.0f, 1.0f);

  ParameterInterpolator fm(&frequency_, frequency, size);
  ParameterInterpolator pw_square_modulation(
      &pw_square_, target_pw_square, size);
  ParameterInterpolator pw_saw_modulation(&pw_saw_, target_pw_saw, size);
  ParameterInterpolator pw_sub_modulation(&pw_sub_, target_pw_sub, size);
  // Braids interpolates the RAW COLOR integer across the block
  // (BEGIN/INTERPOLATE_PARAMETER_1, macro_oscillator.cc:242-249) and re-folds
  // the V per sample. Interpolating the folded gain instead would cut the
  // corner off the V when COLOR sweeps through noon.
  ParameterInterpolator colour_modulation(
      &colour_, static_cast<float>(colour), size);
  ParameterInterpolator shape_modulation(&shape_, shape, size);

  float next_square = next_sample_square_;
  float next_saw = next_sample_saw_;
  float next_sub = next_sample_sub_;

  for (size_t i = 0; i < size; ++i) {
    float this_square = next_square;
    float this_saw = next_saw;
    float this_sub = next_sub;
    next_square = 0.0f;
    next_saw = 0.0f;
    next_sub = 0.0f;

    float f = fm.Next();
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      f += parameters.frequency_offset[i];
      CONSTRAIN(f, 0.0f, kMaxFrequency);
    }
#endif
    const float pw_square = pw_square_modulation.Next();
    const float pw_saw = pw_saw_modulation.Next();
    const float pw_sub = pw_sub_modulation.Next();
    const float shape = shape_modulation.Next();
    const float f_sub = f * sub_ratio;

    // The main oscillator. One phase, two naive waveforms, three
    // discontinuities: the square's rising edge at pw_square, the saw's
    // downward jump at pw_saw, and the wrap, which cuts both.
    phase_ += f;
    while (true) {
      if (!high_square_ && phase_ >= pw_square) {
        float t = (phase_ - pw_square) / (previous_pw_square_ - pw_square + f);
        CONSTRAIN(t, 0.0f, 1.0f);
        this_square += ThisBlepSample(t);
        next_square += NextBlepSample(t);
        high_square_ = true;
        continue;
      }
      if (!high_saw_ && phase_ >= pw_saw) {
        float t = (phase_ - pw_saw) / (previous_pw_saw_ - pw_saw + f);
        CONSTRAIN(t, 0.0f, 1.0f);
        this_saw -= ThisBlepSample(t);
        next_saw -= NextBlepSample(t);
        high_saw_ = true;
        continue;
      }
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
        const float t = phase_ / f;
        if (high_square_) {
          this_square -= ThisBlepSample(t);
          next_square -= NextBlepSample(t);
          high_square_ = false;
        }
        if (high_saw_) {
          this_saw -= ThisBlepSample(t);
          next_saw -= NextBlepSample(t);
          high_saw_ = false;
        }
        continue;
      }
      break;
    }
    // RenderSquare's naive sample, in a 0..1 domain (scaled to +-1 below).
    next_square += phase_ < pw_square ? 0.0f : 1.0f;
    // RenderVariableSaw's naive sample: `(phase >> 18) + ((phase - pw) >> 18)`
    // closed over the wrap (analog_oscillator.cc:414-415), already +-1.
    next_saw += phase_ < pw_saw
        ? (2.0f * phase_ - pw_saw)
        : (2.0f * phase_ - pw_saw - 1.0f);
    previous_pw_square_ = pw_square;
    previous_pw_saw_ = pw_saw;

    // The sub: a plain square at its own frequency, Braids' `set_parameter(0)`
    // widened into MACRO's range.
    phase_sub_ += f_sub;
    while (true) {
      if (!high_sub_ && phase_sub_ >= pw_sub) {
        float t = (phase_sub_ - pw_sub) / (previous_pw_sub_ - pw_sub + f_sub);
        CONSTRAIN(t, 0.0f, 1.0f);
        this_sub += ThisBlepSample(t);
        next_sub += NextBlepSample(t);
        high_sub_ = true;
        continue;
      }
      if (phase_sub_ >= 1.0f) {
        phase_sub_ -= 1.0f;
        const float t = phase_sub_ / f_sub;
        if (high_sub_) {
          this_sub -= ThisBlepSample(t);
          next_sub -= NextBlepSample(t);
          high_sub_ = false;
        }
        continue;
      }
      break;
    }
    next_sub += phase_sub_ < pw_sub ? 0.0f : 1.0f;
    previous_pw_sub_ = pw_sub;

    // A narrow pulse carries a large DC term by construction, and Braids
    // leaves it in; the LPG here does not tolerate that. The mean is known in
    // closed form -- `1 - 2*pw` for a square, exactly zero for the variable
    // saw -- so it is subtracted rather than filtered out. That costs no phase
    // and no low-frequency level, which a pole low enough to clear a two-
    // octave sub at note 24 could not have managed.
    const float square = 2.0f * this_square - 1.0f - (1.0f - 2.0f * pw_square);
    const float saw = this_saw;
    const float main = square + (saw - square) * shape;
    const float sub = 2.0f * this_sub - 1.0f - (1.0f - 2.0f * pw_sub);

    // Braids' V: `(p2 < 16384 ? 16383 - p2 : p2 - 16384) << 1`, applied
    // through stmlib's Mix(a, b, balance) = (a*(65535-balance) + b*balance)
    // >> 16 (stmlib/utils/dsp.h:86-88, macro_oscillator.cc:247-249).
    const float c = colour_modulation.Next();
    float gain = c < kSubOscillatorColourCentre
        ? (kSubOscillatorColourCentre - 1.0f - c) * 2.0f
        : (c - kSubOscillatorColourCentre) * 2.0f;
    if (gain < 0.0f) {
      gain = 0.0f;
    }

    out[i] = (main * (65535.0f - gain) + sub * gain) *
        kSubOscillatorParameterScale;
    // AUX carries the sub on its own, at full level rather than scaled by the
    // blend -- otherwise it would go silent at the centre of MORPH, which is
    // the first place anyone would look for it.
    aux[i] = sub;
  }

  next_sample_square_ = next_square;
  next_sample_saw_ = next_saw;
  next_sample_sub_ = next_sub;
}

}  // namespace plaits
