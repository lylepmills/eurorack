// Copyright 2026 Rubato Audio.
//
// Production-faithful overrun threshold. Private diagnostic firmware only.
//
// The overrun sweep (overrun_sweep.h) writes its own test sine after Render
// and keeps books inside the audio interrupt, so its "stale output starts
// here" load includes ~8% of work production firmware never does. This build
// measures where production itself starts playing stale data:
//
//   * the signal is production's own: engine 0 (a cheap engine) on OUT, and
//     the Voice sub-oscillator in sine mode on AUX, rendered and written by
//     the ordinary post-processing path;
//   * the added load is a busy-wait inside Voice::Render, just before the
//     post-processors write the block -- where an engine's own work sits, so
//     a too-slow block is written late exactly as a too-slow engine's is;
//   * per block the interrupt only reads the cycle counter, compares, and
//     reads one DMA flag; the per-step results sit in RAM and go out as FSK
//     (same framing as the sweep) only after the ladder.
//
// Ladder: target load 0.80 .. 1.10 in 0.01 steps, 3 s each, three passes. The
// cost reported per step is the sweep's bracket -- interrupt entry to the end
// of Render -- so the onset maps directly onto the sweep's per-engine peaks.

#ifndef PLAITS_THRESHOLD_LADDER_H_
#define PLAITS_THRESHOLD_LADDER_H_

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

#ifdef TEST
extern uint32_t threshold_ladder_test_cycles;
inline uint32_t ThresholdCycles() {
  threshold_ladder_test_cycles += 50;
  return threshold_ladder_test_cycles;
}
inline void ThresholdEnableCycles() { }
#else
inline uint32_t ThresholdCycles() { return DWT->CYCCNT; }
inline void ThresholdEnableCycles() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
#endif

class ThresholdLadder {
 public:
  enum Phase { PHASE_START, PHASE_LADDER, PHASE_REPORT };

  static const int kVersion = 1;
  static const int kBlocksPerSecond = 3989;
  static const int kSteps = 31;                     // 0.80 .. 1.10
  static const int kPasses = 3;
  static const int kStepBlocks = 3 * kBlocksPerSecond;
  static const int kSettleBlocks = kBlocksPerSecond / 4;
  static const int kStartBlocks = 3 * kBlocksPerSecond;
  static const int kQueueSize = 320;
  static const int kBaud = 1200;
  static const uint8_t kPacketHello = 1;
  static const uint8_t kPacketThreshold = 8;
  static const int kStepBytes = 8;

  ThresholdLadder() { }

  void Init() {
    ThresholdEnableCycles();
    phase_ = PHASE_START;
    step_ = 0;
    pass_ = 0;
    block_ = 0;
    start_ = 0;
    budget_ = static_cast<uint32_t>(
        kBlockSize * (static_cast<float>(F_CPU) / kSampleRate));
    target_ = 0;
    for (int p = 0; p < kPasses; ++p) {
      for (int s = 0; s < kSteps; ++s) {
        Stats& st = stats_[p][s];
        st.max = 0;
        st.sum = 0;
        st.count = 0;
        st.late = 0;
      }
    }
    fsk_.Init();
    report_pass_ = 0;
    report_gap_ = 0;
    EnqueueHello();
  }

  bool reporting() const { return phase_ == PHASE_REPORT; }
  Phase phase() const { return phase_; }
  int step() const { return step_; }
  int pass() const { return pass_; }

  static float Load(int step) { return 0.80f + 0.01f * static_cast<float>(step); }

  inline void BeginCallback() { start_ = ThresholdCycles(); }

  // Engine 0 at rest, TRIG and LEVEL unpatched; AUX is the Voice's
  // sub-oscillator as a sine at 1330 Hz (36 samples a cycle, so a block
  // replayed from one or two DMA laps earlier is a third of a cycle out).
  void Prepare(Patch* patch, Modulations* modulations) {
    patch->engine = 0;
    patch->note = 88.25f;  // 1330 Hz
    patch->harmonics = 0.5f;
    patch->timbre = 0.5f;
    patch->morph = 0.5f;
    patch->frequency_modulation_amount = 0.0f;
    patch->timbre_modulation_amount = 0.0f;
    patch->morph_modulation_amount = 0.0f;
    patch->decay = 0.5f;
    patch->lpg_colour = 0.5f;
    patch->locked_frequency_pot_option = 0;
    patch->trig_response_option = 0;
    patch->model_cv_option = 0;
    patch->level_cv_option = 0;
    patch->aux_output_option = 2;   // sub-oscillator
    patch->aux_subosc_option = 3;   // sine, no octave down
    patch->hold_on_trigger_option = 0;
    patch->attenuverter_mode = 0;
    modulations->engine = 0.0f;
    modulations->note = 0.0f;
    modulations->frequency = 0.0f;
    modulations->harmonics = 0.0f;
    modulations->timbre = 0.0f;
    modulations->morph = 0.0f;
    modulations->trigger = 0.0f;
    modulations->level = 0.0f;
    modulations->hard_sync = 0;
    modulations->frequency_audio_rate = false;
    modulations->frequency_patched = false;
    modulations->timbre_patched = false;
    modulations->morph_patched = false;
    modulations->trigger_patched = false;
    modulations->level_patched = false;
  }

