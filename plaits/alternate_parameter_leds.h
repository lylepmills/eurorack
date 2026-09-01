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

// Chord tables follow the order printed by Palette's Loaded tables list:
// table 1 is the top Plaits light (and the left Ro'Ved light), then the
// selection proceeds down/right as HARMONICS turns clockwise. This is
// deliberately not OrderedLedIndex: the frequency-range ladder reads upward
// on Plaits, while this display mirrors an ordered list shown top-to-bottom.
inline int ChordTableLedIndex(int position) {
  return position;
}

// TRIG response is a two-by-two cross product rather than a three-colour
// sequence: trigger/gate is green/red, and velocity adds the blink tier.
// Translate its four stored values into the generic option renderer's
// green, red, blink-green, blink-red positions (0, 1, 3, 4).
inline int TrigResponseLedValue(int option) {
  return option + (option >= 2 ? 1 : 0);
}

// With FREQUENCY locked, stock Plaits prepares right-button + MORPH for the
// octave shortcut at the same time that right-button + HARMONICS edits the
// frequency range. The range display must win when HARMONICS is the knob that
// actually moved; otherwise the stored octave masks it (usually as LED 4/5).
inline bool ShowLockedOctave(
    bool locked_octave_context, bool editing_frequency_range) {
  return locked_octave_context && !editing_frequency_range;
}

}  // namespace plaits

#endif  // PLAITS_ALTERNATE_PARAMETER_LEDS_H_
