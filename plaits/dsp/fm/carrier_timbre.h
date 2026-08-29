// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
// Give DX7 algorithm 32 a useful TIMBRE response. That algorithm contains no
// modulators, so the stock brightness mapping is structurally a no-op. Tilt the
// active carriers by their frequency positions instead: counter-clockwise
// favours lower carriers, clockwise favours higher carriers, and noon remains
// the original patch exactly.
#ifndef PLAITS_DSP_FM_CARRIER_TIMBRE_H_
#define PLAITS_DSP_FM_CARRIER_TIMBRE_H_

#include <stdint.h>

#include "plaits/dsp/fm/patch.h"

namespace plaits { namespace fm {

// The shared FM Voice template is also instantiated for the stock four-op
// engine. Keep that instance a compile-time no-op so the six-carrier feature
// costs no flash or state outside SixOpEngine.
template<int num_operators>
class CarrierTimbre {
 public:
  void Init() { }
#ifdef TEST
  void set_enabled(bool enabled) { }
#endif
  void SetPatch(const Patch& patch, const float* ratios) { }
  float LevelOffset(int i, float timbre) const { return 0.0f; }
};

template<>
class CarrierTimbre<6> {
 public:
  void Init() {
    active_ = false;
#ifdef TEST
    enabled_ = true;
#endif
  }

#ifdef TEST
  // Host regressions can render the pre-feature reference without carrying a
  // second copy of the FM voice. This member and branch do not exist on target.
  void set_enabled(bool enabled) { enabled_ = enabled; }
#endif

  void SetPatch(const Patch& patch, const float* ratios) {
    active_ = patch.algorithm == 31;
    for (int i = 0; i < 6; ++i) {
      tilt_[i] = 0;
    }
    if (!active_) return;

    // Rank every audible carrier against every other carrier at MIDI 48. The
    // pairwise scores sum to zero, so the knob tilts rather than merely changing
    // overall level. Only exactly matching frequencies share a rank; detuned
    // unisons are distinct carriers and therefore remain useful tilt targets.
    // This rank form is much smaller on Cortex-M4 than the
    // former log-frequency mean/span normalization, while preserving the same
    // low-to-high ordering and exact patch at noon.
    for (int i = 0; i < 6; ++i) {
      if (!patch.op[i].level) continue;
      const float a = Position(ratios[i]);
      for (int j = i + 1; j < 6; ++j) {
        if (!patch.op[j].level) continue;
        const float b = Position(ratios[j]);
        if (a > b) {
          ++tilt_[i];
          --tilt_[j];
        } else if (b > a) {
          --tilt_[i];
          ++tilt_[j];
        }
      }
    }
    active_ = false;
    for (int i = 0; i < 6; ++i) active_ |= tilt_[i] != 0;
  }

  float LevelOffset(int i, float timbre) const {
#ifdef TEST
    if (!enabled_) return 0.0f;
#endif
    // Six distinct carriers score -5,-3,-1,+1,+3,+5. A factor of 4.8 makes
    // the endpoints +/-12 DX level units (about +/-9 dB), matching the
    // qualified tilt's +/-18 dB low-to-high difference at either knob end.
    return active_ ? (timbre - 0.5f) * 4.8f * float(tilt_[i]) : 0.0f;
  }

 private:
  static float Position(float ratio) {
    // ratios_ stores fixed-frequency operators as negative Hz and ratio-mode
    // operators as positive multiples of the played note. Compare both at C3.
    return ratio < 0.0f ? -ratio : ratio * 130.8128f;
  }
  bool active_;
#ifdef TEST
  bool enabled_;
#endif
  int8_t tilt_[6];
};

} }  // namespace plaits::fm
#endif
