// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' MORPH: a triangle-saw-square-pulse morph into a pitch-tracking
// low-pass and a tanh(8x) fuzz.

#include "plaits/dsp/engine2/morph_engine.h"

#include "plaits/build_config.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' ws_violent_overdrive, verbatim from braids/resources.cc.
//
// It reproduces EXACTLY -- 0 LSB of deviation across all 257 entries --
// from the closed form in braids/resources/waveshapers.py:
//
//   violent_overdrive = scale(tanh(8x))        (:38, appended at :63)
//
// with `scale` removing the mean, normalising by the peak and mapping onto
// -32766..32766. Checked rather than asserted. The data is embedded because
// the shaper is read once per 96 kHz sub-sample and a tanh there is not
// affordable; the table also carries the curve's saturation faithfully --
// 86 of the 257 entries sit within 1 LSB of full scale, every one of them at
// |x| >= 0.671875, so past that point this is a hard clip, not a soft one.

const int16_t kViolentOverdrive[257] = {
  -32766, -32766, -32766, -32766, -32766, -32766, -32766, -32766,
  -32766, -32766, -32766, -32766, -32766, -32766, -32766, -32766,
  -32766, -32766, -32766, -32766, -32766, -32766, -32766, -32766,
  -32766, -32766, -32766, -32766, -32766, -32766, -32766, -32766,
  -32766, -32766, -32765, -32765, -32765, -32765, -32765, -32765,
  -32765, -32765, -32765, -32764, -32764, -32764, -32764, -32763,
  -32763, -32763, -32762, -32762, -32761, -32760, -32760, -32759,
  -32758, -32757, -32756, -32754, -32753, -32751, -32749, -32747,
  -32744, -32741, -32738, -32734, -32730, -32725, -32720, -32713,
  -32706, -32698, -32689, -32679, -32668, -32655, -32640, -32623,
  -32604, -32582, -32558, -32531, -32499, -32464, -32424, -32379,
  -32327, -32269, -32204, -32130, -32046, -31951, -31844, -31724,
  -31587, -31434, -31260, -31065, -30845, -30598, -30320, -30008,
  -29658, -29266, -28828, -28340, -27795, -27189, -26518, -25774,
  -24954, -24053, -23064, -21985, -20811, -19541, -18172, -16705,
  -15142, -13486, -11742,  -9919,  -8025,  -6073,  -4075,  -2045,
       0,   2045,   4075,   6073,   8025,   9919,  11742,  13486,
   15142,  16705,  18172,  19541,  20811,  21985,  23064,  24053,
   24954,  25774,  26518,  27189,  27795,  28340,  28828,  29266,
   29658,  30008,  30320,  30598,  30845,  31065,  31260,  31434,
   31587,  31724,  31844,  31951,  32046,  32130,  32204,  32269,
   32327,  32379,  32424,  32464,  32499,  32531,  32558,  32582,
   32604,  32623,  32640,  32655,  32668,  32679,  32689,  32698,
   32706,  32713,  32720,  32725,  32730,  32734,  32738,  32741,
   32744,  32747,  32749,  32751,  32753,  32754,  32756,  32757,
   32758,  32759,  32760,  32760,  32761,  32762,  32762,  32763,
   32763,  32763,  32764,  32764,  32764,  32764,  32765,  32765,
   32765,  32765,  32765,  32765,  32765,  32765,  32765,  32766,
   32766,  32766,  32766,  32766,  32766,  32766,  32766,  32766,
   32766,  32766,  32766,  32766,  32766,  32766,  32766,  32766,
   32766,  32766,  32766,  32766,  32766,  32766,  32766,  32766,
   32766,  32766,  32766,  32766,  32766,  32766,  32766,  32766,
   32766,
};

// Braids reads the shaper with Interpolate88(table, lp_state + 32768): the
// int16 sample becomes a uint16 index, its top 8 bits select the entry and its
// low 8 interpolate toward the next. `x` here is that same input, normalised.
inline float ReadShaper(float x) {
  float index = (x + 1.0f) * 128.0f;
  CONSTRAIN(index, 0.0f, 255.999f);
  const int integral = static_cast<int>(index);
  const float fractional = index - static_cast<float>(integral);
  const float a = static_cast<float>(kViolentOverdrive[integral]);
  const float b = static_cast<float>(kViolentOverdrive[integral + 1]);
  return (a + (b - a) * fractional) * (1.0f / 32768.0f);
}

