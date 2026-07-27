// Guard for ChiptuneEngine::Init initializing every one of its kChordNumVoices
// square-wave oscillators.
//
// Init used to loop kChordNumNotes (4) over a kChordNumVoices (5) array, so
// voice_[4] ran on whatever its memory already held. On the module that is
// harmless (Voice lives in .bss, so the fifth oscillator starts zeroed), but a
// host that stack-allocates an engine — the preview renderers do — can hand it
// a garbage phase, and SuperSquareOscillator's transition loop exits only on a
// phase comparison. The failure is therefore a HANG, not a wrong sample, which
// is why this test arms a watchdog and places the engine in deliberately dirty
// memory rather than checking a return value.
//
// Build + run: plaits/test/chiptune_init_test.sh

#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <new>

#include "plaits/dsp/engine2/chiptune_engine.h"

#include "stmlib/utils/buffer_allocator.h"

using plaits::ChiptuneEngine;
using plaits::EngineParameters;
using plaits::TRIGGER_UNPATCHED;

namespace {

const size_t kBlock = 24;

char g_ram_block[16 * 1024];

// Storage for the engine, deliberately dirtied before construction so an
// uninitialized member reads garbage rather than a benign zero.
alignas(ChiptuneEngine) unsigned char g_engine_storage[sizeof(ChiptuneEngine)];

extern "C" void OnTimeout(int) {
  const char message[] =
      "FAIL: ChiptuneEngine::Render did not return within the watchdog window "
      "(an uninitialized oscillator is spinning in its transition loop)\n";
  ssize_t ignored = write(2, message, sizeof(message) - 1);
  (void) ignored;
  _exit(1);
}

}  // namespace

int main() {
  signal(SIGALRM, OnTimeout);
  alarm(20);

  // The "leftover memory" case, not the zeroed one. The byte pattern matters:
  // it must decode to a LARGE float, because the loop that hangs advances a
  // phase by 1.0 per iteration and exits on a comparison — a garbage phase that
  // happens to decode small (0xA5A5A5A5 is ~-2.9e-16, near zero) is benign and
  // reproduces nothing. 0x7F7F7F7F is ~3.4e38.
  memset(g_engine_storage, 0x7F, sizeof(g_engine_storage));
  ChiptuneEngine* engine = new (g_engine_storage) ChiptuneEngine();

  stmlib::BufferAllocator allocator(g_ram_block, sizeof(g_ram_block));
  engine->Init(&allocator);
  engine->Reset();

  EngineParameters p;
  p.note = 48.0f;
  p.accent = 0.8f;
  p.chord_set_option = 0;
  p.stereo = false;
  p.harmonics = 0.5f;
  p.timbre = 0.5f;
  p.morph = 0.5f;
  // The chord path (rather than the arpeggiator) runs when TRIG is unpatched,
  // and register_mode != 4 is where the fifth voice is rendered.
  p.trigger = TRIGGER_UNPATCHED;

  int non_finite = 0;
  // Sweep MACRO across every register mode, which is what selects how the five
  // voices are registered; 0.5 alone sits in the one mode that skips the path.
  for (int step = 0; step <= 50; ++step) {
    p.macro = static_cast<float>(step) / 50.0f;
    for (int block = 0; block < 40; ++block) {
      float out[kBlock];
      float aux[kBlock];
      bool already_enveloped = false;
      engine->Render(p, out, aux, kBlock, &already_enveloped);
      for (size_t i = 0; i < kBlock; ++i) {
        if (!std::isfinite(out[i])) ++non_finite;
      }
    }
  }

  alarm(0);
  engine->~ChiptuneEngine();

  if (non_finite) {
    printf("FAIL: %d non-finite samples\n", non_finite);
    return 1;
  }
  printf("OK: ChiptuneEngine initializes every chord voice\n");
  return 0;
}
