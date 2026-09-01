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

// Where the Nth step of an ordered display lights up, for the same reason.
// Plaits' column is read bottom-to-top, so a rising value walks DOWN the
// driver indices; Ro'Ved's row is read left-to-right, so it walks UP them.
// Without this the octave and frequency-range displays run right-to-left on
// Ro'Ved while the knob turns clockwise.
inline int OrderedLedIndex(int step, bool roved_panel) {
  return roved_panel ? step : 7 - step;
}

}  // namespace plaits

#endif  // PLAITS_ALTERNATE_PARAMETER_LEDS_H_