// Braids' triangle: a phase ramp folded about its midpoint, full scale, -1 at
// phase 0 and +1 at phase 0.5. Verified against the integer original --
// (phase_16 << 1) ^ (sign ? 0xffff : 0) then += 32768, read as int16
// (analog_oscillator.cc:442-444) -- at every sixteenth of a cycle.
inline float Triangle(float phase) {
  const float t = phase < 0.5f ? phase : 1.0f - phase;
  return 4.0f * t - 1.0f;
}

// lut_svf_cutoff's generator: f = cutoff / sample_rate, clamped at 1/8, then
// 2 * sin(pi * f). `cutoff_note` is the MIDI note the table indexes by, and
// NoteToFrequency returns cycles per OUTPUT sample, so halving it gives
// cycles per 96 kHz sub-sample -- which is exactly Braids' cutoff / 96000.
inline float CutoffCoefficient(float cutoff_note) {
  float f = NoteToFrequency(cutoff_note) * 0.5f;
  if (f > kMorphCutoffLimit) {
    f = kMorphCutoffLimit;
  }
  // Sine(p) is sin(2*pi*p), so sin(pi*f) is Sine(f * 0.5).
  return 2.0f * Sine(f * 0.5f);
}

}  // namespace

// The 31-tap halfband, read out of a 32-entry ring of sub-samples. The caller
// has written both sub-samples of the frame and advanced the position, so the
// newest sub-sample sits at (p - 1) and the centre tap fifteen further back at
// (p - 16) -- an EVEN sub-sample, which is what keeps the decimation phase
// right. Every even tap of a halfband is zero, so only the odd offsets and the
// centre are summed.
inline float MorphEngine::Decimate(const float* history) const {
  // Each logical entry is mirrored 32 floats later. Reading from the second
  // copy makes the whole 31-tap window contiguous and removes the wrap mask
  // from all seventeen history positions.
  const int p = history_position_ + 32;
  return kMorphHalfbandCentre * history[p - 16]
      + kMorphHalfband1 * (history[p - 15] + history[p - 17])
      + kMorphHalfband3 * (history[p - 13] + history[p - 19])
      + kMorphHalfband5 * (history[p - 11] + history[p - 21])
      + kMorphHalfband7 * (history[p - 9] + history[p - 23])
      + kMorphHalfband9 * (history[p - 7] + history[p - 25])
      + kMorphHalfband11 * (history[p - 5] + history[p - 27])
      + kMorphHalfband13 * (history[p - 3] + history[p - 29])
      + kMorphHalfband15 * (history[p - 1] + history[p - 31]);
}

void MorphEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void MorphEngine::Reset() {
  phase_ = 0.0f;
  frequency_ = 0.01f;
  saw_ = 0.0f;
  square_ = 0.0f;
  pw_ = 0.5f;
  fuzz_ = 0.0f;
  fuzz_aux_ = 0.0f;
  high_ = false;
  previous_pw_ = 0.5f;
  next_sample_ = 0.0f;
  lp_state_ = 0.0f;
  lp_state_aux_ = 0.0f;
  dc_input_ = 0.0f;
  dc_input_aux_ = 0.0f;
  for (int i = 0; i < 64; ++i) {
    history_[i] = 0.0f;
    history_aux_[i] = 0.0f;
  }
  history_position_ = 0;
}

void MorphEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // Braids does NOT reset phase here: MacroOscillator::Strike
    // (macro_oscillator.h:80-82) only raises the DIGITAL oscillator's strike_
    // flag, which no part of the MORPH path reads. This reset is the house
    // convention csaw and fold already follow, and it is listed as a deviation
    // in the header. All three shapes share one accumulator (see the header).
    phase_ = 0.0f;
    high_ = false;
    next_sample_ = 0.0f;
  }

  const bool stereo = PLAITS_STEREO_MORPH && parameters.stereo;

  float note = parameters.note;
  if (note > kMorphMaxNote) {
    note = kMorphMaxNote;
  }
  const float frequency = NoteToFrequency(note) * (1.0f / kMorphOversampling);

  // TIMBRE, macro_oscillator.cc:74-92. The branch is on the raw 15-bit
  // parameter because the boundaries are integers there.
  const float p0 = parameters.timbre * 32767.0f;
  float target_saw;
  float target_square;
  float target_pw;
  if (p0 <= kMorphRegion1End) {
    // :77-79 -- triangle against saw.
    target_saw = p0 * kMorphBalanceSlope;
    target_square = 0.0f;
    target_pw = 0.5f;
  } else if (p0 <= kMorphRegion2End) {
    // :83-85 -- saw against a 50% square, running backwards.
    target_saw = 1.0f - (p0 - kMorphRegion2Start) * kMorphBalanceSlope;
    target_square = 1.0f - target_saw;
    target_pw = 0.5f;
  } else {
    // :87-91 -- the square alone, narrowing. The sine the model asks for on
    // this branch is multiplied by a zero balance and never heard.
    float square_parameter = (p0 - kMorphRegion3Start) * kMorphSquareSlope;
    if (square_parameter > kMorphSquareMax) {
      square_parameter = kMorphSquareMax;
    }
    target_saw = 0.0f;
    target_square = 1.0f;
    target_pw = (32768.0f - square_parameter) * (1.0f / 65536.0f);
  }
  // A floor only so the pulse can never reach zero width. Chaining the two
  // edge branches above already handles a sub-sample pulse, so half a
  // sub-sample is enough and it does not bite until about MIDI 97 -- where
  // Braids' own 1.2% pulse is under a sample wide and aliasing on both sides.
  float pw_floor = 0.5f * frequency;
  if (pw_floor > 0.25f) {
    pw_floor = 0.25f;
  }
  if (target_pw < pw_floor) {
    target_pw = pw_floor;
  }

  // COLOR, :99 and :107. Both jobs come off the same knob; MORPH then offsets
  // the cutoff away from where that coupling put it, which is the axis the
  // module has no control for.
  const float tone_offset = (parameters.morph - 0.5f) * 2.0f *
      kMorphMaxToneOffset;

  // :107-113 -- the fuzz mix, rolled off above MIDI 80.
  float fuzz_position = parameters.harmonics;
  float fuzz_position_aux = fuzz_position;
  // In stereo the two channels sit either side of the Fuzz knob. The knob is
  // squeezed into [d, 1-d] first so neither side can clamp -- a clamp would
  // make them identical at the ends of the knob and collapse the image there.
  if (stereo) {
    const float centre = kMorphStereoFuzz +
        fuzz_position * (1.0f - 2.0f * kMorphStereoFuzz);
    fuzz_position = centre - kMorphStereoFuzz;
    fuzz_position_aux = centre + kMorphStereoFuzz;
  }

  float target_fuzz = fuzz_position;
  float target_fuzz_aux = fuzz_position_aux;
  if (note > kMorphFuzzGuardNote) {
    const float roll_off = (note - kMorphFuzzGuardNote) * kMorphFuzzRollOff;
    target_fuzz -= roll_off;
    target_fuzz_aux -= roll_off;
  }
  CONSTRAIN(target_fuzz, 0.0f, 1.0f);
  CONSTRAIN(target_fuzz_aux, 0.0f, 1.0f);

  // The cutoff travels with the fuzz exactly as Braids welds them, so in
  // stereo the cleaner side is also the brighter one.
  float cutoff_note = note + kMorphCutoffCeiling -
      fuzz_position * kMorphCutoffSpan + tone_offset;
  float cutoff_note_aux = note + kMorphCutoffCeiling -
      fuzz_position_aux * kMorphCutoffSpan + tone_offset;
  CONSTRAIN(cutoff_note, 0.0f, kMorphCutoffMax);
  CONSTRAIN(cutoff_note_aux, 0.0f, kMorphCutoffMax);
  const float lp_a = CutoffCoefficient(cutoff_note);
  const float lp_a_aux = stereo ? CutoffCoefficient(cutoff_note_aux) : 0.0f;

  const float drive = ApplyMacro(
      1.0f, kMorphMinDrive, kMorphMaxDrive, parameters.macro);

  ParameterInterpolator fm(&frequency_, frequency, size);
  ParameterInterpolator saw_modulation(&saw_, target_saw, size);
  ParameterInterpolator square_modulation(&square_, target_square, size);
  ParameterInterpolator pwm(&pw_, target_pw, size);
  ParameterInterpolator fuzz_modulation(&fuzz_, target_fuzz, size);
  ParameterInterpolator fuzz_modulation_aux(&fuzz_aux_, target_fuzz_aux, size);

  float next_sample = next_sample_;

  for (size_t i = 0; i < size; ++i) {
    float f = fm.Next();
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      f += parameters.frequency_offset[i] /
          static_cast<float>(kMorphOversampling);
      CONSTRAIN(
          f, 0.0f,
          0.25f / static_cast<float>(kMorphOversampling));
    }
