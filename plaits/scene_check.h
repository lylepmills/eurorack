// Copyright 2026 Rubato Audio.
//
// Production-faithful spot check of specific engine settings. Private
// diagnostic firmware only.
//
// Plays a short list of fixed scenes -- engine + HARMONICS/TIMBRE/MORPH/TWIST
// + pitch + AUX mode + TRIG -- for 20 s each through the ordinary production
// signal path, nothing written after Render. Per block the interrupt only
// reads the cycle counter and one DMA flag. In mono scenes AUX is the Voice's
// sine sub-oscillator, which the host checks for stale blocks the same way as
// in threshold_ladder.h; in stereo scenes AUX is the engine's right channel,
// so those are judged by cost against the measured threshold and by ear.
// Results go out as FSK after the last scene, with a per-scene breakdown of
// where the block's time goes: panel scan (Ui::Poll), Voice setup before the
// engine, the engine's own Render, and post-processing (sub-oscillator, outer
// LPG, output) -- from timestamps at those boundaries (Mark()).
//
// The scene table comes from a generated header (PLAITS_SCENE_TABLE), written
// by alt_firmwares/plaits_lab_builder/export_overrun_sweep.py --scenes.

#ifndef PLAITS_SCENE_CHECK_H_
#define PLAITS_SCENE_CHECK_H_

#include <stddef.h>
#include <stdint.h>

#include "plaits/diagnostic_fsk.h"
#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"

#ifndef TEST
#include <stm32f37x_conf.h>
#endif

#ifndef F_CPU
#define F_CPU 72000000L
#endif

namespace plaits {

struct CheckScene {
  int engine;
  float harmonics;
  float timbre;
  float morph;
  float twist;
  float note;
  uint8_t aux_output_option;   // 0 regular, 1 stereo, 2 sub-oscillator
  uint8_t aux_subosc_option;   // 3 = sine, no octave down
  uint8_t triggered;           // 0 unpatched, 1 a 4 Hz trigger
};

#ifdef TEST
extern uint32_t scene_check_test_cycles;
inline uint32_t SceneCycles() {
  scene_check_test_cycles += 50;
  return scene_check_test_cycles;
}
inline void SceneEnableCycles() { }
#else
inline uint32_t SceneCycles() { return DWT->CYCCNT; }
inline void SceneEnableCycles() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
#endif

template <int kScenes>
class SceneCheck {
 public:
  enum Phase { PHASE_START, PHASE_SCENES, PHASE_REPORT };

  static const int kVersion = 1;
  static const int kBlocksPerSecond = 3989;
  static const int kStartBlocks = 3 * kBlocksPerSecond;
  static const int kSceneBlocks = 20 * kBlocksPerSecond;
  static const int kSwitchBlocks = kBlocksPerSecond / 2;
  static const int kTriggerPeriod = kBlocksPerSecond / 4;   // 4 Hz
  static const int kTriggerLength = 4;
  static const uint8_t kPacketHello = 1;
  static const uint8_t kPacketScene = 9;
  static const uint8_t kPacketSections = 10;
  static const int kSections = 4;   // ui, voice setup, engine, post

  SceneCheck() { }

  void Init(const CheckScene* scenes) {
    SceneEnableCycles();
    scenes_ = scenes;
    phase_ = PHASE_START;
    scene_ = 0;
    block_ = 0;
    start_ = 0;
    budget_ = static_cast<uint32_t>(
        kBlockSize * (static_cast<float>(F_CPU) / kSampleRate));
    for (int i = 0; i < kScenes; ++i) {
      Stats& s = stats_[i];
      s.max = s.sum = s.switch_max = 0;
      s.count = s.late = s.switch_late = 0;
      for (int k = 0; k < kSections; ++k) {
        s.section_sum[k] = 0;
        s.section_max[k] = 0;
      }
    }
    for (int k = 0; k < 4; ++k) mark_[k] = 0;
    fsk_.Init();
    report_scene_ = 0;
    report_gap_ = 0;
    fsk_.BeginPacket(kPacketHello, 8);
    fsk_.Put(kVersion);
    fsk_.Put(0xfe);                // group 254: the scene check
    fsk_.Put(kScenes);
    fsk_.Put(0);
    fsk_.Put16(static_cast<uint16_t>(kSceneBlocks / 4));
    fsk_.Put16(static_cast<uint16_t>(kStartBlocks));
    fsk_.EndPacket();
  }

  bool reporting() const { return phase_ == PHASE_REPORT; }
  int scene() const { return scene_; }
  Phase phase() const { return phase_; }

  inline void BeginCallback() {
    start_ = SceneCycles();
    mark_[1] = mark_[2] = mark_[3] = start_;
  }

  // Section boundaries: 1 after Ui::Poll, 2 before and 3 after the engine.
  inline void Mark(int section) { mark_[section] = SceneCycles(); }

