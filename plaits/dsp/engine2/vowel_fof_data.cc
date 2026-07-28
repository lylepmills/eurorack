// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' formant tables, vendored. 5 vowels x 5 registers x 5 formants.
//
// NOT shared with NaiveSpeechSynth, which holds the same grid: its uint8
// quantisation carries about half a semitone of centre error, and these
// filters run at Q = 64, whose bandwidth is 0.24 semitones. Half a semitone of
// error on a quarter-semitone filter is not a rounding difference, it is a
// different formant. 500 bytes buys the pitch accuracy the model needs.
//
// Frequencies are in 1/128-semitone MIDI units. Amplitudes are LINEAR, with
// 16384 as unity -- they are not semitone attenuations, so a tilt law built on
// SemitonesToRatio would be shaping the wrong quantity.

#include "plaits/dsp/engine2/vowel_fof_data.h"

namespace plaits {

const int16_t kVowelFofFrequency[125] = {
  9519, 10738, 12448, 12636, 12892,
  8620, 11720, 12591, 12932, 13158,
  7579, 11891, 12768, 13122, 13323,
  8620, 10013, 12591, 12768, 13010,
  8324, 9519, 12591, 12831, 13048,
  9696, 10821, 12810, 13010, 13263,
  8620, 11827, 12768, 13228, 13477,
  7908, 12038, 12932, 13263, 13452,
  8620, 10156, 12768, 12932, 13085,
  8324, 9519, 12852, 13010, 13296,
  9730, 10902, 12892, 13085, 13330,
  8832, 11953, 12852, 13085, 13296,
  7749, 12014, 13010, 13330, 13483,
  8781, 10211, 12852, 13085, 13296,
  8448, 9627, 12892, 13085, 13363,
  10156, 10960, 12932, 13427, 14195,
  8620, 11692, 12852, 13296, 14195,
  8324, 11827, 12852, 13550, 14195,
  8881, 10156, 12956, 13427, 14195,
  8160, 9860, 12708, 13427, 14195,
  10156, 10960, 13010, 13667, 14195,
  8324, 12187, 12932, 13489, 14195,
  7749, 12337, 13048, 13667, 14195,
  8881, 10156, 12956, 13609, 14195,
  8160, 9860, 12852, 13609, 14195
};

const int16_t kVowelFofAmplitude[125] = {
  16384, 7318, 5813, 5813, 1638,
  16384, 4115, 5813, 4115, 2062,
  16384, 518, 2596, 1301, 652,
  16384, 4617, 1460, 1638, 163,
  16384, 1638, 411, 652, 259,
  16384, 8211, 7318, 6522, 1301,
  16384, 3269, 4115, 3269, 1638,
  16384, 2913, 2062, 1638, 518,
  16384, 5181, 4115, 4115, 821,
  16384, 1638, 2314, 3269, 821,
  16384, 8211, 1159, 1033, 206,
  16384, 3269, 2062, 1638, 1638,
  16384, 1033, 1033, 259, 259,
  16384, 5181, 821, 1301, 326,
  16384, 1638, 1159, 518, 326,
  16384, 10337, 1638, 259, 16,
  16384, 1033, 518, 291, 16,
  16384, 1638, 518, 259, 16,
  16384, 5813, 2596, 652, 29,
  16384, 4115, 518, 163, 10,
  16384, 8211, 411, 1638, 51,
  16384, 1638, 2913, 163, 25,
  16384, 4115, 821, 821, 103,
  16384, 4617, 1301, 1301, 51,
  16384, 2596, 291, 163, 16
};

}  // namespace plaits
