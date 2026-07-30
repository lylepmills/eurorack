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
#include "plaits/resources.h"

#ifndef PLAITS_LAB_ENGINE_HEADER
#error PLAITS_LAB_ENGINE_HEADER must name the package engine header
#endif
#ifndef PLAITS_LAB_ENGINE_CLASS
#error PLAITS_LAB_ENGINE_CLASS must name the package engine class
#endif
#include PLAITS_LAB_ENGINE_HEADER

#ifndef PLAITS_LAB_USER_DATA_BANK
#define PLAITS_LAB_USER_DATA_BANK -1
#endif

using namespace plaits;

namespace {

// Engine memory. The engine is placement-new'd in init() rather than declared
// as a global object, so a standalone .wasm needs no static-constructor pass.
char g_allocator_memory[16 * 1024];
alignas(PLAITS_LAB_ENGINE_CLASS) char g_engine_storage[sizeof(PLAITS_LAB_ENGINE_CLASS)];
PLAITS_LAB_ENGINE_CLASS* g_engine = nullptr;

EngineParameters g_params;
float g_base_timbre = 0.5f;
float g_base_morph = 0.5f;
float g_randomizer_timbre = 0.0f;
float g_randomizer_morph = 0.0f;

// Four independent smooth random-voltage sources. "Near" uses a compact,
// center-heavy distribution; "Far" glides between uniform destinations. The
// time constants differ for TIMBRE and MORPH so texture moves faster than the
// more structural MORPH parameter.
struct SmoothRandomSource {
  float value;
  float target;
  int blocks_until_target;
};

SmoothRandomSource g_timbre_near;
SmoothRandomSource g_timbre_far;
SmoothRandomSource g_morph_near;
SmoothRandomSource g_morph_far;
uint32_t g_random_state = 0x6d2b79f5u;

float RandomUnit() {
  uint32_t x = g_random_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_random_state = x;
  return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

float RandomBipolar() {
  return RandomUnit() * 2.0f - 1.0f;
}

float PeakyBipolar() {
  return (RandomUnit() + RandomUnit() + RandomUnit()) * (2.0f / 3.0f) - 1.0f;
}

void ResetRandomSource(SmoothRandomSource* source) {
  source->value = 0.0f;
  source->target = 0.0f;
  source->blocks_until_target = 1;
}

void AdvanceRandomSource(
    SmoothRandomSource* source,
    int minimum_blocks,
    int variable_blocks,
    float slew,
    bool peaky) {
  --source->blocks_until_target;
  if (source->blocks_until_target <= 0) {
    source->target = peaky ? PeakyBipolar() : RandomBipolar();
    source->blocks_until_target = minimum_blocks +
        static_cast<int>(RandomUnit() * static_cast<float>(variable_blocks));
  }
  source->value += (source->target - source->value) * slew;
}

float RandomizedParameter(
    float base,
    float amount,
    const SmoothRandomSource& near_source,
    const SmoothRandomSource& far_source) {
  if (amount > -0.0001f && amount < 0.0001f) return base;
  const float depth = amount < 0.0f ? -amount : amount;
  // Near deliberately occupies less of the available headroom even at full
  // depth. Its center-heavy targets make it a quiver, not a range explorer.
  const float voltage = amount < 0.0f
      ? near_source.value * 0.45f
      : far_source.value;
  const float headroom = voltage < 0.0f ? base : 1.0f - base;
  float value = base + voltage * headroom * depth;
  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;
  return value;
}

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
#if PLAITS_LAB_USER_DATA_BANK >= 0
  g_engine->LoadUserData(plaits::fm_patches_table[PLAITS_LAB_USER_DATA_BANK]);
#else
  g_engine->LoadUserData(NULL);
#endif
  g_engine->Reset();
  g_params.note = 48.0f;
  g_params.harmonics = 0.5f;
  g_params.timbre = 0.5f;
  g_params.morph = 0.5f;
  g_params.macro = 0.5f;
  g_params.accent = 0.8f;
  g_params.chord_set_option = 0;
  g_params.trigger = TRIGGER_UNPATCHED;
  g_params.stereo = false;
  g_base_timbre = 0.5f;
  g_base_morph = 0.5f;
  g_randomizer_timbre = 0.0f;
  g_randomizer_morph = 0.0f;
  g_random_state = 0x6d2b79f5u;
  ResetRandomSource(&g_timbre_near);
  ResetRandomSource(&g_timbre_far);
  ResetRandomSource(&g_morph_near);
  ResetRandomSource(&g_morph_far);
  g_block_fill = 0;
  g_block_pos = 0;
  g_retrigger = false;
  g_block_already = false;
  g_env = 1.0f;
  g_env_mode = ENV_SUSTAINED;
}

// Request a true stereo render (OUT=left, AUX=right) — only meaningful for an
// engine that reports stereo_capable() and honors parameters.stereo; a mono
// engine ignores it. Lets the audition exercise the same stereo path the voice
// drives on hardware when the palette turns stereo on for this model.
void set_stereo(int on) { g_params.stereo = on ? true : false; }

// 1 if the loaded engine overrides stereo_capable() to true (it renders a real
// L/R pair when parameters.stereo is set); 0 for a mono engine. The audition UI
// uses this to show whether stereo is available for this model.
int stereo_capable() {
  return (g_engine != nullptr && g_engine->stereo_capable()) ? 1 : 0;
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
  g_base_timbre = timbre;
  g_base_morph = morph;
  g_params.macro = macro;
}

// Signed attenuverter positions: negative selects the close, center-heavy
// process; positive selects the wide roaming process; zero is exactly off.
void set_randomizer_amounts(float timbre, float morph) {
  if (timbre < -1.0f) timbre = -1.0f;
  if (timbre > 1.0f) timbre = 1.0f;
  if (morph < -1.0f) morph = -1.0f;
  if (morph > 1.0f) morph = 1.0f;
  g_randomizer_timbre = timbre;
  g_randomizer_morph = morph;
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
      // 48 kHz / 12 samples = 4 kHz control rate. Target intervals and slew
      // constants are intentionally parameter-specific listening choices.
      AdvanceRandomSource(&g_timbre_near, 320, 960, 0.006f, true);
      AdvanceRandomSource(&g_timbre_far, 2400, 4800, 0.001f, false);
      AdvanceRandomSource(&g_morph_near, 1200, 2400, 0.0025f, true);
      AdvanceRandomSource(&g_morph_far, 5200, 6800, 0.0005f, false);
      g_params.timbre = RandomizedParameter(
          g_base_timbre, g_randomizer_timbre, g_timbre_near, g_timbre_far);
      g_params.morph = RandomizedParameter(
          g_base_morph, g_randomizer_morph, g_morph_near, g_morph_far);
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

// Audition-only telemetry. These expose the parameters actually supplied to the
// engine, allowing the browser to distinguish subtle synthesis response from a
// broken control/message path.
float current_timbre() { return g_params.timbre; }
float current_morph() { return g_params.morph; }

}  // extern "C"
