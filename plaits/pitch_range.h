// Copyright 2026 Rubato Audio.
//
// Pure pitch-range math shared by the hardware UI and its host test. The
// selector keeps Plaits' eight ordinary +/-7-semitone octave ranges, followed
// by octave switching, precision tuning, and the high-frequency range.

#ifndef PLAITS_PITCH_RANGE_H_
#define PLAITS_PITCH_RANGE_H_

#include <cmath>

#include "stmlib/stmlib.h"

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

inline int PitchRangeFromControl(float value) {
  int range = static_cast<int>(value * static_cast<float>(PITCH_RANGE_COUNT));
  if (range < PITCH_RANGE_LOW) range = PITCH_RANGE_LOW;
  if (range >= PITCH_RANGE_COUNT) range = PITCH_RANGE_COUNT - 1;
  return range;
}

inline float WideRangeNote(int range, float transposition) {
  return transposition * 7.0f + static_cast<float>(range) * 12.0f;
}

inline float PrecisionRangeNote(float anchor_note, float transposition) {
  // transposition is a caught-up fine control in [-1, +1].
  return anchor_note + transposition;
}

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
