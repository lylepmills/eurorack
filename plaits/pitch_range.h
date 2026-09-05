// Copyright 2026 Rubato Audio.
//
// Pure pitch-range math shared by the hardware UI and its host test. The
// selector keeps Plaits' eight ordinary +/-7-semitone octave ranges, followed
// by octave switching, precision tuning, and a full-range coarse sweep.
//
// That last one is PITCH_RANGE_HIGH, and the name misleads: it is not a
// high-frequency range but `60 + transposition * 48`, i.e. 12..108 on one knob
// -- the position players reach for as COARSE, and the far-clockwise one a
// module ships pointing at. Prefer "coarse" in user-facing copy.

#ifndef PLAITS_PITCH_RANGE_H_
#define PLAITS_PITCH_RANGE_H_

#include <cmath>

#include "stmlib/stmlib.h"
#include "plaits/build_config.h"

namespace plaits {

enum PitchRange {
  PITCH_RANGE_LOW = 0,
  PITCH_RANGE_FIRST_WIDE = 1,
  PITCH_RANGE_LAST_WIDE = 8,
  PITCH_RANGE_OCTAVES = 9,
  PITCH_RANGE_PRECISION = 10,
  PITCH_RANGE_HIGH = 11,
  PITCH_RANGE_COUNT = 12
};

// The root octave switching falls back to: middle C, which is also the tuned
// root a module ships with and the one it returns to after a factory reset.
const float kDefaultTunedRootNote = 60.0f;

// The simplified selector keeps the three most clockwise positions -- octave
// switching, fine tuning, coarse -- and relies on their being a contiguous run
// at the top of the enum, so a third of the knob maps onto each. The enum values
// themselves never change between the two layouts: only the selector mapping
// does. That is deliberate, because range indices are used elsewhere as a
// vocabulary rather than as positions (the UI_MODE_DISPLAY_OCTAVE LED patterns,
// and the octave shortcut's locked_octave_ == 8 ? PITCH_RANGE_HIGH sentinel),
// and because it leaves the saved tuned root, the locked octave, and the
// octave-root snapshot identical across both builds.
// STATIC_ASSERT, not static_assert: the ARM firmware builds as C++98.
STATIC_ASSERT(
    PITCH_RANGE_PRECISION == PITCH_RANGE_OCTAVES + 1
        && PITCH_RANGE_HIGH == PITCH_RANGE_OCTAVES + 2,
    octave_precision_and_coarse_must_stay_contiguous_in_that_order);

const int kSimplifiedFirstRange = PITCH_RANGE_OCTAVES;
const int kSimplifiedRangeCount = 3;

inline int PitchRangeFromControl(float value) {
#if PLAITS_BUILD_SIMPLIFIED_PITCH_RANGES
  int index = static_cast<int>(
      value * static_cast<float>(kSimplifiedRangeCount));
  if (index < 0) index = 0;
  if (index >= kSimplifiedRangeCount) index = kSimplifiedRangeCount - 1;
  return kSimplifiedFirstRange + index;
#else
  int range = static_cast<int>(value * static_cast<float>(PITCH_RANGE_COUNT));
  if (range < PITCH_RANGE_LOW) range = PITCH_RANGE_LOW;
  if (range >= PITCH_RANGE_COUNT) range = PITCH_RANGE_COUNT - 1;
  return range;
#endif
}

inline float WideRangeNote(int range, float transposition) {
  return transposition * 7.0f + static_cast<float>(range) * 12.0f;
}

inline float PrecisionRangeNote(float anchor_note, float transposition) {
  // transposition is a caught-up fine control in [-1, +1].
  return anchor_note + transposition;
}

// The original Plaits octave-switching workflow let the player hold the right
// button and turn FREQUENCY to move the root over the same +/-7-semitone span
// as an ordinary pitch range. Palette's dedicated precision range superseded
// the old absolute fine-tune byte, but the direct gesture remains useful. Keep
// it relative to the persisted root so entering the gesture never discards a
// tuning established through Coarse -> Fine tuning.
inline float RootRetuneNote(float anchor_note, float transposition) {
  const float note = anchor_note + transposition * 7.0f;
  // tuned_root_q8 is the persistent source of truth. Clamp the live value to
  // the same representable interval so repeated gestures cannot sound one note
  // and restore a different, saturated note after a power cycle.
  const float minimum = -128.0f;
  const float maximum = 32767.0f / 256.0f;
  return note < minimum ? minimum : note > maximum ? maximum : note;
}

// Chooses the manual pitch that becomes the octave-switching root.
//
// The range selector is a hidden parameter on HARMONICS, so it sweeps: to
// reach octave switching the selection is dragged across every range in
// between, and each one rewrites the sounding note on the way past. The pitch
// present when the selection LANDS on octave switching is therefore not the
// pitch the player was hearing when they reached for the selector. Approaching
// from below crosses the eight ordinary ranges and arrives carrying the last
// of them (note 96, C7), rooting the mode three octaves above where the player
// left off and putting five of its nine positions at or above C7; approaching
// from above, down through Fine tuning, arrives carrying the player's own
// pitch. Nothing on the panel distinguishes the two, and the wrong root is
// saved, so it survives a power cycle and outlives the ranges that caused it.
//
// Snapshotting the manual pitch when the gesture BEGINS, and holding that
// snapshot for as long as it lasts, roots the mode at the pitch the player
// left behind whichever way they turn the selector.
class OctaveRootSnapshot {
 public:
  OctaveRootSnapshot() : editing_(false), note_(0.0f) { }

