// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' vowel formant tables, vendored for vowel-fof.

#ifndef PLAITS_DSP_ENGINE2_VOWEL_FOF_DATA_H_
#define PLAITS_DSP_ENGINE2_VOWEL_FOF_DATA_H_

#include "stmlib/stmlib.h"

namespace plaits {

// [vowel][register][formant], flattened. 5 x 5 x 5.
extern const int16_t kVowelFofFrequency[125];
extern const int16_t kVowelFofAmplitude[125];

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_VOWEL_FOF_DATA_H_
