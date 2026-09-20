// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// TWIST tuning behavior for the five engines whose fourth macro scales or
// offsets a PITCH interval the player already chose as musical, rather than
// moving a continuous timbre the way the other ~90 engines' macros do.
//
// ApplyMacro's contract -- midpoint exactly neutral, halves reaching useful
// extremes -- is not in question here and is not changed by any mode below:
// every mode returns the module's own value at MACRO 0.5, bit for bit. What
// differs is how wide, and how finely spaced, the travel either side of noon
// is. All three modes are selectable per ENGINE INSTANCE so one firmware can
// carry the same engine three times for an A/B; a build that never calls
// set_twist_tuning() keeps whatever PLAITS_BUILD_TWIST_TUNING_RANGE selects,
// which defaults to the shipped behavior.

#ifndef PLAITS_DSP_ENGINE_TWIST_TUNING_H_
#define PLAITS_DSP_ENGINE_TWIST_TUNING_H_

#include "stmlib/dsp/units.h"

#include "plaits/build_config.h"
#include "plaits/dsp/engine/engine.h"

namespace plaits {

enum TwistTuning {
  // The shipped span. Continuous, and wide enough that the chosen interval
  // survives only within a fraction of a percent of the knob's travel.
  TWIST_TUNING_STOCK = 0,
  // The same continuous curve over a much smaller span: a fine trim the whole
  // knob can express. Cheapest fix, but the shipped span's musical endpoints
  // (unison, doubled width, octave up/down) become unreachable.
  TWIST_TUNING_NARROW = 1,
  // The shipped span, snapped to musical steps. Noon keeps a wide capture
  // region and the endpoints survive, at the cost of the control no longer
  // being continuous.
  TWIST_TUNING_QUANTIZED = 2
};

#if PLAITS_BUILD_TWIST_TUNING_RANGE == 1
const TwistTuning kDefaultTwistTuning = TWIST_TUNING_NARROW;
#elif PLAITS_BUILD_TWIST_TUNING_RANGE == 2
const TwistTuning kDefaultTwistTuning = TWIST_TUNING_QUANTIZED;
#else
const TwistTuning kDefaultTwistTuning = TWIST_TUNING_STOCK;
#endif

// Nearest multiple of `step`. Runs once per block, so the int round is for
// libm avoidance (no lrintf, no __errno) rather than for speed.
inline float QuantizeToStep(float value, float step) {
  const float scaled = value / step;
  const int nearest = static_cast<int>(
      scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
  return static_cast<float>(nearest) * step;
}

// For a macro that SCALES a stored interval, where the module's own value is
// x1 at noon and the shipped span is x0 .. x2 (i.e. plain `macro * 2`).
// `narrow_span` is the half-width the narrowed mode keeps around x1;
// `quantize_step` is the scale increment the quantized mode snaps to, which
// must divide 1.0f so that noon lands on x1 exactly.
inline float TwistIntervalScale(
    TwistTuning mode,
    float macro,
    float narrow_span,
    float quantize_step) {
  switch (mode) {
    case TWIST_TUNING_NARROW:
      return ApplyMacro(
          1.0f, 1.0f - narrow_span, 1.0f + narrow_span, macro);
    case TWIST_TUNING_QUANTIZED:
      return QuantizeToStep(macro * 2.0f, quantize_step);
    default:
      // Bit-identical to the shipped `macro * 2.0f`: ApplyMacro's lower half
      // is 0 + (1 - 0) * 2m and its upper half is 1 + (2 - 1) * (2m - 1),
      // both of which are exact in float for m in [0, 1].
      return macro * 2.0f;
  }
}

// For a macro that OFFSETS a pitch by a signed number of semitones, zero at
// noon. The quantized mode keeps the shipped span and snaps to whole
// semitones, so every position is a musical interval from the chosen one.
inline float TwistSemitoneOffset(
    TwistTuning mode,
    float macro,
    float stock_span,
    float narrow_span) {
  switch (mode) {
    case TWIST_TUNING_NARROW:
      return ApplyMacro(0.0f, -narrow_span, narrow_span, macro);
    case TWIST_TUNING_QUANTIZED:
      return QuantizeToStep(
          ApplyMacro(0.0f, -stock_span, stock_span, macro), 1.0f);
    default:
      return ApplyMacro(0.0f, -stock_span, stock_span, macro);
  }
}

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_TWIST_TUNING_H_
