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

#include "stmlib/dsp/hysteresis_quantizer.h"

using namespace plaits;

static int checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { \
  std::printf("FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while (0)

static bool Near(float actual, float expected, float tolerance = 1e-5f) {
  return fabsf(actual - expected) <= tolerance;
}

// The selector value that sits in the middle of a range. Config-aware, so the
// simulator below replays a sweep correctly under either selector layout: the
// simplified one gives each of its three ranges a third of the travel, so the
// same range sits at a different knob position than it does in a full build.
static float SelectorFor(int range) {
#if PLAITS_BUILD_SIMPLIFIED_PITCH_RANGES
  return (static_cast<float>(range - kSimplifiedFirstRange) + 0.5f)
      / static_cast<float>(kSimplifiedRangeCount);
#else
  return (static_cast<float>(range) + 0.5f)
      / static_cast<float>(PITCH_RANGE_COUNT);
#endif
}

// Mirrors the range-transition bookkeeping at the tail of Ui::Poll, so that a
// selector sweep can be replayed without hardware. Only the octave offset is
// abbreviated: the module derives it from a hysteresis quantizer over the
// FREQUENCY knob, which is irrelevant to where the root comes from.
struct RangeSim {
  float transposition;      // FREQUENCY knob, in [-1, +1].
  float note;               // patch_->note.
  float tuned_root;         // tuned_root_note_.
  float precision_anchor;   // precision_anchor_note_.
  int octave_offset;        // The selected octave, in [-4, +4].
  int previous_range;
  EndpointCatchUp precision_catch_up;
  EndpointCatchUp octave_catch_up;
  OctaveRootSnapshot snapshot;

  void Init(float transposition_value, float saved_root, float selector) {
    transposition = transposition_value;
    tuned_root = saved_root;
    precision_anchor = saved_root;
    note = 0.0f;
    octave_offset = 0;
    previous_range = PitchRangeFromControl(selector);
    precision_catch_up.Reset();
    octave_catch_up.Reset();
    snapshot.Reset();
    Tick(selector, false);          // Settle in the starting range.
  }

  void Tick(float selector, bool editing) {
    const int pitch_range = PitchRangeFromControl(selector);

    snapshot.Track(editing, previous_range, note, tuned_root);

    if (pitch_range != previous_range) {
      if (pitch_range == PITCH_RANGE_PRECISION) {
        precision_anchor = note;
        precision_catch_up.Init(0.5f, 0.5f * transposition + 0.5f);
      } else if (pitch_range == PITCH_RANGE_OCTAVES) {
        tuned_root = snapshot.Root(note);
        precision_anchor = tuned_root;
        octave_offset = 0;
        octave_catch_up.Init(0.5f, 0.5f * transposition + 0.5f);
      } else if (previous_range == PITCH_RANGE_PRECISION) {
        tuned_root = note;
        precision_anchor = tuned_root;
      }
      previous_range = pitch_range;
    }

    if (pitch_range == PITCH_RANGE_LOW) {
      note = -48.37f + transposition * 60.0f;
    } else if (pitch_range == PITCH_RANGE_PRECISION) {
      const float fine = precision_catch_up.Process(
          0.5f * transposition + 0.5f);
      note = PrecisionRangeNote(precision_anchor, 2.0f * fine - 1.0f);
      tuned_root = note;
    } else if (pitch_range == PITCH_RANGE_OCTAVES) {
      note = tuned_root + 12.0f * static_cast<float>(octave_offset);
    } else if (pitch_range == PITCH_RANGE_HIGH) {
      note = 60.0f + transposition * 48.0f;
    } else {
      note = WideRangeNote(pitch_range, transposition);
    }
  }

  // Turns the selector from one range to another the way a knob does, at the
  // control rate, with the gesture held throughout.
  void Sweep(int from_range, int to_range) {
    const float from = SelectorFor(from_range);
    const float to = SelectorFor(to_range);
    const int steps = 500;
    for (int i = 1; i <= steps; ++i) {
      Tick(from + (to - from) * static_cast<float>(i)
          / static_cast<float>(steps), true);
    }
  }

  void ReleaseSelector() {
    Tick(SelectorFor(previous_range), false);
  }
};

static const int kWideRangeMiddleC = 5;   // WideRangeNote(5, 0) == 60.

// Mirrors the locked-FREQUENCY-knob option handling in Ui (the
// OPTION_LIGHT_FREQUENCY_POT branch of the options menu, plus the
// PITCH_RANGE_OCTAVES arm of Ui::Poll), so that cycling the menu can be
// replayed without hardware. Octave switching is value 0 of that option, so
// walking the menu necessarily passes THROUGH it on the way to anything else.
struct LockedOptionSim {
  uint8_t locked_octave;
  int option;
  float transposition;            // FREQUENCY knob, in [-1, +1].
  float selector;                 // The hidden range selector.
  EndpointCatchUp octave_catch_up;
  stmlib::HysteresisQuantizer2 octave_quantizer;

  void Init(uint8_t stored_octave, int starting_option, float knob,
      float selector_value) {
    locked_octave = stored_octave;
    option = starting_option;
    transposition = knob;
    selector = selector_value;
    octave_catch_up.Reset();
    octave_quantizer.Init(9, 0.01f, false);
  }

  // One step of the options menu, in either direction.
  void StepOption(int delta, int num_values) {
    const int old_value = option;
    int new_value = option + delta;
    if (new_value < 0) new_value = num_values - 1;
    else if (new_value >= num_values) new_value = 0;

    if (old_value == 0 && new_value != 0 &&
        PitchRangeFromControl(selector) == PITCH_RANGE_OCTAVES) {
      locked_octave = static_cast<uint8_t>(
          octave_quantizer.Process(
              octave_catch_up.Process(0.5f * transposition + 0.5f)));
    } else if (old_value != 0 && new_value == 0) {
      octave_catch_up.Init(
          static_cast<float>(locked_octave) / 8.0f,
          0.5f * transposition + 0.5f);
    }
    option = new_value;
  }

  // The octave offset Ui::Poll would be sounding right now.
  int SoundingOctaveOffset() {
    if (PitchRangeFromControl(selector) != PITCH_RANGE_OCTAVES) return 0;
    if (option == 0) {
      return octave_quantizer.Process(
          octave_catch_up.Process(0.5f * transposition + 0.5f)) - 4;
    }
    return static_cast<int>(locked_octave) - 4;
  }

  // Turning the FREQUENCY knob, at the control rate.
  void TurnKnob(float target) {
    const int steps = 200;
    const float from = transposition;
    for (int i = 1; i <= steps; ++i) {
      transposition = from + (target - from)
          * static_cast<float>(i) / static_cast<float>(steps);
      SoundingOctaveOffset();          // Poll runs while the knob moves.
    }
  }
};

int main() {
  // The special positions are adjacent and the eight wide ranges keep their
  // integer identities. Both layouts depend on that contiguity, so it is
  // asserted in both.
  CHECK(PITCH_RANGE_LAST_WIDE + 1 == PITCH_RANGE_OCTAVES);
  CHECK(PITCH_RANGE_OCTAVES + 1 == PITCH_RANGE_PRECISION);
  CHECK(PITCH_RANGE_PRECISION + 1 == PITCH_RANGE_HIGH);

#if PLAITS_BUILD_SIMPLIFIED_PITCH_RANGES
  // Three equal thirds, landing on octave switching, fine tuning and coarse.
  for (int index = 0; index < kSimplifiedRangeCount; ++index) {
    const float centre = (static_cast<float>(index) + 0.5f)
        / static_cast<float>(kSimplifiedRangeCount);
    CHECK(PitchRangeFromControl(centre) == kSimplifiedFirstRange + index);
  }
  CHECK(PitchRangeFromControl(0.0f) == PITCH_RANGE_OCTAVES);
  CHECK(PitchRangeFromControl(-0.1f) == PITCH_RANGE_OCTAVES);
  CHECK(PitchRangeFromControl(1.0f) == PITCH_RANGE_HIGH);
  CHECK(PitchRangeFromControl(1.5f) == PITCH_RANGE_HIGH);

  // The point of the option: no knob position anywhere reaches a wide range or
  // the LFO range. Sweeping the whole travel must only ever produce the three.
  for (int i = 0; i <= 2000; ++i) {
    const int range = PitchRangeFromControl(
        static_cast<float>(i) / 2000.0f);
    CHECK(range >= PITCH_RANGE_OCTAVES && range <= PITCH_RANGE_HIGH);
  }

  // A module ships with state.octave == 255, which must still land on coarse
  // rather than stranding a fresh build in fine tuning or octave switching.
  CHECK(PitchRangeFromControl(255.0f / 256.0f) == PITCH_RANGE_HIGH);
#else
  for (int range = 0; range < PITCH_RANGE_COUNT; ++range) {
    const float centre = (static_cast<float>(range) + 0.5f)
        / static_cast<float>(PITCH_RANGE_COUNT);
    CHECK(PitchRangeFromControl(centre) == range);
  }
  CHECK(PitchRangeFromControl(-0.1f) == PITCH_RANGE_LOW);
  CHECK(PitchRangeFromControl(1.0f) == PITCH_RANGE_HIGH);

  // The same factory default lands on coarse in a full build too.
  CHECK(PitchRangeFromControl(255.0f / 256.0f) == PITCH_RANGE_HIGH);
#endif

  CHECK(Near(WideRangeNote(5, 0.0f), 60.0f));
  CHECK(Near(WideRangeNote(5, -1.0f), 53.0f));
  CHECK(Near(WideRangeNote(5, 1.0f), 67.0f));

  // Precision is exactly +/- one semitone around the captured manual pitch.
  CHECK(Near(PrecisionRangeNote(60.25f, -1.0f), 59.25f));
  CHECK(Near(PrecisionRangeNote(60.25f, 0.0f), 60.25f));
  CHECK(Near(PrecisionRangeNote(60.25f, 1.0f), 61.25f));

  // The right-button + FREQUENCY shortcut is a broad, relative retune around
  // the persisted root. The octave currently being sounded is deliberately
  // absent from this math, so editing from (say) root + two octaves cannot bake
  // that octave into the saved tuning.
  CHECK(Near(RootRetuneNote(60.25f, -1.0f), 53.25f));
  CHECK(Near(RootRetuneNote(60.25f, 0.0f), 60.25f));
  CHECK(Near(RootRetuneNote(60.25f, 1.0f), 67.25f));
  CHECK(Near(RootRetuneNote(-127.0f, -1.0f), -128.0f));
  CHECK(Near(RootRetuneNote(127.0f, 1.0f), 32767.0f / 256.0f));

  EndpointCatchUp retune;
  retune.Init(0.5f, 0.8f);
  CHECK(Near(RootRetuneNote(60.0f,
      2.0f * retune.Process(0.8f) - 1.0f), 60.0f));
  const float retuned_down = RootRetuneNote(
      60.0f, 2.0f * retune.Process(0.7f) - 1.0f);
  CHECK(retuned_down < 60.0f);
  CHECK(retuned_down > 53.0f);

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

  // A fine-tuning gesture produces one save only after settling. Further
  // changes restart the delay, returning to the saved value cancels it, and an
  // unrelated state save can explicitly consume a pending write.
  DeferredValueSave deferred_save;
  deferred_save.Init(100, 3);
  CHECK(!deferred_save.Process(101));
  CHECK(!deferred_save.Process(101));
  CHECK(!deferred_save.Process(102));
  CHECK(!deferred_save.Process(102));
  CHECK(!deferred_save.Process(102));
  CHECK(deferred_save.Process(102));
  CHECK(!deferred_save.Process(102));
  CHECK(!deferred_save.Process(103));
  CHECK(!deferred_save.Process(102));
  CHECK(!deferred_save.Process(102));
  CHECK(!deferred_save.Process(104));
  deferred_save.MarkSaved(104);
  CHECK(!deferred_save.Process(104));

  // The default root is middle C, and encodes to the same byte pair the factory
  // defaults have always written. True of both layouts.
  CHECK(Near(kDefaultTunedRootNote, 60.0f));
  CHECK(EncodeTunedRoot(kDefaultTunedRootNote) == 60 * 256);

#if !PLAITS_BUILD_SIMPLIFIED_PITCH_RANGES
  // Everything below replays sweeps that cross the wide and LFO ranges, so it
  // describes the full twelve-position selector only. The simplified layout's
  // equivalents follow in the #else branch.

  // The octave-switching root is the pitch that was sounding when the selector
  // gesture began, not whatever the ranges crossed on the way there left
  // behind. Reaching the mode from below used to cross wide range 8 and root
  // it at note 96 (C7), three octaves sharp.
  RangeSim from_below;
  from_below.Init(0.0f, 60.0f, SelectorFor(kWideRangeMiddleC));
  CHECK(Near(from_below.note, 60.0f));
  from_below.Sweep(kWideRangeMiddleC, PITCH_RANGE_OCTAVES);
  CHECK(from_below.previous_range == PITCH_RANGE_OCTAVES);
  CHECK(Near(from_below.tuned_root, 60.0f));
  CHECK(Near(from_below.note, 60.0f));

  // Reaching it from above, down through Fine tuning, was always right and
  // stays right.
  RangeSim from_above;
  from_above.Init(0.0f, 60.0f, SelectorFor(PITCH_RANGE_HIGH));
  CHECK(Near(from_above.note, 60.0f));
  from_above.Sweep(PITCH_RANGE_HIGH, PITCH_RANGE_OCTAVES);
  CHECK(Near(from_above.tuned_root, 60.0f));

  // Both directions agree for any starting pitch, including one the player set
  // in an ordinary range with the FREQUENCY knob off noon.
  for (int i = 0; i < 5; ++i) {
    const float transposition = -1.0f + 0.5f * static_cast<float>(i);
    RangeSim up;
    up.Init(transposition, 60.0f, SelectorFor(kWideRangeMiddleC));
    const float sounding = up.note;
    up.Sweep(kWideRangeMiddleC, PITCH_RANGE_OCTAVES);
    CHECK(Near(up.tuned_root, sounding));

    RangeSim down;
    down.Init(transposition, 60.0f, SelectorFor(PITCH_RANGE_HIGH));
    const float sounding_high = down.note;
    down.Sweep(PITCH_RANGE_HIGH, PITCH_RANGE_OCTAVES);
    CHECK(Near(down.tuned_root, sounding_high));
  }

  // Starting from the LFO range is the one case where the sounding pitch is no
  // use: it is sub-audio, so it would root every octave position at or below
  // the bottom of hearing. Leaving the LFO range for octave switching lands on
  // the default root instead, from either side of the selector.
  RangeSim from_lfo;
  from_lfo.Init(0.0f, 84.0f, SelectorFor(PITCH_RANGE_LOW));
  CHECK(from_lfo.note < 0.0f);
  from_lfo.Sweep(PITCH_RANGE_LOW, PITCH_RANGE_OCTAVES);
  CHECK(Near(from_lfo.tuned_root, kDefaultTunedRootNote));
  CHECK(Near(from_lfo.note, kDefaultTunedRootNote));

  // It holds wherever in the LFO range the FREQUENCY knob was left, and for a
  // gesture that reaches octave switching the long way round.
  for (int i = 0; i < 5; ++i) {
    RangeSim lfo;
    lfo.Init(-1.0f + 0.5f * static_cast<float>(i), 84.0f,
        SelectorFor(PITCH_RANGE_LOW));
    lfo.Sweep(PITCH_RANGE_LOW, PITCH_RANGE_HIGH);
    lfo.Sweep(PITCH_RANGE_HIGH, PITCH_RANGE_OCTAVES);
    CHECK(Near(lfo.tuned_root, kDefaultTunedRootNote));
  }

  // The default applies to the LFO range only, not to the ordinary ranges near
  // it, and only for as long as the gesture that began there lasts: stopping
  // in a range and reaching for the selector again roots on the pitch that
  // range is now sounding.
  RangeSim lowest_wide;
  lowest_wide.Init(0.0f, 84.0f, SelectorFor(PITCH_RANGE_FIRST_WIDE));
  CHECK(Near(lowest_wide.note, 12.0f));
  lowest_wide.Sweep(PITCH_RANGE_FIRST_WIDE, PITCH_RANGE_OCTAVES);
  CHECK(Near(lowest_wide.tuned_root, 12.0f));

  RangeSim left_lfo;
  left_lfo.Init(0.0f, 84.0f, SelectorFor(PITCH_RANGE_LOW));
  left_lfo.Sweep(PITCH_RANGE_LOW, kWideRangeMiddleC);
  left_lfo.ReleaseSelector();
  CHECK(Near(left_lfo.note, 60.0f));
  left_lfo.Sweep(kWideRangeMiddleC, PITCH_RANGE_OCTAVES);
  CHECK(Near(left_lfo.tuned_root, 60.0f));

  // A long sweep passing THROUGH octave switching on its way somewhere else
  // leaves the same root behind as one that stops there, so the direction of
  // travel never decides the tuning.
  RangeSim past;
  past.Init(0.0f, 60.0f, SelectorFor(kWideRangeMiddleC));
  past.Sweep(kWideRangeMiddleC, PITCH_RANGE_HIGH);
  CHECK(Near(past.tuned_root, 60.0f));
  past.Sweep(PITCH_RANGE_HIGH, PITCH_RANGE_OCTAVES);
  CHECK(Near(past.tuned_root, 60.0f));

  // Leaving octave switching and coming back keeps the root where it was
  // instead of folding the selected octave into it.
  RangeSim returning;
  returning.Init(0.0f, 60.0f, SelectorFor(PITCH_RANGE_OCTAVES));
  returning.octave_offset = 2;
  returning.ReleaseSelector();
  CHECK(Near(returning.note, 84.0f));
  returning.Sweep(PITCH_RANGE_OCTAVES, kWideRangeMiddleC);
  returning.Sweep(kWideRangeMiddleC, PITCH_RANGE_OCTAVES);
  CHECK(Near(returning.tuned_root, 60.0f));
  CHECK(returning.octave_offset == 0);

  // Releasing the selector ends the gesture, so the next one snapshots afresh
  // from wherever the player has since tuned to.
  RangeSim second_gesture;
  second_gesture.Init(0.0f, 60.0f, SelectorFor(PITCH_RANGE_HIGH));
  second_gesture.Sweep(PITCH_RANGE_HIGH, kWideRangeMiddleC + 2);
  second_gesture.ReleaseSelector();
  const float retuned = second_gesture.note;
  CHECK(Near(retuned, 84.0f));
  second_gesture.Sweep(kWideRangeMiddleC + 2, PITCH_RANGE_OCTAVES);
  CHECK(Near(second_gesture.tuned_root, retuned));

  // Fine tuning still anchors on the sounding pitch, octave and all, so
  // stepping up into it from a transposed octave switch does not jump.
  RangeSim fine_from_octaves;
  fine_from_octaves.Init(0.0f, 60.0f, SelectorFor(PITCH_RANGE_OCTAVES));
  fine_from_octaves.octave_offset = -3;
  fine_from_octaves.ReleaseSelector();
  CHECK(Near(fine_from_octaves.note, 24.0f));
  fine_from_octaves.Sweep(PITCH_RANGE_OCTAVES, PITCH_RANGE_PRECISION);
  CHECK(Near(fine_from_octaves.precision_anchor, 24.0f));
  CHECK(Near(fine_from_octaves.note, 24.0f));

#else
  // The same invariants, reachable only through coarse, fine and octave
  // switching. The workflow the option exists for is coarse, then fine, then
  // lock it in, so that is what these replay.

  // Coarse straight into octave switching roots at the pitch left sounding.
  for (int i = 0; i < 5; ++i) {
    const float transposition = -1.0f + 0.5f * static_cast<float>(i);
    RangeSim coarse;
    coarse.Init(transposition, 60.0f, SelectorFor(PITCH_RANGE_HIGH));
    const float sounding = coarse.note;
    CHECK(Near(sounding, 60.0f + transposition * 48.0f));
    coarse.Sweep(PITCH_RANGE_HIGH, PITCH_RANGE_OCTAVES);
    CHECK(coarse.previous_range == PITCH_RANGE_OCTAVES);
    CHECK(Near(coarse.tuned_root, sounding));
  }

  // The whole workflow: coarse, fine tune it, then lock it in. Fine tuning
  // anchors on the coarse pitch, and the root that octave switching keeps is
  // the fine-tuned one, not the coarse one it started from.
  RangeSim workflow;
  workflow.Init(0.0f, 60.0f, SelectorFor(PITCH_RANGE_HIGH));
  CHECK(Near(workflow.note, 60.0f));
  workflow.Sweep(PITCH_RANGE_HIGH, PITCH_RANGE_PRECISION);
  CHECK(Near(workflow.precision_anchor, 60.0f));
  workflow.ReleaseSelector();
  workflow.transposition = 1.0f;               // Fine tune a semitone sharp.
  workflow.Tick(SelectorFor(PITCH_RANGE_PRECISION), false);
  const float fine_tuned = workflow.note;
  CHECK(Near(fine_tuned, 61.0f));
  workflow.Sweep(PITCH_RANGE_PRECISION, PITCH_RANGE_OCTAVES);
  CHECK(Near(workflow.tuned_root, fine_tuned));

  // Leaving octave switching and coming back does not fold the selected octave
  // into the root -- the guard the 2026-08-21 fix added, still holding when the
  // only way out and back is through fine tuning or coarse.
  RangeSim returning;
  returning.Init(0.0f, 60.0f, SelectorFor(PITCH_RANGE_OCTAVES));
  returning.octave_offset = 2;
  returning.ReleaseSelector();
  CHECK(Near(returning.note, 84.0f));
  returning.Sweep(PITCH_RANGE_OCTAVES, PITCH_RANGE_HIGH);
  returning.Sweep(PITCH_RANGE_HIGH, PITCH_RANGE_OCTAVES);
  CHECK(Near(returning.tuned_root, 60.0f));
  CHECK(returning.octave_offset == 0);

  // Fine tuning still anchors on the sounding pitch, octave and all.
  RangeSim fine_from_octaves;
  fine_from_octaves.Init(0.0f, 60.0f, SelectorFor(PITCH_RANGE_OCTAVES));
  fine_from_octaves.octave_offset = -3;
  fine_from_octaves.ReleaseSelector();
  CHECK(Near(fine_from_octaves.note, 24.0f));
  fine_from_octaves.Sweep(PITCH_RANGE_OCTAVES, PITCH_RANGE_PRECISION);
  CHECK(Near(fine_from_octaves.precision_anchor, 24.0f));
  CHECK(Near(fine_from_octaves.note, 24.0f));
#endif

  // The snapshot itself: held for the length of a gesture however far the
  // sounding pitch wanders, transparent outside one, and rooted on the stored
  // root rather than the sounding pitch when the gesture starts in the mode.
  OctaveRootSnapshot snapshot;
  CHECK(!snapshot.editing());
  CHECK(Near(snapshot.Root(72.0f), 72.0f));
  snapshot.Track(true, kWideRangeMiddleC, 60.0f, 48.0f);
  CHECK(snapshot.editing());
  CHECK(Near(snapshot.Root(96.0f), 60.0f));
  snapshot.Track(true, PITCH_RANGE_LAST_WIDE, 96.0f, 48.0f);
  CHECK(Near(snapshot.Root(96.0f), 60.0f));
  snapshot.Track(false, PITCH_RANGE_OCTAVES, 60.0f, 60.0f);
  CHECK(Near(snapshot.Root(96.0f), 96.0f));
  snapshot.Track(true, PITCH_RANGE_OCTAVES, 84.0f, 60.0f);
  CHECK(Near(snapshot.Root(96.0f), 60.0f));
  snapshot.Track(false, PITCH_RANGE_LOW, -20.0f, 48.0f);
  snapshot.Track(true, PITCH_RANGE_LOW, -20.0f, 48.0f);
  CHECK(Near(snapshot.Root(96.0f), kDefaultTunedRootNote));
  snapshot.Reset();
  CHECK(!snapshot.editing());

  // Cycling the options menu past octave switching must not disturb the stored
  // octave. Value 0 of the locked-FREQUENCY-knob option IS octave switching, so
  // reaching any later option steps through it; before this was fixed, stepping
  // ONTO 0 reset the octave to neutral and stepping OFF it re-froze that
  // neutral, so a user cycling to a different option silently lost the octave
  // they had frozen -- audibly, if the selector was in octave switching.
  const int kNumLockedFrequencyPotOptions = 6;

  // Passing through, with the selector NOT in octave switching.
  for (int stored = 0; stored <= 8; ++stored) {
    LockedOptionSim sim;
    sim.Init(static_cast<uint8_t>(stored), 4, 0.0f,
        SelectorFor(PITCH_RANGE_PRECISION));
    sim.StepOption(1, kNumLockedFrequencyPotOptions);   // 4 -> 5
    sim.StepOption(1, kNumLockedFrequencyPotOptions);   // 5 -> 0, through it
    sim.StepOption(1, kNumLockedFrequencyPotOptions);   // 0 -> 1, and out
    CHECK(sim.option == 1);
    CHECK(sim.locked_octave == stored);
  }

  // Passing through WHILE in octave switching: the sounding octave must not
  // move either, since the knob was never touched.
  for (int stored = 0; stored <= 8; ++stored) {
    LockedOptionSim sim;
    sim.Init(static_cast<uint8_t>(stored), 4, 0.0f,
        SelectorFor(PITCH_RANGE_OCTAVES));
    const int before = sim.SoundingOctaveOffset();
    CHECK(before == stored - 4);
    sim.StepOption(1, kNumLockedFrequencyPotOptions);
    sim.StepOption(1, kNumLockedFrequencyPotOptions);   // now at 0
    CHECK(sim.SoundingOctaveOffset() == stored - 4);
    sim.StepOption(1, kNumLockedFrequencyPotOptions);   // and out again
    CHECK(sim.locked_octave == stored);
    CHECK(sim.SoundingOctaveOffset() == stored - 4);
  }

  // The same walked the other way round the menu.
  for (int stored = 0; stored <= 8; ++stored) {
    LockedOptionSim sim;
    sim.Init(static_cast<uint8_t>(stored), 1, 0.0f,
        SelectorFor(PITCH_RANGE_OCTAVES));
    sim.StepOption(-1, kNumLockedFrequencyPotOptions);  // 1 -> 0
    sim.StepOption(-1, kNumLockedFrequencyPotOptions);  // 0 -> 5, out again
    CHECK(sim.locked_octave == stored);
  }

  // What SHOULD change it: resting on octave switching and turning the knob.
  {
    LockedOptionSim sim;
    sim.Init(4, 1, 0.0f, SelectorFor(PITCH_RANGE_OCTAVES));
    sim.StepOption(-1, kNumLockedFrequencyPotOptions);  // rest at 0
    CHECK(sim.option == 0);
    sim.TurnKnob(1.0f);                                 // sweep to the top
    const int moved = sim.SoundingOctaveOffset();
    CHECK(moved > 0);
    sim.StepOption(1, kNumLockedFrequencyPotOptions);   // leave, freezing it
    CHECK(static_cast<int>(sim.locked_octave) - 4 == moved);
  }

  std::printf("pitch_range_test: %d checks passed\n", checks);
  return 0;
}
