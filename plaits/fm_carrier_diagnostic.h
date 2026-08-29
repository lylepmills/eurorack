// Copyright 2026 Rubato Audio. SPDX-License-Identifier: MIT
// Autonomous on-target qualification for algorithm-32 Carrier Tilt.

#ifndef PLAITS_FM_CARRIER_DIAGNOSTIC_H_
#define PLAITS_FM_CARRIER_DIAGNOSTIC_H_

#include <stddef.h>
#include <stdint.h>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"

#if !PLAITS_CPU_PROBE
#error "The FM carrier diagnostic requires PLAITS_CPU_PROBE"
#endif

namespace plaits {

class FmCarrierDiagnostic {
 public:
  enum Failure {
    FAILURE_CPU_HEADROOM = 1 << 0,
    FAILURE_DEADLINE = 1 << 1,
    FAILURE_SILENCE = 1 << 2
  };

  static const int kTimbreStates = 5;
  static const int kPitchStates = 4;
  static const int kArticulationStates = 2;
  static const int kWarmupBlocks = 8;
  // 24 measured blocks after an 8-block warmup gives 32 blocks/state.
  // Across the 71-patch qualification corpus this is 22.72 seconds: long
  // enough to catch sustained CPU pressure without making the no-touch test
  // feel stalled.
  static const int kMeasuredBlocks = 24;
  static const int kBlocksPerState = kWarmupBlocks + kMeasuredBlocks;
  // ChannelPostProcessor deliberately biases exact zero to one integer count.
  // Anything above that proves the synthesis path produced a real sample.
  // Audibility is a catalog-quality concern, not a hardware render failure.
  static const int kNumericalSilenceFloor = 1;
  static const int kAuditionSeconds = 4;
  static const int kAuditionScenes = 6;

  FmCarrierDiagnostic() { }

  void Init() {
    engine_ = 0;
    patch_index_ = 0;
    timbre_state_ = 0;
    pitch_state_ = 0;
    articulation_state_ = 0;
    block_ = 0;
    peak_usage_ = 0.0f;
    over_ninety_ = 0;
    missed_deadline_ = 0;
    silent_centres_ = 0;
    state_has_signal_ = false;
    patch_has_centre_signal_ = false;
    finished_ = false;
    passed_ = false;
    audition_block_ = 0;
    stress_blocks_ = 0;
    total_stress_blocks_ = 0;
    for (int engine = 0; engine < PLAITS_ENGINE_COUNT; ++engine) {
      total_stress_blocks_ += PatchCount(engine) * kTimbreStates *
          kPitchStates * kArticulationStates * kBlocksPerState;
    }
  }

  bool finished() const { return finished_; }
  bool passed() const { return passed_; }
  float peak_usage() const { return peak_usage_; }
  uint32_t over_ninety() const { return over_ninety_; }
  uint32_t missed_deadline() const { return missed_deadline_; }
  uint32_t silent_centres() const { return silent_centres_; }
  uint8_t failure_mask() const {
    uint8_t mask = 0;
    if (peak_usage_ >= 0.9f) mask |= FAILURE_CPU_HEADROOM;
    if (missed_deadline_) mask |= FAILURE_DEADLINE;
    if (silent_centres_) mask |= FAILURE_SILENCE;
    return mask;
  }
  float progress() const {
    return total_stress_blocks_
        ? static_cast<float>(stress_blocks_) /
            static_cast<float>(total_stress_blocks_)
        : 0.0f;
  }

  void Prepare(Patch* patch, Modulations* modulations, size_t size) {
    if (finished_) {
      PrepareAudition(patch, modulations, size);
      return;
    }

    SetCommonPatch(patch);
    SetCommonModulations(modulations, size);
    patch->engine = engine_;
    patch->harmonics = HarmonicsForPatch(engine_, patch_index_);
    patch->timbre = TimbreValue(timbre_state_);
    patch->note = PitchValue(pitch_state_);

    const bool triggered = articulation_state_ != 0;
    modulations->trigger_patched = triggered;
    modulations->trigger = triggered && block_ >= 2 && block_ < 34
        ? 1.0f : 0.0f;
  }

  void Observe(float usage, const Voice::Frame* frames, size_t size) {
    if (finished_) {
      ++audition_block_;
      const uint32_t loop = AuditionLoopBlocks();
      if (audition_block_ >= loop) audition_block_ = 0;
      return;
    }

    if (block_ >= kWarmupBlocks) {
      if (usage > peak_usage_) peak_usage_ = usage;
      if (usage >= 0.9f) ++over_ninety_;
      if (usage >= 1.0f) ++missed_deadline_;
      for (size_t i = 0; i < size; ++i) {
        int32_t sample = frames[i].out;
        if (sample < 0) sample = -sample;
        if (sample > kNumericalSilenceFloor) state_has_signal_ = true;
      }
    }

    ++block_;
    ++stress_blocks_;
    if (block_ < kBlocksPerState) return;

    if (IsCentreSignalCheck()) {
      if (state_has_signal_) patch_has_centre_signal_ = true;
      // A voice can legitimately be nearly silent as a drone while producing
      // a healthy triggered envelope. Only fail patches silent in both modes.
      if (articulation_state_ == kArticulationStates - 1 &&
          !patch_has_centre_signal_) {
        ++silent_centres_;
      }
    }
    block_ = 0;
    state_has_signal_ = false;
    AdvanceState();
  }

  void MuteStress(Voice::Frame* frames, size_t size) const {
    if (finished_) return;
    for (size_t i = 0; i < size; ++i) {
      frames[i].out = 0;
      frames[i].aux = 0;
    }
  }

 private:
  static float TimbreValue(int state) {
    static const float values[kTimbreStates] = {
      0.02f, 0.25f, 0.5f, 0.75f, 0.98f
    };
    return values[state];
  }

