// Copyright 2026 Rubato Audio.
//
// Turns one jack's normalization-probe verdicts into its patched state, slowly
// in one direction. A window whose probe sequence came back (the jack is still
// normalled) counts as unpatched at once. A jack only counts as patched after
// kPatchedStreak mismatched windows in a row, so a plug's contacts bouncing on
// insertion, or a stray bad window on an empty jack, can no longer switch the
// module over to reading the jack for a window or two. An empty jack carries
// the probe's own random square wave, and the unpatched behaviours (the
// attenuverter modes among them) drop out while it is read.
//
// The asymmetry and the count are Beads' (beads/normalization_detector.cc).
// A probe sequence coming back by chance through a patched cable is unlikely
// enough that the unpatched verdict needs no such delay.

#ifndef PLAITS_PATCH_DETECTOR_H_
#define PLAITS_PATCH_DETECTOR_H_

namespace plaits {

class PatchDetector {
 public:
  // Plaits decides once per 32-step probe window at 1 kHz, so 8 windows is
  // 256 ms from the plug going in to the jack counting as patched -- the same
  // as Beads, whose windows are 64 steps at 2 kHz.
  static const int kPatchedStreak = 8;

  PatchDetector() { }
  ~PatchDetector() { }

  // Starts the streak full, so the first window after power-up decides on its
  // own, as before: a cable already patched at boot is not delayed.
  void Init() {
    streak_ = kPatchedStreak;
  }

  // mismatched: this window's probe sequence did not come back. Returns
  // whether the jack now counts as patched.
  bool Process(bool mismatched) {
    if (!mismatched) {
      streak_ = 0;
    } else if (streak_ < kPatchedStreak) {
      ++streak_;
    }
    return patched();
  }

  bool patched() const {
    return streak_ >= kPatchedStreak;
  }

 private:
  int streak_;
};

}  // namespace plaits

#endif  // PLAITS_PATCH_DETECTOR_H_