  void Prepare(Patch* patch, Modulations* modulations) {
    // The start phase plays the first scene's engine at rest.
    const CheckScene& s = scenes_[phase_ == PHASE_SCENES ? scene_ : 0];
    const bool active = phase_ == PHASE_SCENES;
    patch->engine = s.engine;
    patch->note = active ? s.note : 60.0f;
    patch->harmonics = active ? s.harmonics : 0.5f;
    patch->timbre = active ? s.timbre : 0.5f;
    patch->morph = active ? s.morph : 0.5f;
    patch->freqlock_param = active ? s.twist : 0.5f;
    patch->locked_frequency_pot_option = 1;  // TWIST on the FREQUENCY knob
    patch->frequency_modulation_amount = 0.0f;
    patch->timbre_modulation_amount = 0.0f;
    patch->morph_modulation_amount = 0.0f;
    patch->decay = 0.5f;
    patch->lpg_colour = 0.5f;
    patch->trig_response_option = 0;
    patch->model_cv_option = 0;
    patch->level_cv_option = 0;
    patch->aux_output_option = active ? s.aux_output_option : 0;
    patch->aux_subosc_option = s.aux_subosc_option;
    patch->hold_on_trigger_option = 0;
    patch->attenuverter_mode = 0;
    modulations->engine = 0.0f;
    modulations->note = 0.0f;
    modulations->frequency = 0.0f;
    modulations->harmonics = 0.0f;
    modulations->timbre = 0.0f;
    modulations->morph = 0.0f;
    modulations->level = 0.0f;
    modulations->hard_sync = 0;
    modulations->frequency_audio_rate = false;
    modulations->frequency_patched = false;
    modulations->timbre_patched = false;
    modulations->morph_patched = false;
    modulations->level_patched = false;
    modulations->trigger_patched = active && s.triggered;
    modulations->trigger =
        active && s.triggered && (block_ % kTriggerPeriod) < kTriggerLength
            ? 1.0f : 0.0f;
  }

  // Right after Voice::Render. `late`: the DMA already entered this half.
  inline void EndRender(bool late) {
    if (phase_ == PHASE_SCENES) {
      const uint32_t cycles = SceneCycles() - start_;
      Stats& s = stats_[scene_];
      if (block_ < kSwitchBlocks) {
        if (cycles > s.switch_max) s.switch_max = cycles;
        s.switch_late += late;
      } else {
        if (cycles > s.max) s.max = cycles;
        s.sum += cycles;
        ++s.count;
        s.late += late;
        const uint32_t end = start_ + cycles;
        const uint32_t section[kSections] = {
          mark_[1] - start_, mark_[2] - mark_[1],
          mark_[3] - mark_[2], end - mark_[3] };
        for (int k = 0; k < kSections; ++k) {
          s.section_sum[k] += section[k];
          if (section[k] > s.section_max[k]) s.section_max[k] = section[k];
        }
      }
    }
    if (phase_ == PHASE_REPORT) return;
    ++block_;
    if (phase_ == PHASE_START && block_ >= kStartBlocks) {
      phase_ = PHASE_SCENES;
      scene_ = 0;
      block_ = 0;
    } else if (phase_ == PHASE_SCENES && block_ >= kSceneBlocks) {
      block_ = 0;
      if (++scene_ >= kScenes) {
        scene_ = kScenes - 1;
        phase_ = PHASE_REPORT;
      }
    }
  }

  void WriteStart(Voice::Frame* frames, size_t size) {
    if (phase_ != PHASE_START) return;
    for (size_t i = 0; i < size; ++i) {
      if (fsk_.transmitting()) frames[i].aux = fsk_.Sample();
    }
  }

  void WriteReport(Voice::Frame* frames, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      if (!fsk_.transmitting()) {
        if (report_gap_ > 0) {
          --report_gap_;
        } else {
          EnqueueReportItem();
        }
      }
      frames[i].out = 0;
      frames[i].aux = fsk_.transmitting() ? fsk_.Sample() : 0;
    }
  }

 private:
  struct Stats {
    uint32_t max;
    uint32_t sum;
    uint32_t switch_max;
    uint32_t count;
    uint16_t late;
    uint16_t switch_late;
    uint32_t section_sum[kSections];
    uint32_t section_max[kSections];
  };

  uint16_t Code(uint32_t cycles) const {
    const uint32_t code = static_cast<uint32_t>(
        (static_cast<uint64_t>(cycles) * 1000u + budget_ / 2) / budget_);
    return static_cast<uint16_t>(code > 0xffff ? 0xffff : code);
  }

  // Two packets per scene. SCENE: scene, engine, peak, mean, late, switch
  // peak, switch late, blocks/16. SECTIONS: scene, then mean and max of each
  // section.
  void EnqueueReportItem() {
    const Stats& s = stats_[report_scene_];
    fsk_.BeginPacket(kPacketSections, 1 + kSections * 4);
    fsk_.Put(static_cast<uint8_t>(report_scene_));
    for (int k = 0; k < kSections; ++k) {
      fsk_.Put16(Code(s.count ? s.section_sum[k] / s.count : 0));
      fsk_.Put16(Code(s.section_max[k]));
    }
    fsk_.EndPacket();
    fsk_.BeginPacket(kPacketScene, 14);
    fsk_.Put(static_cast<uint8_t>(report_scene_));
    fsk_.Put(static_cast<uint8_t>(scenes_[report_scene_].engine));
    fsk_.Put16(Code(s.max));
    fsk_.Put16(Code(s.count ? s.sum / s.count : 0));
    fsk_.Put16(s.late);
    fsk_.Put16(Code(s.switch_max));
    fsk_.Put16(s.switch_late);
    fsk_.Put16(static_cast<uint16_t>(s.count / 16));
    fsk_.EndPacket();
    if (++report_scene_ >= kScenes) {
      report_scene_ = 0;
      report_gap_ = 47872;
    }
  }

  uint32_t mark_[4];
  const CheckScene* scenes_;
  Phase phase_;
  int scene_;
  int block_;
  uint32_t start_;
  uint32_t budget_;
  Stats stats_[kScenes];
  DiagnosticFsk<96> fsk_;
  int report_scene_;
  int report_gap_;

  DISALLOW_COPY_AND_ASSIGN(SceneCheck);
};

}  // namespace plaits

#endif  // PLAITS_SCENE_CHECK_H_
