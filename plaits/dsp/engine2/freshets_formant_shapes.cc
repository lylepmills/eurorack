// Copyright 2026 Dylan Bolink.
// SPDX-License-Identifier: MIT
//
// Waveshaping transfer curves for the formant envelope: the five audio-rate
// shapes of Tides' lut_wavetable, in Tides' order - inverse_tan, inverse_sin,
// linear, sin, bump.
//
// Four of the five are already in the firmware. Plaits' lut_ws_* tables are the
// odd extension of these same curves across [-1, 1]: 257 entries with the
// midpoint at index 128, so `table + 128` is exactly the 129-entry unipolar
// half this mode reads, byte for byte. Only `sin` - the soft S-curve Tides puts
// between linear and bump - has no counterpart in Plaits, so it is the only one
// carried here. Vendoring all five cost 1290 bytes; this costs 258 plus a
// five-entry pointer table.
//
// Curve values are identical either way. Plaits' tables and Tides' agree at
// every one of the 128 interpolated steps and differ only at the x = 1.0
// endpoint, which normalizes to full scale in Plaits and not in Tides - a point
// the envelope only reaches when it lands exactly on the attack/decay corner.

#include "plaits/dsp/engine2/freshets_formant_shapes.h"

#include "plaits/resources.h"

namespace plaits {

namespace {

// sin: (1 - cos(pi * x)) / 2, at the same 128-step resolution as the shared
// tables so all five curves crossfade against each other on equal terms.
const int16_t kFormantShapeSin[] = {
  0,5,20,44,79,123,177,241,315,398,491,
  593,705,827,958,1098,1247,1406,1573,1749,1935,2128,
  2331,2542,2761,2989,3224,3468,3719,3978,4244,4518,4799,
  5086,5381,5682,5990,6304,6624,6950,7281,7618,7961,8308,
  8660,9017,9379,9744,10114,10487,10864,11244,11628,12014,12403,
  12794,13187,13583,13980,14378,14778,15178,15580,15981,16383,16786,
  17187,17589,17989,18389,18787,19184,19580,19973,20364,20753,21139,
  21523,21903,22280,22653,23023,23388,23750,24107,24459,24806,25149,
  25486,25817,26143,26463,26777,27085,27386,27681,27968,28249,28523,
  28789,29048,29299,29543,29778,30006,30225,30436,30639,30832,31018,
  31194,31361,31520,31669,31809,31940,32062,32174,32276,32369,32452,
  32526,32590,32644,32688,32723,32747,32762,32767,
};

}  // namespace

const int16_t* const kFormantShape[kFormantShapeCount] = {
  lut_ws_inverse_tan + 128,
  lut_ws_inverse_sin + 128,
  lut_ws_linear + 128,
  kFormantShapeSin,
  lut_ws_bump + 128,
};

}  // namespace plaits