  inline void Reset() {
    editing_ = false;
    note_ = 0.0f;
  }

  // Call once per control tick, BEFORE this tick's range change is handled.
  // |editing| is true while the range selector is being turned; |range|,
  // |note| and |tuned_root| are the range, the sounding manual pitch and the
  // stored root as they stood at the end of the previous tick.
  inline void Track(bool editing, int range, float note, float tuned_root) {
    if (editing && !editing_) {
      if (range == PITCH_RANGE_OCTAVES) {
        // Starting from octave switching itself, the sounding pitch already
        // carries the selected octave. Keeping the root instead is what stops
        // leaving and returning from walking the tuning up or down.
        note_ = tuned_root;
      } else if (range == PITCH_RANGE_LOW) {
        // The LFO range's pitch is sub-audio by design, so rooting there would
        // put all nine octave positions at or below the bottom of hearing --
        // the same trap as arriving three octaves sharp, pointing the other
        // way. Someone leaving the LFO range for octave switching is asking
        // for pitches, so hand them the default root rather than a rate.
        note_ = kDefaultTunedRootNote;
      } else {
        note_ = note;
      }
    }
    editing_ = editing;
  }

  // The root for a change into octave switching happening this tick. The
  // selector cannot move outside a gesture, so the sounding pitch is only a
  // fallback for a caller that changes the range some other way.
  inline float Root(float note) const {
    return editing_ ? note_ : note;
  }

  inline bool editing() const {
    return editing_;
  }

 private:
  bool editing_;
  float note_;
};

// The endpoint-weighted catch-up used by Plaits' ordinary hidden parameters,
// packaged for controls whose meaning changes when the pitch range changes.
// The virtual value starts wherever the new role needs it (normally noon), so
// entering that role does not jump. Any deliberate physical movement changes
// it immediately, while the skew converges virtual and physical values at the
// end of the knob's travel; after they meet, tracking is direct.
class EndpointCatchUp {
 public:
  EndpointCatchUp() : initialized_(false), catching_up_(false) { }

  inline void Reset() {
    initialized_ = false;
    catching_up_ = false;
  }

  inline void Init(float value, float physical_value) {
    value_ = Clamp(value);
    previous_physical_value_ = Clamp(physical_value);
    initialized_ = true;
    catching_up_ = true;
  }

  inline float Process(float physical_value) {
    physical_value = Clamp(physical_value);
    if (!initialized_) {
      Init(0.5f, physical_value);
    }

    if (!catching_up_) {
      value_ = physical_value;
      previous_physical_value_ = physical_value;
      return value_;
    }

    if (fabsf(physical_value - previous_physical_value_) > 0.005f) {
      const float delta = physical_value - previous_physical_value_;
      float skew_ratio = delta > 0.0f
          ? (1.001f - value_) / (1.001f - previous_physical_value_)
          : (0.001f + value_) / (0.001f + previous_physical_value_);
      if (skew_ratio < 0.1f) skew_ratio = 0.1f;
      if (skew_ratio > 10.0f) skew_ratio = 10.0f;

      value_ += skew_ratio * delta;
      value_ = Clamp(value_);
      previous_physical_value_ = physical_value;

      if (fabsf(value_ - physical_value) < 0.005f) {
        catching_up_ = false;
      }
    }
    return value_;
  }

  inline bool catching_up() const {
    return catching_up_;
  }

 private:
  static inline float Clamp(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
  }

  float value_;
  float previous_physical_value_;
  bool initialized_;
  bool catching_up_;
};

// Coalesces a stream of changing values into one save request after the value
// has remained unchanged for a caller-selected number of control-rate ticks.
// Returning to the last saved value cancels the pending write.
class DeferredValueSave {
 public:
  DeferredValueSave() : delay_(0), countdown_(0), initialized_(false) { }

  inline void Init(int16_t saved_value, uint16_t delay) {
    saved_value_ = saved_value;
    pending_value_ = saved_value;
    delay_ = delay;
    countdown_ = 0;
    initialized_ = true;
  }

  inline bool Process(int16_t value) {
    if (!initialized_) {
      Init(value, 0);
      return false;
    }

    if (value != pending_value_) {
      pending_value_ = value;
      countdown_ = delay_;
      return false;
    }

    if (pending_value_ == saved_value_) {
      countdown_ = 0;
      return false;
    }

    if (countdown_) {
      --countdown_;
      if (countdown_) {
        return false;
      }
    }

    saved_value_ = pending_value_;
    return true;
  }

  inline void MarkSaved(int16_t value) {
    saved_value_ = value;
    pending_value_ = value;
    countdown_ = 0;
    initialized_ = true;
  }

 private:
  int16_t saved_value_;
  int16_t pending_value_;
  uint16_t delay_;
  uint16_t countdown_;
  bool initialized_;
};

inline int16_t EncodeTunedRoot(float note) {
  float scaled = note * 256.0f;
  if (scaled <= -32768.0f) return static_cast<int16_t>(-32768);
  if (scaled >= 32767.0f) return static_cast<int16_t>(32767);
  const float rounded = scaled + (scaled >= 0.0f ? 0.5f : -0.5f);
  return static_cast<int16_t>(rounded);
}

inline float DecodeTunedRoot(int16_t note_q8) {
  return static_cast<float>(note_q8) / 256.0f;
}

}  // namespace plaits

#endif  // PLAITS_PITCH_RANGE_H_
