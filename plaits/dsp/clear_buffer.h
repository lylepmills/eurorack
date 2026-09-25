// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// Zeroes a buffer 32 bits at a time.
//
// The firmware links newlib-nano, whose memset is built for size and clears
// one byte per loop iteration. Delay lines cleared on a strike or an engine
// switch are kilobytes long, so that byte loop -- or an element loop over
// int8/int16 samples -- costs whole audio blocks: on the module, Blown's and
// Brass's strikes reached 3.7 and 4.5 block periods through memset (on-module
// scene check, 2026-09-25). This writes aligned words, four per iteration.

#ifndef PLAITS_DSP_CLEAR_BUFFER_H_
#define PLAITS_DSP_CLEAR_BUFFER_H_

#include <stddef.h>
#include <stdint.h>

namespace plaits {

// may_alias: the buffer is later read through its own element type.
typedef uint32_t __attribute__((__may_alias__)) ClearWord;

inline void ClearBuffer(void* buffer, size_t bytes) {
  uint8_t* p = static_cast<uint8_t*>(buffer);
  while (bytes && (reinterpret_cast<uintptr_t>(p) & 3)) {
    *p++ = 0;
    --bytes;
  }
  ClearWord* w = reinterpret_cast<ClearWord*>(p);
  size_t words = bytes >> 2;
  while (words >= 4) {
    w[0] = 0;
    w[1] = 0;
    w[2] = 0;
    w[3] = 0;
    w += 4;
    words -= 4;
  }
  while (words--) {
    *w++ = 0;
  }
  p = reinterpret_cast<uint8_t*>(w);
  bytes &= 3;
  while (bytes--) {
    *p++ = 0;
  }
}

}  // namespace plaits

#endif  // PLAITS_DSP_CLEAR_BUFFER_H_
