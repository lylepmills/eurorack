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

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
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
    queue_head_ = queue_tail_ = 0;
    bits_left_ = 0;
    shift_ = 0;
    lead_in_bits_ = kLeadInBits;
    trail_bits_ = kTrailBits;
    fsk_active_ = false;
    current_bit_ = 1;
    tone_phase_ = 0.0f;
    bit_phase_ = 0.0f;
    mark_increment_ = 2400.0f / kCorrectedSampleRate;
    space_increment_ = 4800.0f / kCorrectedSampleRate;
    bit_increment_ = static_cast<float>(kBaud) / kCorrectedSampleRate;
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
      if (Transmitting()) frames[i].aux = FskSample();
    }
  }

  void WriteReport(Voice::Frame* frames, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      if (!Transmitting()) {
        if (report_gap_ > 0) {
          --report_gap_;
        } else {
          EnqueueReportItem();
        }
      }
      frames[i].out = 0;
      frames[i].aux = Transmitting() ? FskSample() : 0;
    }
  }

  static uint16_t CrcUpdate(uint16_t crc, uint8_t byte) {
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (int i = 0; i < 8; ++i) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
    return crc;
  }

 private:
  struct Stats {
    uint32_t max;
    uint32_t sum;
    uint16_t count;
    uint16_t late;
  };

  static const int kLeadInBits = 24;
  static const int kTrailBits = 12;

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

  // ---- Packets (the overrun sweep's framing) ----

  void Push(uint8_t byte) {
    const int next = (queue_tail_ + 1) % kQueueSize;
    if (next == queue_head_) return;
    queue_[queue_tail_] = byte;
    queue_tail_ = next;
  }

  void PushCrc(uint8_t byte, uint16_t* crc) {
    Push(byte);
    *crc = CrcUpdate(*crc, byte);
  }

  void Push16(uint16_t value, uint16_t* crc) {
    PushCrc(static_cast<uint8_t>(value & 0xff), crc);
    PushCrc(static_cast<uint8_t>(value >> 8), crc);
  }

  void BeginPacket(uint8_t type, uint8_t length, uint16_t* crc) {
    Push(0x55);
    Push(0x55);
    Push(0x7e);
    Push(0xa5);
    *crc = 0xffff;
    PushCrc(type, crc);
    PushCrc(length, crc);
  }

  void EndPacket(uint16_t crc) {
    Push(static_cast<uint8_t>(crc & 0xff));
    Push(static_cast<uint8_t>(crc >> 8));
  }

  void EnqueueHello() {
    uint16_t crc;
    BeginPacket(kPacketHello, 8, &crc);
    PushCrc(kVersion, &crc);
    PushCrc(0xff, &crc);           // group 255: the threshold ladder
    PushCrc(0, &crc);              // no engines swept
    PushCrc(kSteps, &crc);
    Push16(kPasses, &crc);
    Push16(static_cast<uint16_t>(kStepBlocks), &crc);
    EndPacket(crc);
  }

  // One packet per pass: pass, then per step target, max, mean, late.
  void EnqueueReportItem() {
    uint16_t crc;
    BeginPacket(kPacketThreshold, 1 + kSteps * kStepBytes, &crc);
    PushCrc(static_cast<uint8_t>(report_pass_), &crc);
    for (int s = 0; s < kSteps; ++s) {
      const Stats& st = stats_[report_pass_][s];
      Push16(static_cast<uint16_t>(Load(s) * 1000.0f + 0.5f), &crc);
      Push16(Code(st.max), &crc);
      Push16(Code(st.count ? st.sum / st.count : 0), &crc);
      Push16(st.late, &crc);
    }
    EndPacket(crc);
    if (++report_pass_ >= kPasses) {
      report_pass_ = 0;
      report_gap_ = 47872;  // a second of silence per pass
    }
  }

  // ---- FSK modulator: UART framing, idle mark around each burst ----

  bool Transmitting() const {
    return fsk_active_ || queue_head_ != queue_tail_;
  }

  short FskSample() {
    if (!fsk_active_) {
      fsk_active_ = true;
      bit_phase_ = 0.0f;
      LoadNextBit();
    }
    tone_phase_ += current_bit_ ? mark_increment_ : space_increment_;
    if (tone_phase_ >= 1.0f) tone_phase_ -= 1.0f;
    const short sample = static_cast<short>(Sine(tone_phase_) * 16000.0f);
    bit_phase_ += bit_increment_;
    if (bit_phase_ >= 1.0f) {
      bit_phase_ -= 1.0f;
      if (!LoadNextBit()) fsk_active_ = false;
    }
    return sample;
  }

  bool LoadNextBit() {
    if (bits_left_) {
      current_bit_ = shift_ & 1;
      shift_ >>= 1;
      --bits_left_;
      return true;
    }
    if (queue_head_ != queue_tail_) {
      if (lead_in_bits_) {
        current_bit_ = 1;
        --lead_in_bits_;
        return true;
      }
      const uint8_t byte = queue_[queue_head_];
      queue_head_ = (queue_head_ + 1) % kQueueSize;
      shift_ = (static_cast<uint32_t>(byte) << 1) | (1u << 9);
      current_bit_ = shift_ & 1;
      shift_ >>= 1;
      bits_left_ = 9;
      return true;
    }
    if (trail_bits_) {
      current_bit_ = 1;
      --trail_bits_;
      return true;
    }
    lead_in_bits_ = kLeadInBits;
    trail_bits_ = kTrailBits;
    return false;
  }

  Phase phase_;
  int step_;
  int pass_;
  int block_;
  uint32_t start_;
  uint32_t budget_;
  uint32_t target_;
  Stats stats_[kPasses][kSteps];

  uint8_t queue_[kQueueSize];
  int queue_head_;
  int queue_tail_;
  uint32_t shift_;
  int bits_left_;
  int lead_in_bits_;
  int trail_bits_;
  bool fsk_active_;
  int current_bit_;
  float tone_phase_;
  float bit_phase_;
  float mark_increment_;
  float space_increment_;
  float bit_increment_;
  int report_pass_;
  int report_gap_;

  DISALLOW_COPY_AND_ASSIGN(ThresholdLadder);
};

}  // namespace plaits

#endif  // PLAITS_THRESHOLD_LADDER_H_