#endif
    const float w_saw = saw_modulation.Next();
    const float w_square = square_modulation.Next();
    const float w_triangle = 1.0f - w_saw - w_square;
    const float pw = pwm.Next();
    const float fuzz = fuzz_modulation.Next();
    const float fuzz_aux = fuzz_modulation_aux.Next();

    for (int j = 0; j < kMorphOversampling; ++j) {
      float this_sample = next_sample;
      next_sample = 0.0f;

      phase_ += f;

      if (!high_ && phase_ >= pw) {
        // The square's rising edge. Only the square has one here.
        float t = (phase_ - pw) / (previous_pw_ - pw + f);
        CONSTRAIN(t, 0.0f, 1.0f);
        const float step = 2.0f * w_square;
        this_sample += step * ThisBlepSample(t);
        next_sample += step * NextBlepSample(t);
        high_ = true;
      }
      // NOT an `else`: at the top of TIMBRE the pulse is 1.2% of a cycle, so
      // above roughly MIDI 85 it is shorter than one sub-sample and both its
      // edges land in the same one. Chaining them lets the two BLEPs overlap
      // and partly cancel, which is what a sub-sample pulse should do; an
      // `else if` would drop one edge per cycle and the pulse would have to be
      // floored four times as wide to stay well-posed, costing 10.9 dB of
      // level against the module at MIDI 96.
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
        float t = phase_ / f;
        CONSTRAIN(t, 0.0f, 1.0f);
        // The saw drops from +1 to -1 and, in the same instant, so does the
        // square: just before the wrap the phase is past pw and just after it
        // is not. One BLEP carries both. `high_` keeps the step from claiming
        // a fall for a square that never rose, which the pulse-width floor
        // makes unreachable but the chained branches above no longer prove.
        const float step = -2.0f * (w_saw + (high_ ? w_square : 0.0f));
        this_sample += step * ThisBlepSample(t);
        next_sample += step * NextBlepSample(t);
        high_ = false;
      }

      // Braids' triangle is itself 2x oversampled and box-averaged
      // (analog_oscillator.cc:441-451), so it is evaluated at the half-step
      // and the step and the pair averaged, exactly as written there. The
      // triangle carries no value discontinuity, so it needs no BLEP -- and
      // Braids does not band-limit its slope corners either.
      float half_phase = phase_ - 0.5f * f;
      if (half_phase < 0.0f) {
        half_phase += 1.0f;
      }
      const float triangle =
          0.5f * (Triangle(half_phase) + Triangle(phase_));

      const float naive = w_saw * (2.0f * phase_ - 1.0f) +
          w_square * (phase_ < pw ? -1.0f : 1.0f) +
          w_triangle * triangle;
      next_sample += naive;
      previous_pw_ = pw;

      // The morph oscillator, band-limited. Braids filters and fuzzes THIS.
      const float sample = this_sample;

      // :117-118 -- the one-pole, with Braids' own clip on the state. The
      // coefficient is the 96 kHz one and the loop runs at 96 kHz.
      lp_state_ += (sample - lp_state_) * lp_a;
      CONSTRAIN(lp_state_, -1.0f, 1.0f);
      // :121-122 -- the shaper reads the filter state, and the result is
      // crossfaded against the DRY pre-filter sample, not against the filtered
      // one. MACRO's drive is the only thing here Braids does not have.
      const float fuzzed = ReadShaper(lp_state_ * drive);
      const float processed = sample + (fuzzed - sample) * fuzz;
      history_[history_position_] = processed;
      history_[history_position_ + 32] = history_[history_position_];

      if (stereo) {
        lp_state_aux_ += (sample - lp_state_aux_) * lp_a_aux;
        CONSTRAIN(lp_state_aux_, -1.0f, 1.0f);
        const float fuzzed_aux = ReadShaper(lp_state_aux_ * drive);
        const float processed_aux =
            sample + (fuzzed_aux - sample) * fuzz_aux;
        history_aux_[history_position_] = processed_aux;
      } else {
        // Mono AUX is the morph oscillator dry: the analog waveform the model
        // is built on, with neither the filter nor the fuzz on it.
        history_aux_[history_position_] = sample;
      }
      history_aux_[history_position_ + 32] =
          history_aux_[history_position_];

      history_position_ = (history_position_ + 1) & 31;
    }

    const float raw = Decimate(history_);
    const float raw_aux = Decimate(history_aux_);

    // Braids has no DC blocker, and at the top of TIMBRE its pulse carries up
    // to +0.98 of DC into whatever follows. R1 then applies: an engine with a
    // blocker cannot pin its post-blocker peak, so this one registers negative
    // gains and takes the limiter path.
    ONE_POLE(dc_input_, raw, 0.001f);
    ONE_POLE(dc_input_aux_, raw_aux, 0.001f);

    out[i] = raw - dc_input_;
    aux[i] = raw_aux - dc_input_aux_;
  }

  next_sample_ = next_sample;
}

}  // namespace plaits
