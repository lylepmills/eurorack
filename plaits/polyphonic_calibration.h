// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// Compact, independent calibration for repurposing MODEL, HARMONICS and LEVEL
// as three additional pitch-CV inputs. The ordinary per-channel calibration in
// Settings remains authoritative whenever the jacks retain their stock roles.

#ifndef PLAITS_POLYPHONIC_CALIBRATION_H_
#define PLAITS_POLYPHONIC_CALIBRATION_H_

#include <stdint.h>

#include "stmlib/stmlib.h"

namespace plaits {

enum PolyphonicPitchInput {
  POLYPHONIC_PITCH_INPUT_MODEL,
  POLYPHONIC_PITCH_INPUT_HARMONICS,
  POLYPHONIC_PITCH_INPUT_LEVEL,
  POLYPHONIC_PITCH_INPUT_LAST
};

// This structure deliberately occupies the 16 bytes that Mutable Instruments
// reserved as padding at the end of PersistentData. Keeping that outer chunk's
// tag and size unchanged is what lets stock and prototype firmware preserve
// one another's calibration records.
struct PolyphonicPitchCalibrationData {
  enum { kSignature = 0x34564350 };  // "PCV4" in little-endian flash.

  uint32_t signature;
  int16_t c1[POLYPHONIC_PITCH_INPUT_LAST];
  int16_t c3[POLYPHONIC_PITCH_INPUT_LAST];

  static inline bool PairIsValid(int16_t low, int16_t high) {
    // MODEL, HARMONICS and LEVEL all use a -0.33 V/V input stage, like
    // V/OCT. Two volts should therefore move the normalized ADC reading by
    // about -0.4 full-scale, with the same broad acceptance window as the
    // stock V/OCT routine (-0.6 .. -0.2).
    const int32_t delta = static_cast<int32_t>(high) - low;
    return delta > -19661 && delta < -6554;
  }

  inline bool valid() const {
    if (signature != kSignature) {
      return false;
    }
    for (int i = 0; i < POLYPHONIC_PITCH_INPUT_LAST; ++i) {
      if (!PairIsValid(c1[i], c3[i])) {
        return false;
      }
    }
    return true;
  }

  inline void Clear() {
    signature = 0;
    for (int i = 0; i < POLYPHONIC_PITCH_INPUT_LAST; ++i) {
      c1[i] = 0;
      c3[i] = 0;
    }
  }

  // Commit all three channels transactionally. A bad pair leaves the previous
  // profile untouched, just as a bad V/OCT high point leaves its saved slope
  // untouched.
  inline bool Store(const int16_t* low, const int16_t* high) {
    for (int i = 0; i < POLYPHONIC_PITCH_INPUT_LAST; ++i) {
      if (!PairIsValid(low[i], high[i])) {
        return false;
      }
    }
    signature = 0;
    for (int i = 0; i < POLYPHONIC_PITCH_INPUT_LAST; ++i) {
      c1[i] = low[i];
      c3[i] = high[i];
    }
    signature = kSignature;
    return true;
  }

  // Return semitones, matching ChannelCalibrationData::Transform(V/OCT):
  // the captured low point is 12 semitones and the high point is 36.
  inline float Transform(PolyphonicPitchInput input, int16_t raw) const {
    if (!valid() || input < 0 || input >= POLYPHONIC_PITCH_INPUT_LAST) {
      return 0.0f;
    }
    const float delta = static_cast<float>(c3[input] - c1[input]);
    return 12.0f + 24.0f *
        static_cast<float>(raw - c1[input]) / delta;
  }
};

STATIC_ASSERT(
    sizeof(PolyphonicPitchCalibrationData) == 16,
    polyphonic_pitch_calibration_must_fit_reserved_padding);

}  // namespace plaits

#endif  // PLAITS_POLYPHONIC_CALIBRATION_H_
