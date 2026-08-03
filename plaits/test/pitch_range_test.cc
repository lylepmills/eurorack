// Copyright 2026 Rubato Audio.
//
// Host test for the precision-tuning prototype's pure selector and pitch math.
// Build from the repository root with:
//
//   g++ -std=c++11 -I. plaits/test/pitch_range_test.cc \
//     -o /tmp/pitch_range_test && /tmp/pitch_range_test

#include "plaits/pitch_range.h"

#include <cmath>
#include <cstdio>

using namespace plaits;

static int checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { \
  std::printf("FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while (0)

static bool Near(float actual, float expected, float tolerance = 1e-5f) {
  return fabsf(actual - expected) <= tolerance;
}

int main() {
  // The new special positions are adjacent and the old eight wide ranges keep
  // their integer identities.
  for (int range = 0; range < PITCH_RANGE_COUNT; ++range) {
    const float centre = (static_cast<float>(range) + 0.5f)
        / static_cast<float>(PITCH_RANGE_COUNT);
    CHECK(PitchRangeFromControl(centre) == range);
  }
  CHECK(PITCH_RANGE_LAST_WIDE + 1 == PITCH_RANGE_OCTAVES);
  CHECK(PITCH_RANGE_OCTAVES + 1 == PITCH_RANGE_PRECISION);
  CHECK(PITCH_RANGE_PRECISION + 1 == PITCH_RANGE_HIGH);
  CHECK(PitchRangeFromControl(-0.1f) == PITCH_RANGE_LOW);
  CHECK(PitchRangeFromControl(1.0f) == PITCH_RANGE_HIGH);

  CHECK(Near(WideRangeNote(5, 0.0f), 60.0f));
  CHECK(Near(WideRangeNote(5, -1.0f), 53.0f));
  CHECK(Near(WideRangeNote(5, 1.0f), 67.0f));

  // Precision is exactly +/- one semitone around the captured manual pitch.
  CHECK(Near(PrecisionRangeNote(60.25f, -1.0f), 59.25f));
  CHECK(Near(PrecisionRangeNote(60.25f, 0.0f), 60.25f));
  CHECK(Near(PrecisionRangeNote(60.25f, 1.0f), 61.25f));

  // Changing roles starts from noon without a pitch jump. Movement beyond the
  // ordinary Plaits pickup threshold changes the virtual control immediately,
  // and reaching an endpoint completes pickup so subsequent tracking is direct.
  EndpointCatchUp catch_up;
  catch_up.Init(0.5f, 0.8f);
  CHECK(Near(catch_up.Process(0.8f), 0.5f));
  CHECK(Near(catch_up.Process(0.796f), 0.5f));
  const float first_movement = catch_up.Process(0.79f);
  CHECK(first_movement < 0.5f);
  CHECK(first_movement > 0.48f);
  CHECK(catch_up.catching_up());

  EndpointCatchUp catch_up_clockwise;
  catch_up_clockwise.Init(0.5f, 0.8f);
  CHECK(catch_up_clockwise.Process(1.0f) > 0.99f);
  CHECK(!catch_up_clockwise.catching_up());
  CHECK(Near(catch_up_clockwise.Process(0.9f), 0.9f));

  EndpointCatchUp catch_up_counterclockwise;
  catch_up_counterclockwise.Init(0.5f, 0.8f);
  CHECK(Near(catch_up_counterclockwise.Process(0.0f), 0.0f));
  CHECK(!catch_up_counterclockwise.catching_up());

  // Tuned roots persist at 1/256 semitone (about 0.39 cent), without squeezing
  // them through the legacy eight-bit value spread across fourteen semitones.
  const float roots[] = {
    5.0f, 36.0f, 59.6823f, 59.8021f, 60.0f, 60.3891f, 60.5041f, 84.0f, 108.0f
  };
  for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
    CHECK(Near(DecodeTunedRoot(EncodeTunedRoot(roots[i])), roots[i], 0.002f));
  }
  CHECK(EncodeTunedRoot(-200.0f) == -32768);
  CHECK(EncodeTunedRoot(200.0f) == 32767);

  std::printf("pitch_range_test: %d checks passed\n", checks);
  return 0;
}
