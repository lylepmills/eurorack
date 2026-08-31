// Copyright 2026 Combust.
// SPDX-License-Identifier: MIT
//
// ARS Necklace — a point-process synthesizer. Gaps between events are drawn
// from a rigidity ladder of spacing statistics (HARMONICS): sigma-delta
// rigid, jittered lattice, quasiperiodic rotation (three-distance), GUE- and
// GOE-like surmise gaps, Poisson (with a dead-time TIMBRE), and clustered.
// Zones 4-6 reproduce nearest-neighbor spacing distributions, NOT spectral
// rigidity; the audible low-frequency rate flutter they carry (absent in
// zones 1-3) is number variance made audible. Do not "fix" it.
//
// v3 renderer (chiptune-expert review, 2026-08-15): events are no longer
// averaged into a decaying contour — they PING two complex-one-pole
// resonators. Resonator A is pitch-quantized per event by bucketing the
// realized gap onto {1, 3/2, 2, 3} of f0 (the gap melody: rigid zones give
// ostinati, three-distance gives a three-note motif, Poisson gives free
// melody). Resonator B rings at the wrap formant m x f0. Velocity comes
// from gap size, with an accent on every mod-m wrap (the necklace downbeat),
// and feeds both the pings and the AUX gates. Stored loops are normalized
// to exact bar length so TRIG-replayed riffs phrase-lock.
//
// Deterministic per seed: TRIG replays the stored realization from gap 0.
// Holding TRIG ~1.5 s draws a new seed. Zone and TIMBRE changes re-push the
// SAME stored uniforms through the new transform. macro: CCW tightens the
// loop (8..64 gaps), CW frays it — each gap is replaced by a fresh ensemble
// draw with rising probability, reaching full FREERUN at the end.

#ifndef PLAITS_DSP_ENGINE2_BUBBLETIME_ENGINE_H_
#define PLAITS_DSP_ENGINE2_BUBBLETIME_ENGINE_H_

#ifndef PLAITS_STEREO_BUBBLETIME
#define PLAITS_STEREO_BUBBLETIME 1
#endif

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/chords/chord_bank.h"

namespace plaits {

class BubbleTimeEngine : public Engine {
 public:
  BubbleTimeEngine() { }
  ~BubbleTimeEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void RestartPlayback();
  void LoadUserData(const uint8_t* user_data) { }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_BUBBLETIME; }

 private:
  static const int kLoopGaps = 64;

  void Reseed();
  void RefillUniforms();
  void RegenerateGaps(int zone, float timbre);
  void StartIncrementalRegen(int zone, float timbre);
  void UpdateLoopNorm();
  float NextGap(int zone, float timbre, float mean_gap_samples);

  // Stored uniform randoms (the realization's identity) and the gap ring
  // derived from them under the current zone/TIMBRE transform.
  uint32_t seed_;
  float uniforms_[kLoopGaps];
  float gap_ring_[kLoopGaps];
  int gap_pos_;
  int loop_length_;
  int normed_loop_length_;
  float loop_norm_;      // scales the first L gaps to sum exactly L
  float fray_;           // probability a gap is replaced by an ensemble draw
  uint32_t freerun_state_;

  // Procedural zone state (zones 0-2).
  float sd_accumulator_;
  float rotation_phase_;
  float rotation_countdown_;

  // Event scheduling.
  float event_countdown_;
  float cumulative_position_;    // running sum of gaps, mod m (ring 1)
  float cumulative_position_b_;  // the same sum, mod m' (ring 2, booleans)
  float samples_since_event_;
  float last_lattice_u_;

  // The gap melody's chord tones come from the firmware ChordBank; the
  // chord follows the wrap modulus, so MORPH-low picks pattern AND harmony.
  ChordBank chords_;

  // Ping resonators (complex one-pole): A = gap melody, B = base tone.
  float a_re_, a_im_, a_c_, a_s_;
  float b_re_, b_im_, b_c_, b_s_;
  float a_right_re_, a_right_im_;
  float b_right_re_, b_right_im_;
  float resonator_r_;
  // Strike excitation ramp: injection spread over ~1 ms so OUT carries a
  // percussive attack, not a wideband click.
  int excite_remaining_;
  float excite_amp_;
  float excite_left_;
  float excite_right_;

  // Gates.
  int gate_remaining_;
  float gate_level_;
  float dc_aux_;

  // Hysteresis, regen, reseed-gesture state.
  int zone_;
  int modulus_index_;
  int base_interval_;
  int cached_zone_;
  float cached_timbre_;
  bool ring_valid_;
  float regen_k_;
  int regen_pending_;
  int regen_write_;
  int hold_blocks_;
  bool reseed_armed_;

  DISALLOW_COPY_AND_ASSIGN(BubbleTimeEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_BUBBLETIME_ENGINE_H_
