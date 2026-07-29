// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' interval ladder for the TRIPLE models, in semitones.
//
// The near-centre entries are what make unison reachable, but NOT by the route
// this comment used to give. Index 32 is exactly zero and index 31 is -3.125
// cents, and the 255/256 crossfade at the knob centre (p = 16383) does NOT land
// near unison in the module: macro_oscillator.cc:207 ends the crossfade with an
// integer `>> 16`, which floors that position to -1/128 semitone = -0.781 cents.
// What both implementations DO reach is index 32 with a zero crossfade -- every
// parameter from 16384 to 16639 resolves i1 == i2 == 32 and detunes by exactly
// nothing. (Braids' zero-detune plateau actually runs to 16703, because the
// same floor swallows the first 64 steps of the crossfade toward index 33; the
// port's stops at 16640. See the OPEN DEVIATION block in triple_engine.h.)
// A cleaner re-parameterisation of this ladder would silently break the
// plateau, which is why the arithmetic is reproduced rather than tidied.
//
// This file is NOT declared in the catalog's `source.files` (the schema accepts
// only `.cc` there, plaits_lab.py:489), so these 65 values sit outside
// `package_digest`. vowel-fof avoids that by putting its tables in a declared
// `vowel_fof_data.cc` behind an extern-only header; doing the same here is a
// digest move and so is reported rather than made.

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
