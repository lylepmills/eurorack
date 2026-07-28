// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' interval ladder for the TRIPLE models, in semitones.
//
// The near-centre entries are what make unison reachable: index 31 is
// -3.125 cents and index 32 is exactly zero, and Braids' crossfade weight at
// the knob centre is 255/256, so the two land within a hundredth of a cent of
// unison. A cleaner re-parameterisation of this ladder would silently break
// that, which is why the arithmetic is reproduced rather than tidied.

#ifndef PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_DATA_H_
#define PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_DATA_H_

namespace plaits {

const int kTripleNumIntervals = 65;

const float kTripleIntervals[kTripleNumIntervals] = {
  -24.000000f, -24.000000f, -23.968750f, -23.000000f, -22.000000f, -21.000000f,
  -20.000000f, -19.000000f, -18.000000f, -17.031250f, -17.000000f, -16.000000f,
  -15.000000f, -14.000000f, -13.000000f, -12.031250f, -12.000000f, -11.000000f,
  -10.000000f, -9.000000f, -8.000000f, -7.031250f, -7.000000f, -6.000000f,
  -5.000000f, -4.000000f, -3.000000f, -2.000000f, -1.000000f, -0.187500f,
  -0.062500f, -0.031250f, 0.000000f, 0.031250f, 0.062500f, 0.187500f,
  1.000000f, 2.000000f, 3.000000f, 4.000000f, 5.000000f, 6.000000f,
  7.000000f, 7.031250f, 8.000000f, 9.000000f, 10.000000f, 11.000000f,
  12.000000f, 12.031250f, 13.000000f, 14.000000f, 15.000000f, 16.000000f,
  17.000000f, 17.031250f, 18.000000f, 19.000000f, 20.000000f, 21.000000f,
  22.000000f, 23.000000f, 23.968750f, 24.000000f, 24.000000f
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_DATA_H_
