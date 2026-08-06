// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_DSP_ENGINE2_HELIX_ENGINE_H_
#define PLAITS_DSP_ENGINE2_HELIX_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/chords/chord_bank.h"

namespace plaits {

const int kHelixOctaves = 4;  // 16 voices -- the ship config. QEMU-estimated 80%
                              // of budget, hardware-verified clean after the
                              // staggered setup (flat per-block load) and the
                              // seamless Shepard wrap landed; the earlier 4-octave
                              // pops came from those two bugs, not headroom. 3
                              // octaves runs at 62-75% (fallback with margin);
                              // 6 octaves measures ~95% on the engine bracket
                              // alone and overruns once ISR overhead lands on top
                              // (hardware-validated, ~1% over the deadline).

class HelixEngine : public Engine {
 public:
  HelixEngine() { }
  ~HelixEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void LoadUserData(const uint8_t* user_data) { }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);
  bool stereo_capable() const { return true; }  // OUT/AUX render as a true L/R pair
  void HardSync() {
    for (int i = 0; i < kHelixOctaves * kChordNumNotes; ++i) {
      osc_x_[i] = 1.0f;
      osc_y_[i] = 0.0f;
    }
  }

 private:
  ChordBank chords_;
  // Each voice is a rotating unit vector rather than a phase into a table: see
  // the note above Render(). x is the cosine component, y the sine we output.
  float osc_x_[kHelixOctaves * kChordNumNotes];
  float osc_y_[kHelixOctaves * kChordNumNotes];
  // Per-voice coefficients, refreshed HALF the palette per block (staggered,
  // setup_phase_ alternates which half): the glide moves the helix ~3.5e-4
  // octaves per block, so a 2 kHz per-voice refresh is inaudible -- and
  // staggering keeps every block's setup cost equal, where a full refresh on
  // alternate blocks left the heavy block as heavy as no optimization at all
  // (the audio deadline is per block, not on average).
  float gain_[kHelixOctaves * kChordNumNotes];
  float cos_w_[kHelixOctaves * kChordNumNotes];
  float sin_w_[kHelixOctaves * kChordNumNotes];
  float pan_l_[kHelixOctaves * kChordNumNotes];
  float pan_r_[kHelixOctaves * kChordNumNotes];
  float inv_w_;
  // Staggered refresh: half the palette's coefficients per block, so every
  // voice refreshes at 2 kHz but no single block carries the whole setup --
  // the audio deadline is per BLOCK, and the earlier all-or-nothing scheme
  // left the setup block as heavy as before the optimization.
  int setup_phase_;
  float weight_half_[2];
  float shift_;                                  // continuous octave glide, [0,1)
  DISALLOW_COPY_AND_ASSIGN(HelixEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_HELIX_ENGINE_H_