  static float PitchValue(int state) {
    static const float values[kPitchStates] = {
      12.0f, 48.0f, 84.0f, 108.0f
    };
    return values[state];
  }

  static int PatchCount(int engine) {
    const int bank = kEngineUserDataBank[engine];
    return bank >= 0
        ? static_cast<int>(kResolvedUserDataBankSize[bank] / fm::Patch::SYX_SIZE)
        : 0;
  }

  static float HarmonicsForPatch(int engine, int patch_index) {
    const int count = PatchCount(engine);
    return count > 0
        ? (static_cast<float>(patch_index) + 0.5f) /
            (static_cast<float>(count) * 1.02f)
        : 0.0f;
  }

  static void SetCommonPatch(Patch* patch) {
    patch->morph = 0.5f;
    patch->frequency_modulation_amount = 0.0f;
    patch->timbre_modulation_amount = 0.0f;
    patch->morph_modulation_amount = 0.0f;
    patch->decay = 0.5f;
    patch->lpg_colour = 0.5f;
    patch->freqlock_param = 0.5f;
    patch->locked_frequency_pot_option = 0;
    patch->model_cv_option = 0;
    patch->level_cv_option = 0;
    patch->aux_output_option = 0;
    patch->aux_subosc_option = 0;
    patch->chord_set_option = 0;
    patch->hold_on_trigger_option = 0;
    patch->attenuverter_mode = 0;
  }

  static void SetCommonModulations(Modulations* modulations, size_t size) {
    modulations->engine = 0.0f;
    modulations->note = 0.0f;
    modulations->frequency = 0.0f;
    modulations->harmonics = 0.0f;
    modulations->timbre = 0.0f;
    modulations->morph = 0.0f;
    modulations->trigger = 0.0f;
    modulations->level = 1.0f;
    modulations->hard_sync = 0;
    modulations->frequency_audio_rate = false;
    modulations->frequency_patched = false;
    modulations->timbre_patched = false;
    modulations->morph_patched = false;
    modulations->trigger_patched = false;
    modulations->level_patched = true;
    for (size_t i = 0; i < size; ++i) {
      modulations->frequency_audio[i] = 0.0f;
    }
  }

  bool IsCentreSignalCheck() const {
    return timbre_state_ == 2 && pitch_state_ == 1;
  }

  void AdvanceState() {
    if (++pitch_state_ < kPitchStates) return;
    pitch_state_ = 0;
    if (++timbre_state_ < kTimbreStates) return;
    timbre_state_ = 0;
    if (++articulation_state_ < kArticulationStates) return;
    articulation_state_ = 0;
    patch_has_centre_signal_ = false;
    if (++patch_index_ < PatchCount(engine_)) return;
    patch_index_ = 0;
    if (++engine_ < PLAITS_ENGINE_COUNT) return;

    engine_ = PLAITS_ENGINE_COUNT - 1;
    finished_ = true;
    passed_ = failure_mask() == 0;
    audition_block_ = 0;
  }

  static uint32_t AuditionLoopBlocks() {
    return static_cast<uint32_t>(kAuditionScenes) * kAuditionSeconds *
        static_cast<uint32_t>(kSampleRate / kBlockSize);
  }

  void PrepareAudition(Patch* patch, Modulations* modulations, size_t size) {
    SetCommonPatch(patch);
    SetCommonModulations(modulations, size);

    const uint32_t blocks_per_scene = kAuditionSeconds *
        static_cast<uint32_t>(kSampleRate / kBlockSize);
    const int scene = static_cast<int>(audition_block_ / blocks_per_scene);
    const uint32_t within = audition_block_ % blocks_per_scene;
    const float phase = static_cast<float>(within) /
        static_cast<float>(blocks_per_scene);
    const int engine = scene % PLAITS_ENGINE_COUNT;
    const int count = PatchCount(engine);
    const int patch_positions[kAuditionScenes] = { 0, 1, 2, 3, 1, 2 };
    const int divisor[kAuditionScenes] = { 1, 3, 3, 3, 2, 2 };
    int selected = count > 0
        ? (patch_positions[scene] * (count - 1)) / divisor[scene]
        : 0;

    patch->engine = engine;
    patch->harmonics = HarmonicsForPatch(engine, selected);
    patch->note = 36.0f + 12.0f * static_cast<float>(scene);
    if (phase < 0.125f) {
      patch->timbre = 0.5f;
    } else if (phase < 0.5f) {
      patch->timbre = 0.5f - (phase - 0.125f) / 0.375f * 0.48f;
    } else if (phase < 0.625f) {
      patch->timbre = 0.5f;
    } else {
      patch->timbre = 0.5f + (phase - 0.625f) / 0.375f * 0.48f;
    }

    const bool triggered = scene & 1;
    modulations->trigger_patched = triggered;
    if (triggered) {
      const uint32_t half_second = static_cast<uint32_t>(
          0.5f * kSampleRate / kBlockSize);
      modulations->trigger = within % half_second < half_second / 2
          ? 1.0f : 0.0f;
    }
  }

  int engine_;
  int patch_index_;
  int timbre_state_;
  int pitch_state_;
  int articulation_state_;
  int block_;
  float peak_usage_;
  uint32_t over_ninety_;
  uint32_t missed_deadline_;
  uint32_t silent_centres_;
  bool state_has_signal_;
  bool patch_has_centre_signal_;
  bool finished_;
  bool passed_;
  uint32_t audition_block_;
  uint32_t stress_blocks_;
  uint32_t total_stress_blocks_;
};

}  // namespace plaits

#endif  // PLAITS_FM_CARRIER_DIAGNOSTIC_H_
