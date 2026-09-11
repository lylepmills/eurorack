// Copyright 2026 Rubato Audio.
//
// Host test for the panel-specific hidden-parameter LED direction. Build with:
//
//   g++ -std=c++11 -I<repo-root> \
//       plaits/test/alternate_parameter_leds_test.cc -o /tmp/aplt && /tmp/aplt

#include "plaits/alternate_parameter_leds.h"

#include <cassert>
#include <cstdio>

using plaits::AlternateParameterLedIndex;
using plaits::ChordTableLedIndex;
using plaits::LpgColourSegmentFill;
using plaits::OrderedLedIndex;
using plaits::ShowLockedOctave;
using plaits::TrigResponseLedValue;

int main() {
  const int stock[] = {3, 2, 1, 0, 7, 6, 5, 4};
  const int roved[] = {0, 1, 2, 3, 4, 5, 6, 7};

  for (int parameter = 0; parameter < 2; ++parameter) {
    for (int segment = 0; segment < 4; ++segment) {
      const int offset = parameter * 4 + segment;
      assert(AlternateParameterLedIndex(parameter, segment, false) ==
          stock[offset]);
      assert(AlternateParameterLedIndex(parameter, segment, true) ==
          roved[offset]);
    }
  }

  // An ordered display (octave, frequency range) has to climb in the panel's
  // own reading direction: up Plaits' column, rightwards along Ro'Ved's row.
  // Getting this backwards makes the lights run against a clockwise turn.
  for (int step = 0; step < 8; ++step) {
    assert(OrderedLedIndex(step, false) == 7 - step);
    assert(OrderedLedIndex(step, true) == step);
  }
  // The two panels disagree on every step but the middle pair.
  assert(OrderedLedIndex(0, false) == 7);
  assert(OrderedLedIndex(0, true) == 0);
  assert(OrderedLedIndex(7, false) == 0);
  assert(OrderedLedIndex(7, true) == 7);

  // Loaded tables are listed from the top down, so the physical table display
  // uses that same order on Plaits instead of the range ladder's bottom-up one.
  for (int position = 0; position < 8; ++position) {
    assert(ChordTableLedIndex(position) == position);
  }

  // LIGHT 4 has no amber state: velocity is represented by blinking the
  // corresponding non-velocity colour.
  const int trig_response_led_values[] = { 0, 1, 3, 4 };
  for (int option = 0; option < 4; ++option) {
    assert(TrigResponseLedValue(option) == trig_response_led_values[option]);
  }

  // The folded COLOUR bar shows filter amount from either end: the low pass
  // half climbs from segment 0, the high pass half from segment 3, and the
  // VCA detent in the middle is dark. No two knob positions share a picture.
  for (int segment = 0; segment < 4; ++segment) {
    assert(LpgColourSegmentFill(0.0f, segment) == 1.0f);
    assert(LpgColourSegmentFill(1.0f, segment) == 1.0f);
    assert(LpgColourSegmentFill(0.5f, segment) == 0.0f);
    assert(LpgColourSegmentFill(0.48f, segment) == 0.0f);
    assert(LpgColourSegmentFill(0.52f, segment) == 0.0f);
  }
  // A quarter of the way out on the low pass side lights only segment 0 ...
  assert(LpgColourSegmentFill(0.5f - 0.475f * 0.3f, 0) > 0.0f);
  assert(LpgColourSegmentFill(0.5f - 0.475f * 0.3f, 1) == 0.0f);
  assert(LpgColourSegmentFill(0.5f - 0.475f * 0.3f, 3) == 0.0f);
  // ... and the same distance on the high pass side lights only segment 3.
  assert(LpgColourSegmentFill(0.5f + 0.475f * 0.3f, 3) > 0.0f);
  assert(LpgColourSegmentFill(0.5f + 0.475f * 0.3f, 2) == 0.0f);
  assert(LpgColourSegmentFill(0.5f + 0.475f * 0.3f, 0) == 0.0f);
  // Fill grows monotonically away from the centre on both sides.
  for (int step = 1; step < 50; ++step) {
    const float d = 0.5f * step / 50.0f;
    for (int segment = 0; segment < 4; ++segment) {
      assert(LpgColourSegmentFill(0.5f - d, segment) >=
          LpgColourSegmentFill(0.5f - d + 0.01f, segment));
      assert(LpgColourSegmentFill(0.5f + d, segment) >=
          LpgColourSegmentFill(0.5f + d - 0.01f, segment));
    }
  }

  // Preparing the locked-octave shortcut must not mask a simultaneous
  // HARMONICS range edit. With no range edit, the shortcut still displays.
  assert(ShowLockedOctave(true, false));
  assert(!ShowLockedOctave(true, true));
  assert(!ShowLockedOctave(false, false));
  assert(!ShowLockedOctave(false, true));

  std::printf("alternate_parameter_leds_test: all checks passed\n");
  return 0;
}
