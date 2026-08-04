// Copyright 2026 Rubato Audio.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "plaits/sync_event_queue.h"

namespace {

void ExpectEqual(const char* label, uint32_t actual, uint32_t expected) {
  if (actual != expected) {
    fprintf(
        stderr,
        "%s: got 0x%08lx, expected 0x%08lx\n",
        label,
        static_cast<unsigned long>(actual),
        static_cast<unsigned long>(expected));
    abort();
  }
}

}  // namespace

int main() {
  plaits::SyncEventQueue queue;

  queue.Init(1000);
  queue.PushFromInterrupt(1100);
  queue.PushFromInterrupt(1200);
  queue.PushFromInterrupt(1300);
  ExpectEqual("sample placement", queue.ReadBlock(1400, 12),
      (1u << 3) | (1u << 6) | (1u << 9));

  queue.PushFromInterrupt(1450);
  ExpectEqual("new boundary", queue.ReadBlock(1500, 12), 1u << 6);

  queue.Init(0xfffffff0u);
  queue.PushFromInterrupt(0xfffffff8u);
  queue.PushFromInterrupt(0x00000008u);
  ExpectEqual("timestamp wrap", queue.ReadBlock(0x00000010u, 8),
      (1u << 2) | (1u << 6));

  queue.Init(1000);
  queue.PushFromInterrupt(1200);
  ExpectEqual("future event deferred", queue.ReadBlock(1100, 12), 0);
  ExpectEqual("deferred event placed", queue.ReadBlock(1300, 12), 1u << 6);

  queue.Init(1000);
  queue.PushFromInterrupt(900);
  ExpectEqual("stale event discarded", queue.ReadBlock(1100, 12), 0);

  queue.Init(0);
  for (uint32_t i = 1; i <= plaits::SyncEventQueue::CAPACITY; ++i) {
    queue.PushFromInterrupt(i);
  }
  ExpectEqual("one-slot-empty overflow", queue.dropped(), 1);

  printf("sync_event_queue_test: all checks passed\n");
  return 0;
}
