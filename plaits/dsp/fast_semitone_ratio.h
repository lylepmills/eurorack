// Copyright 2026 Rubato Audio.

#ifndef PLAITS_DSP_FAST_SEMITONE_RATIO_H_
#define PLAITS_DSP_FAST_SEMITONE_RATIO_H_

#include "stmlib/dsp/units.h"

namespace plaits {

// Same two-table approximation as stmlib::SemitonesToRatio(), with the
// integer semitone and 1/256-semitone indices extracted from one Q8
// conversion. This removes one floating-point-to-integer dependency from
// sample-rate pitch modulation without replacing the proven lookup curve with
// a polynomial. Like the original, the caller keeps semitones in [-128, 128].
inline float FastSemitonesToRatio(float semitones) {
  const int32_t pitch_q8 = static_cast<int32_t>(
      (semitones + 128.0f) * 256.0f);
  return stmlib::lut_pitch_ratio_high[pitch_q8 >> 8] *
      stmlib::lut_pitch_ratio_low[pitch_q8 & 0xff];
}

}  // namespace plaits

#endif  // PLAITS_DSP_FAST_SEMITONE_RATIO_H_
