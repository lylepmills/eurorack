// Copyright 2026 Rubato Audio.
//
// Interrupt-to-audio-block hard-sync event queue. Kept independent of STM32
// headers so its timestamp wrapping and sample placement can be host-tested.

#ifndef PLAITS_SYNC_EVENT_QUEUE_H_
#define PLAITS_SYNC_EVENT_QUEUE_H_

#include <stddef.h>
#include <stdint.h>

namespace plaits {

class SyncEventQueue {
 public:
  enum { CAPACITY = 8 };

  SyncEventQueue() {
    Init(0);
  }

  void Init(uint32_t block_boundary) {
    read_ = 0;
    write_ = 0;
    dropped_ = 0;
    block_boundary_ = block_boundary;
  }

  // Single producer: ADC1 watchdog interrupt.
  inline void PushFromInterrupt(uint32_t timestamp) {
    uint8_t next = (write_ + 1) & (CAPACITY - 1);
    if (next == read_) {
      ++dropped_;
      return;
    }
    timestamps_[write_] = timestamp;
    write_ = next;
  }

  // Single consumer: the audio callback. Events observed during the preceding
  // callback interval are placed at the corresponding position in the block
  // now being rendered. This gives one fixed scheduling block of latency while
  // preserving sub-block timing. The DAC's ping-pong buffer adds one more
  // block before the rendered samples reach the output jack.
  uint32_t ReadBlock(uint32_t now, size_t size) {
    if (!size) {
      block_boundary_ = now;
      return 0;
    }

    const uint32_t start = block_boundary_;
    const uint32_t duration = now - start;
    uint32_t mask = 0;

    // Snapshot the producer cursor. An interrupt arriving after this point is
    // deliberately consumed by the next block.
    const uint8_t end = write_;
    while (read_ != end) {
      const uint32_t timestamp = timestamps_[read_];
      const int32_t from_start = static_cast<int32_t>(timestamp - start);
      const int32_t until_now = static_cast<int32_t>(now - timestamp);

      // A higher-priority ADC interrupt can land after `now` was read but
      // before `end` was snapshotted. Leave that event queued for next time.
      if (until_now < 0) {
        break;
      }

      read_ = (read_ + 1) & (CAPACITY - 1);
      if (from_start < 0 || !duration) {
        // Stale event (or degenerate initial interval). It cannot be placed
        // faithfully, so do not turn it into a late false sync.
        continue;
      }

      // A callback interval is normally about 18k core cycles, so this stays
      // comfortably in 32 bits and avoids pulling 64-bit division into the
      // audio path. Even a multi-second audio stall is outside useful sync
      // operation long before the multiplication could overflow.
      uint32_t index = ((timestamp - start) * size) / duration;
      if (index >= size) {
        index = size - 1;
      }
      // EngineParameters uses a 32-bit mask; Plaits blocks are 12 samples.
      if (index < 32) {
        mask |= 1u << index;
      }
    }

    block_boundary_ = now;
    return mask;
  }

  inline uint32_t dropped() const { return dropped_; }

 private:
  volatile uint32_t timestamps_[CAPACITY];
  volatile uint8_t read_;
  volatile uint8_t write_;
  volatile uint32_t dropped_;
  uint32_t block_boundary_;
};

}  // namespace plaits

#endif  // PLAITS_SYNC_EVENT_QUEUE_H_
