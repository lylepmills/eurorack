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
using plaits::OrderedLedIndex;

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

  std::printf("alternate_parameter_leds_test: all checks passed\n");
  return 0;
}
