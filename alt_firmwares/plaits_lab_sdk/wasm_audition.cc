// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// WebAssembly audition harness for `plaits-lab dev`. Runs ONE engine instance
// and renders blocks on demand for a browser AudioWorklet — same engine and
// parameter contract as the native renderer (render_model.cc), but STATEFUL:
// no per-call Init/Reset, so it plays continuously and responds to live control
// changes with zero latency. Compiled per package to a standalone .wasm.

#include <cstddef>
#include <cstdint>
#include <new>

#include "plaits/dsp/dsp.h"

#ifndef PLAITS_LAB_ENGINE_HEADER
#error PLAITS_LAB_ENGINE_HEADER must name the package engine header
#endif
#ifndef PLAITS_LAB_ENGINE_CLASS
#error PLAITS_LAB_ENGINE_CLASS must name the package engine class
#endif
#include PLAITS_LAB_ENGINE_HEADER

using namespace plaits;

namespace {

// Engine memory. The engine is placement-new'd in init() rather than declared
// as a global object, so a standalone .wasm needs no static-constructor pass.
char g_allocator_memory[16 * 1024];
alignas(PLAITS_LAB_ENGINE_CLASS) char g_engine_storage[sizeof(PLAITS_LAB_ENGINE_CLASS)];
PLAITS_LAB_ENGINE_CLASS* g_engine = nullptr;

EngineParameters g_params;

// The engine renders in fixed kBlockSize (12) chunks; an audio quantum is 128,
// so we drain a one-block scratch buffer into arbitrary-length requests.
float g_block_main[kBlockSize];
float g_block_aux[kBlockSize];
int g_block_fill = 0;
int g_block_pos = 0;
bool g_retrigger = false;
bool g_block_already = false;  // did the last block self-envelope?

// Amplitude envelope emulating Plaits' low-pass gate on TRIG. The audition
// harness calls Engine::Render() DIRECTLY (no plaits::Voice), so a sustained
// engine that ignores parameters.trigger would otherwise never respond to a
// strike. PLUCKED mode opens this envelope on a strike and decays it to
// silence; SUSTAINED mode holds it open (a continuous drone for tweaking).
// Engines that shape their own amplitude (already_enveloped, e.g. the drums)
// bypass it so we never double-envelope them.
enum EnvMode { ENV_SUSTAINED = 0, ENV_PLUCKED = 1 };
int g_env_mode = ENV_SUSTAINED;
float g_env = 1.0f;
const float kEnvDecay = 0.99976f;  // ~0.6 s to -60 dB at 48 kHz

// Output buffers the worklet reads after render(n).
const int kMaxRender = 256;
float g_out_main[kMaxRender];
float g_out_aux[kMaxRender];

}  // namespace

extern "C" {

void init() {
  stmlib::BufferAllocator allocator(g_allocator_memory, sizeof(g_allocator_memory));
  g_engine = new (g_engine_storage) PLAITS_LAB_ENGINE_CLASS();
  g_engine->Init(&allocator);
  g_engine->LoadUserData(NULL);
  g_engine->Reset();
  g_params.note = 48.0f;
  g_params.harmonics = 0.5f;
  g_params.timbre = 0.5f;
  g_params.morph = 0.5f;
  g_params.macro = 0.5f;
  g_params.accent = 0.8f;
  g_params.chord_set_option = 0;
  g_params.trigger = TRIGGER_UNPATCHED;
  g_block_fill = 0;
  g_block_pos = 0;
  g_retrigger = false;
  g_block_already = false;
  g_env = 1.0f;
  g_env_mode = ENV_SUSTAINED;
}

// 0 = sustained (continuous drone), 1 = plucked (each strike opens the LPG and
// decays to silence). Switching to plucked opens the gate so it sounds at once.
void set_env_mode(int mode) {
  g_env_mode = mode ? ENV_PLUCKED : ENV_SUSTAINED;
  g_env = 1.0f;
}

void set_params(float note, float harmonics, float timbre, float morph, float macro) {
  g_params.note = note;
  g_params.harmonics = harmonics;
  g_params.timbre = timbre;
  g_params.morph = morph;
  g_params.macro = macro;
}

// Fire a single trigger rising edge on the next rendered block (re-strike).
// In plucked mode this also re-opens the LPG envelope.
void trigger() {
  g_retrigger = true;
  if (g_env_mode == ENV_PLUCKED) g_env = 1.0f;
}

// Render `size` (<= kMaxRender) samples into g_out_main / g_out_aux.
void render(int size) {
  if (size > kMaxRender) size = kMaxRender;
  if (g_engine == nullptr) return;
  int produced = 0;
  while (produced < size) {
    if (g_block_fill == 0) {
      g_params.trigger = g_retrigger
          ? static_cast<TriggerState>(TRIGGER_HIGH | TRIGGER_RISING_EDGE)
          : TRIGGER_UNPATCHED;
      g_retrigger = false;
      bool already = false;
      g_engine->Render(g_params, g_block_main, g_block_aux, kBlockSize, &already);
      g_block_already = already;
      g_block_fill = static_cast<int>(kBlockSize);
      g_block_pos = 0;
    }
    int take = size - produced;
    if (take > g_block_fill) take = g_block_fill;
    const bool apply_env = (g_env_mode == ENV_PLUCKED) && !g_block_already;
    for (int i = 0; i < take; ++i) {
      const float amp = apply_env ? g_env : 1.0f;
      g_out_main[produced + i] = g_block_main[g_block_pos + i] * amp;
      g_out_aux[produced + i] = g_block_aux[g_block_pos + i] * amp;
      if (apply_env) g_env *= kEnvDecay;
    }
    g_block_pos += take;
    g_block_fill -= take;
    produced += take;
  }
}

float* main_out() { return g_out_main; }
float* aux_out() { return g_out_aux; }

}  // extern "C"
