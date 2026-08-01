// Copyright 2026 Rubato Audio.
//
// Physical LED mapping for the two four-segment hidden-parameter meters.

#ifndef PLAITS_ALTERNATE_PARAMETER_LEDS_H_
#define PLAITS_ALTERNATE_PARAMETER_LEDS_H_

namespace plaits {

// Stock Plaits presents its LED column top-to-bottom, while Ro'Ved presents
// the same eight driver outputs as two left-to-right rows. Keep increasing LPG
// colour/level and decay values filling in the panel's natural direction.
inline int AlternateParameterLedIndex(
    int parameter, int segment, bool roved_panel) {
  return parameter * 4 + (roved_panel ? segment : 3 - segment);
}

}  // namespace plaits

#endif  // PLAITS_ALTERNATE_PARAMETER_LEDS_H_
