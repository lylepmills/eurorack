// Whole-Voice cost harness: Voice::Render (prelude, engine, sub-osc, LPG,
// output) on an emulated Cortex-M4, linked against a real firmware build's
// objects. Run twice with different block counts and subtract.
#include <stdint.h>
#include <string.h>
#include "stmlib/utils/buffer_allocator.h"
#include "plaits/dsp/voice.h"

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
  for (int i = 0; i < VH_BLOCKS; ++i) {
    modulations.trigger = (VH_TRIG && (i % 997) < 4) ? 1.0f : 0.0f;
    voice.Render(patch, modulations, frames, kBlockSize);
    g_sink += frames[0].out;
  }
  uint32_t exit_block[2] = { 0x20026u, 0u };
  Semihost(0x18, exit_block);
  for (;;) { }
  return 0;
}
