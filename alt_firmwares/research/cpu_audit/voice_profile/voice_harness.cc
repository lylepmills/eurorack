// Whole-Voice cost harness: Voice::Render (prelude, engine, sub-osc, LPG,
// output) on an emulated Cortex-M4, linked against a real firmware build's
// objects. Run twice with different block counts and subtract.
#include <stdint.h>
#include <string.h>
#include "stmlib/utils/buffer_allocator.h"
#include "plaits/dsp/voice.h"

#ifndef VH_HASH
#define VH_HASH 0
#endif
#ifndef VH_DUMP
#define VH_DUMP 0
#endif

using namespace plaits;

extern "C" void plaits_section_mark(int) { }
extern "C" void plaits_ui_task_mark(int) { }
extern "C" void plaits_stock_mark(int) { }

static inline int Semihost(int op, void* arg) {
  register int r0 asm("r0") = op;
  register void* r1 asm("r1") = arg;
  asm volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

char shared_buffer[16384] __attribute__((aligned(8)));
static Patch patch;
static Modulations modulations;
static Voice::Frame frames[kBlockSize];
static volatile int g_sink;

// FNV-1a over every output sample, printed through semihosting (SYS_WRITE0),
// so an optimization can be proven output-identical at the Voice level.
static uint32_t g_hash = 2166136261u;
static void HashFrames() {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(frames);
  for (size_t i = 0; i < sizeof(frames); ++i) {
    g_hash = (g_hash ^ b[i]) * 16777619u;
  }
}
// Semihosting file output (SYS_OPEN / SYS_WRITE / SYS_CLOSE) of every frame,
// for sample-by-sample comparison of two builds.
static int g_dump = -1;
static void DumpOpen() {
  static const char name[] = "vh_frames.raw";
  uint32_t args[3] = { (uint32_t)name, 5u /* wb */, sizeof(name) - 1 };
  g_dump = Semihost(0x01, args);
}
static void DumpFrames() {
  uint32_t args[3] = { (uint32_t)g_dump, (uint32_t)frames, sizeof(frames) };
  Semihost(0x05, args);
}
static void DumpClose() {
  uint32_t args[1] = { (uint32_t)g_dump };
  Semihost(0x02, args);
}
static void PrintHash() {
  static char text[] = "HASH 00000000\n";
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 8; ++i) text[5 + i] = hex[(g_hash >> (28 - 4 * i)) & 15];
  Semihost(0x04, text);
}

int main() {
  // Constructed here: the bare-metal startup runs no global constructors,
  // and Voice's engines need theirs (vtables).
  static Voice voice;
  stmlib::BufferAllocator allocator(shared_buffer, 16384);
  voice.Init(&allocator);
  memset(&patch, 0, sizeof(patch));
  memset(&modulations, 0, sizeof(modulations));
  patch.engine = VH_ENGINE;
  patch.note = VH_NOTE;
  patch.harmonics = VH_H;
  patch.timbre = VH_T;
  patch.morph = VH_M;
  patch.decay = 0.5f;
  patch.lpg_colour = 0.5f;
#ifndef VH_STOCK
  patch.freqlock_param = 0.5f;
  patch.locked_frequency_pot_option = 1;
  patch.aux_output_option = VH_AUX;
  patch.aux_subosc_option = 3;
#endif
  modulations.trigger_patched = VH_TRIG != 0;
#if VH_DUMP
  DumpOpen();
#endif
  for (int i = 0; i < VH_BLOCKS; ++i) {
    modulations.trigger = (VH_TRIG && (i % 997) < 4) ? 1.0f : 0.0f;
    voice.Render(patch, modulations, frames, kBlockSize);
    g_sink += frames[0].out;
#if VH_HASH
    HashFrames();
#endif
#if VH_DUMP
    DumpFrames();
#endif
  }
#if VH_HASH
  PrintHash();
#endif
#if VH_DUMP
  DumpClose();
#endif
  uint32_t exit_block[2] = { 0x20026u, 0u };
  Semihost(0x18, exit_block);
  for (;;) { }
  return 0;
}
