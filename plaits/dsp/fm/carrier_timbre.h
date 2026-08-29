// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
// Give DX7 algorithm 32 a useful TIMBRE response. That algorithm contains no
// modulators, so the stock brightness mapping is structurally a no-op. Tilt the
// active carriers by their frequency positions instead: counter-clockwise
// favours lower carriers, clockwise favours higher carriers, and noon remains
// the original patch exactly.
#ifndef PLAITS_DSP_FM_CARRIER_TIMBRE_H_
#define PLAITS_DSP_FM_CARRIER_TIMBRE_H_

#include <algorithm>
#include <cmath>
#include "plaits/dsp/fm/dx_units.h"

namespace plaits { namespace fm {

class CarrierTimbre {
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

  void SetPatch(const Patch& patch) {
    active_ = patch.algorithm == 31;
    int indices[6];
    float positions[6];
    int count = 0;
    float mean = 0.0f;
    for (int i = 0; i < 6; ++i) {
      tilt_[i] = 0.0f;
      headroom_[i] = 127.0f - OperatorLevel(patch.op[i].level);
      if (!active_ || !patch.op[i].level) continue;
      const Patch::Operator& op = patch.op[i];
      // Ignore tiny detuning: unison carriers should not receive a full-range
      // spectral split. Mixed fixed/ratio operators are compared at MIDI 48;
      // freeze this ordering at patch load so playing notes never flips it.
      const float position = op.mode == 0
          ? 84.375f + lut_coarse[op.coarse] +
              FineSemitones(op.fine)
          : float((op.coarse & 3) * 100 + op.fine) * 0.39864f;
      indices[count] = i;
      positions[i] = position;
      mean += position;
      ++count;
    }
    if (count < 2) { active_ = false; return; }
    mean /= count;
    float span = 0.0f;
    for (int j = 0; j < count; ++j) {
      const int i = indices[j];
      tilt_[i] = positions[i] - mean;
      span = std::max(span, fabsf(tilt_[i]));
    }
    for (int j = 0; j < count; ++j) {
      const int i = indices[j];
      tilt_[i] = span > 0.001f ? tilt_[i] / span : 0.0f;
    }
  }

  float Apply(int i, float timbre, float amplitude) const {
#ifdef TEST
    if (!enabled_) return amplitude;
#endif
    if (!active_ || timbre == 0.5f) return amplitude;
    const float delta = std::min(
        (timbre - 0.5f) * 48.0f * tilt_[i], headroom_[i]);
    if (delta == 0.0f) return amplitude;
    // Runtime gain, NOT repeated patch edits: preserves envelope state/timing
    // and uses the FM operator's existing block interpolation for smoothness.
    // +/-24 DX log-level units = approximately +/-18 dB before headroom caps.
    return amplitude * Pow2Fast<2>(delta * 0.125f);
  }

 private:
  static float FineSemitones(int fine) {
    // log2(1+x), x in [0,.99], using the atanh series. Error < .03 cents.
    // Avoid libm log2f: the firmware's minimal C runtime has no errno support.
    const float x = 0.01f * fine;
    const float y = x / (2.0f + x);
    const float y2 = y * y;
    return 34.62468098f * y * (1.0f + y2 *
        (1.0f / 3.0f + y2 * (1.0f / 5.0f + y2 / 7.0f)));
  }
  bool active_;
#ifdef TEST
  bool enabled_;
#endif
  float tilt_[6];
  float headroom_[6];
};

} }  // namespace plaits::fm
#endif
