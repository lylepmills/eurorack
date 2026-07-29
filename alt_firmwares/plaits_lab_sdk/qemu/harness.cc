// Cost harness: run one engine for a fixed number of blocks on an emulated
// Cortex-M4, so a TCG plugin can count what it actually executes.
//
// The block count comes from -DPLAITS_QEMU_BLOCKS. The SDK runs the harness
// TWICE with different counts and subtracts, which cancels startup, table
// initialisation and Init() entirely -- what remains is exactly the marginal
// cost of the render loop, with no need for the plugin to know when to start
// counting.
//
// SPDX-License-Identifier: MIT

#include <stddef.h>
#include <stdint.h>

#include "stmlib/utils/buffer_allocator.h"
#include "plaits/dsp/engine/engine.h"
#include PLAITS_LAB_ENGINE_HEADER

using namespace plaits;

#ifndef PLAITS_QEMU_BLOCKS
#define PLAITS_QEMU_BLOCKS 1000
#endif

// Parameters are settable so the harness can be run at the SAME knob positions
// a hardware sweep was measured at -- pairing counts with cycles is the whole
// point, and it only works if both sides evaluate the identical workload.
#ifndef PLAITS_QEMU_HARMONICS
#define PLAITS_QEMU_HARMONICS 0.5f
#endif
#ifndef PLAITS_QEMU_MACRO
#define PLAITS_QEMU_MACRO 0.5f
#endif
#ifndef PLAITS_QEMU_TIMBRE
#define PLAITS_QEMU_TIMBRE 0.5f
#endif
// Pitch matters: a physical model's per-sample work can depend on it (delay
// length, filter iterations), so an engine measured at one note may cost quite
// differently at another. Fixing it at 48 is what made inharmonic-string look
// like an outlier against a hardware sweep taken at a different pitch.
#ifndef PLAITS_QEMU_NOTE
#define PLAITS_QEMU_NOTE 48.0f
#endif
// Trigger STATE, not just level. On an unpatched module the voice hands engines
// TRIGGER_UNPATCHED (voice.cc), and engines change behaviour on it -- drums
// sustain, the string engine continuously excites its active voice. Passing 0
// ("patched, low, never fired") measures an engine that has never sounded:
// that mismatch made inharmonic-string count 510 instructions under emulation
// while executing ~4x that on hardware.
#ifndef PLAITS_QEMU_TRIGGER
#define PLAITS_QEMU_TRIGGER 2
#endif

// Stereo requests the L/R render path from engines that branch on it. An
// engine whose stereo branch is a second render (the Pattern B shape: a mono
// AUX voice on one side, an L/R pair on the other) costs DIFFERENTLY in the
// two modes, and pinning this to false measures only the mono half.
#ifndef PLAITS_QEMU_STEREO
#define PLAITS_QEMU_STEREO 0
#endif

// Semihosting: the only way out of a bare-metal guest. bkpt 0xAB is the ARM
// convention; QEMU implements it when started with -semihosting-config.
static inline int Semihost(int op, void* arg) {
  register int r0 asm("r0") = op;
  register void* r1 asm("r1") = arg;
  asm volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

static char g_memory[65536];

// Kept in RAM and consumed after the loop so the compiler cannot decide the
// render calls are dead and remove the very thing being measured.
static float g_sink;

int main() {
  stmlib::BufferAllocator allocator(g_memory, sizeof(g_memory));
  static PLAITS_LAB_ENGINE_CLASS engine;
  engine.Init(&allocator);

  EngineParameters p;
  p.trigger = PLAITS_QEMU_TRIGGER;
  p.note = PLAITS_QEMU_NOTE;
  p.timbre = PLAITS_QEMU_TIMBRE;
  p.morph = 0.5f;
  p.harmonics = PLAITS_QEMU_HARMONICS;
  p.accent = 1.0f;
  p.macro = PLAITS_QEMU_MACRO;
  p.chord_set_option = 0;
  p.stereo = PLAITS_QEMU_STEREO != 0;

  float out[kBlockSize];
  float aux[kBlockSize];
  bool already_enveloped = false;

  for (int i = 0; i < PLAITS_QEMU_BLOCKS; ++i) {
    engine.Render(p, out, aux, kBlockSize, &already_enveloped);
    g_sink += out[0] + aux[0];
  }

  // SYS_EXIT with ADP_Stopped_ApplicationExit.
  uint32_t exit_block[2] = { 0x20026u, 0u };
  Semihost(0x18, exit_block);
  for (;;) { }
  return 0;
}
