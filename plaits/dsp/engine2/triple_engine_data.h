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
// nothing. The plateau in fact runs to 16703, because the same floor swallows
// the first 64 steps of the crossfade toward index 33; since LadderDetune was
// given Braids' integer floor back it runs to 16703 in the port too, so the two
// plateaux now coincide exactly. A cleaner re-parameterisation of this ladder
// would silently break it, which is why the arithmetic is reproduced rather
// than tidied.
//
// These values are consumed as Braids' int16_t, in 1/128-semitone units: every
// entry is one of those integers over 128 and so an exact binary fraction, and
// triple_engine.cc recovers the integer with a bare `* 128.0f` before doing the
// crossfade shift. Verified exact for all 65 entries. Anything added here must
// stay on the 1/128 grid or that recovery starts truncating.

#ifndef PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_DATA_H_
#define PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_DATA_H_

namespace plaits {

extern const int kTripleNumIntervals;
extern const float kTripleIntervals[65];

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_TRIPLE_ENGINE_DATA_H_
