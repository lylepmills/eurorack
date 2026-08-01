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

  std::printf("alternate_parameter_leds_test: all checks passed\n");
  return 0;
}