  // Inside Voice::Render, just before the post-processors write the block.
  inline void Burn() {
    if (phase_ != PHASE_LADDER) return;
    while (ThresholdCycles() - start_ < target_) { }
  }

  // Right after Voice::Render. `late`: the DMA already entered this half.
  inline void EndRender(bool late) {
    if (phase_ == PHASE_LADDER && block_ >= kSettleBlocks) {
      const uint32_t cycles = ThresholdCycles() - start_;
      Stats& st = stats_[pass_][step_];
      if (cycles > st.max) st.max = cycles;
      st.sum += cycles;
      ++st.count;
      st.late += late;
    }
    if (phase_ != PHASE_REPORT && ++block_ >= BlockLimit()) Advance();
  }

  // Start phase only: the HELLO packet on AUX. During the ladder nothing is
  // written after Render.
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
    uint16_t count;
    uint16_t late;
  };

  int BlockLimit() const {
    return phase_ == PHASE_START ? kStartBlocks : kStepBlocks;
  }

  void SetTarget() {
    target_ = static_cast<uint32_t>(static_cast<float>(budget_) * Load(step_));
  }

  void Advance() {
    block_ = 0;
    if (phase_ == PHASE_START) {
      phase_ = PHASE_LADDER;
      step_ = 0;
      pass_ = 0;
      SetTarget();
      return;
    }
    if (++step_ >= kSteps) {
      step_ = 0;
      if (++pass_ >= kPasses) {
        pass_ = kPasses - 1;
        phase_ = PHASE_REPORT;
        report_pass_ = 0;
        report_gap_ = 0;
        return;
      }
    }
    SetTarget();
  }

  uint16_t Code(uint32_t cycles) const {
    const uint32_t code = (cycles * 1000u + budget_ / 2) / budget_;
    return static_cast<uint16_t>(code > 0xffff ? 0xffff : code);
  }

  void EnqueueHello() {
    fsk_.BeginPacket(kPacketHello, 8);
    fsk_.Put(kVersion);
    fsk_.Put(0xff);                // group 255: the threshold ladder
    fsk_.Put(0);                   // no engines swept
    fsk_.Put(kSteps);
    fsk_.Put16(kPasses);
    fsk_.Put16(static_cast<uint16_t>(kStepBlocks));
    fsk_.EndPacket();
  }

  // One packet per pass: pass, then per step target, max, mean, late.
  void EnqueueReportItem() {
    fsk_.BeginPacket(kPacketThreshold, 1 + kSteps * kStepBytes);
    fsk_.Put(static_cast<uint8_t>(report_pass_));
    for (int s = 0; s < kSteps; ++s) {
      const Stats& st = stats_[report_pass_][s];
      fsk_.Put16(static_cast<uint16_t>(Load(s) * 1000.0f + 0.5f));
      fsk_.Put16(Code(st.max));
      fsk_.Put16(Code(st.count ? st.sum / st.count : 0));
      fsk_.Put16(st.late);
    }
    fsk_.EndPacket();
    if (++report_pass_ >= kPasses) {
      report_pass_ = 0;
      report_gap_ = 47872;  // a second of silence per pass
    }
  }

  Phase phase_;
  int step_;
  int pass_;
  int block_;
  uint32_t start_;
  uint32_t budget_;
  uint32_t target_;
  Stats stats_[kPasses][kSteps];

  DiagnosticFsk<kQueueSize> fsk_;
  int report_pass_;
  int report_gap_;

  DISALLOW_COPY_AND_ASSIGN(ThresholdLadder);
};

}  // namespace plaits

#endif  // PLAITS_THRESHOLD_LADDER_H_
