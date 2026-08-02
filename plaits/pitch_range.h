// Copyright 2026 Rubato Audio.
//
// Pure pitch-range math shared by the hardware UI and its host test. The
// selector keeps Plaits' eight ordinary +/-7-semitone octave ranges, then adds
// a precision stop before octave switching and the high-frequency range.

#ifndef PLAITS_PITCH_RANGE_H_
#define PLAITS_PITCH_RANGE_H_

#include "stmlib/stmlib.h"

namespace plaits {

enum PitchRange {
  PITCH_RANGE_LOW = 0,
  PITCH_RANGE_FIRST_WIDE = 1,
  PITCH_RANGE_LAST_WIDE = 8,
  PITCH_RANGE_PRECISION = 9,
  PITCH_RANGE_OCTAVES = 10,
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
  // transposition is the filtered FREQUENCY pot in [-1, +1].
  return anchor_note + transposition;
}

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
