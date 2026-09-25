// Copyright 2026 Rubato Audio.
//
// The FSK packet transmitter shared by the private diagnostic firmwares
// (threshold_ladder.h, scene_check.h): 1200-baud UART framing, mark 2400 Hz,
// space 4800 Hz, packets of 0x55 0x55 0x7e 0xa5 type length payload CRC-16.
// The same framing as overrun_sweep.h, decoded by
// plaits/tools/overrun_sweep_host.py.

#ifndef PLAITS_DIAGNOSTIC_FSK_H_
#define PLAITS_DIAGNOSTIC_FSK_H_

#include <stddef.h>
#include <stdint.h>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

template <int kQueueSize>
class DiagnosticFsk {
 public:
  DiagnosticFsk() { }

  void Init() {
    queue_head_ = queue_tail_ = 0;
    bits_left_ = 0;
    shift_ = 0;
    lead_in_bits_ = kLeadInBits;
    trail_bits_ = kTrailBits;
    active_ = false;
    current_bit_ = 1;
    tone_phase_ = 0.0f;
    bit_phase_ = 0.0f;
    mark_increment_ = 2400.0f / kCorrectedSampleRate;
    space_increment_ = 4800.0f / kCorrectedSampleRate;
    bit_increment_ = 1200.0f / kCorrectedSampleRate;
  }

  static uint16_t CrcUpdate(uint16_t crc, uint8_t byte) {
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (int i = 0; i < 8; ++i) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
    return crc;
  }

  void BeginPacket(uint8_t type, uint8_t length) {
    Push(0x55);
    Push(0x55);
    Push(0x7e);
    Push(0xa5);
    crc_ = 0xffff;
    Put(type);
    Put(length);
  }

  void Put(uint8_t byte) {
    Push(byte);
    crc_ = CrcUpdate(crc_, byte);
  }

  void Put16(uint16_t value) {
    Put(static_cast<uint8_t>(value & 0xff));
    Put(static_cast<uint8_t>(value >> 8));
  }

  void EndPacket() {
    const uint16_t crc = crc_;
    Push(static_cast<uint8_t>(crc & 0xff));
    Push(static_cast<uint8_t>(crc >> 8));
  }

  bool transmitting() const {
    return active_ || queue_head_ != queue_tail_;
  }

  short Sample() {
    if (!active_) {
      active_ = true;
      bit_phase_ = 0.0f;
      LoadNextBit();
    }
    tone_phase_ += current_bit_ ? mark_increment_ : space_increment_;
    if (tone_phase_ >= 1.0f) tone_phase_ -= 1.0f;
    const short sample = static_cast<short>(Sine(tone_phase_) * 16000.0f);
    bit_phase_ += bit_increment_;
    if (bit_phase_ >= 1.0f) {
      bit_phase_ -= 1.0f;
      if (!LoadNextBit()) active_ = false;
    }
    return sample;
  }

 private:
  static const int kLeadInBits = 24;
  static const int kTrailBits = 12;

  void Push(uint8_t byte) {
    const int next = (queue_tail_ + 1) % kQueueSize;
    if (next == queue_head_) return;
    queue_[queue_tail_] = byte;
    queue_tail_ = next;
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

  uint8_t queue_[kQueueSize];
  int queue_head_;
  int queue_tail_;
  uint32_t shift_;
  int bits_left_;
  int lead_in_bits_;
  int trail_bits_;
  bool active_;
  int current_bit_;
  float tone_phase_;
  float bit_phase_;
  float mark_increment_;
  float space_increment_;
  float bit_increment_;
  uint16_t crc_;
};

}  // namespace plaits

#endif  // PLAITS_DIAGNOSTIC_FSK_H_
