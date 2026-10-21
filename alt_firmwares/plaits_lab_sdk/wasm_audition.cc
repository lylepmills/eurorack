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

// The UI sends raw signed pot positions. Keep a small, absolute-off center
// region, then recover the remaining half-travel and use one simple macro:
// excursion grows quadratically while orbit speed grows only linearly.
const float kRandomizerDeadZone = 0.07f;

// Per-model Drift speeds; the range law is the same for every model (see
// RandomizedParameter).
struct ParameterRandomizerProfile {
  float near_rate;
  float far_rate;
};

struct RandomizerProfile {
  ParameterRandomizerProfile timbre;
  ParameterRandomizerProfile morph;
};

// plaits_lab.py resolves catalog-driven archetypes and per-model overrides into
// these compile-time values. Defaults keep direct harness builds conservative.
#ifndef PLAITS_LAB_RANDOMIZER_TIMBRE_NEAR_RATE
#define PLAITS_LAB_RANDOMIZER_TIMBRE_NEAR_RATE 0.000009f
#endif
#ifndef PLAITS_LAB_RANDOMIZER_TIMBRE_FAR_RATE
#define PLAITS_LAB_RANDOMIZER_TIMBRE_FAR_RATE 0.000006f
#endif
#ifndef PLAITS_LAB_RANDOMIZER_MORPH_NEAR_RATE
#define PLAITS_LAB_RANDOMIZER_MORPH_NEAR_RATE 0.000006f
#endif
#ifndef PLAITS_LAB_RANDOMIZER_MORPH_FAR_RATE
#define PLAITS_LAB_RANDOMIZER_MORPH_FAR_RATE 0.000003f
#endif

const RandomizerProfile kRandomizerProfile = {
  {
    PLAITS_LAB_RANDOMIZER_TIMBRE_NEAR_RATE,
    PLAITS_LAB_RANDOMIZER_TIMBRE_FAR_RATE,
  },
  {
    PLAITS_LAB_RANDOMIZER_MORPH_NEAR_RATE,
    PLAITS_LAB_RANDOMIZER_MORPH_FAR_RATE,
  },
};

// Four independent, autonomous chaotic orbits. There are no sampled targets,
// countdowns, or slew stages: each x/y/z state evolves continuously on every
// control block. This borrows the slow, bounded, never-settling behaviour of a
// Sloth without attempting to emulate its analogue circuit.
struct ChaosOrbit {
  float x;
  float y;
  float z;
};

ChaosOrbit g_timbre_near;
ChaosOrbit g_timbre_far;
ChaosOrbit g_morph_near;
ChaosOrbit g_morph_far;

float Absolute(float value) {
  return value < 0.0f ? -value : value;
}

void ResetChaosOrbit(ChaosOrbit* orbit, float x, float y, float z) {
  orbit->x = x;
  orbit->y = y;
  orbit->z = z;
}

// One Euler step of the Lorenz system. At these deliberately tiny dt values,
// it behaves as a continuously curved slow-control orbit while needing only
// multiplies and adds that are realistic on the Cortex-M4.
void AdvanceChaosOrbit(ChaosOrbit* orbit, float dt) {
  const float x = orbit->x;
  const float y = orbit->y;
  const float z = orbit->z;
  const float dx = 10.0f * (y - x);
  const float dy = x * (28.0f - z) - y;
  const float dz = x * y - 2.6666667f * z;
  orbit->x = x + dx * dt;
  orbit->y = y + dy * dt;
  orbit->z = z + dz * dt;
}

void SettleChaosOrbit(ChaosOrbit* orbit) {
  // Discard the deterministic startup transient and begin on the attractor.
  // This happens once in init(), never on the audio path.
  for (int i = 0; i < 20000; ++i) {
    AdvanceChaosOrbit(orbit, 0.001f);
  }
}

// Smoothly map an unbounded attractor coordinate into a bipolar control
// voltage. Unlike a hard clip, this retains motion near the orbit's extremes.
float SoftBipolar(float value, float scale) {
  const float normalized = value * scale;
  return normalized / (0.4f + Absolute(normalized));
}

float RandomizerPosition(float amount) {
  const float magnitude = Absolute(amount);
  if (magnitude <= kRandomizerDeadZone) return 0.0f;
  return (magnitude - kRandomizerDeadZone) /
      (1.0f - kRandomizerDeadZone);
}

float RandomizerExcursion(float amount) {
  const float position = RandomizerPosition(amount);
  return position * position;
}

float RandomizerRateMultiplier(float amount) {
  return 0.65f + 1.35f * RandomizerPosition(amount);
}

// The firmware's Near Drift normalisation and quantile map
// (plaits/dsp/parameter_randomizer.cc); keep the two in step.
const float kNearDriftNorm = 0.876f;
const float kDriftPeak = 0.8f;
const float kNearDriftQuantiles[33] = {
  -1.0000f, -0.9386f, -0.8955f, -0.8602f, -0.8277f, -0.7964f, -0.7652f,
  -0.7336f, -0.7007f, -0.6660f, -0.6289f, -0.5885f, -0.5441f, -0.4941f,
  -0.4371f, -0.3706f, -0.2911f, -0.1935f, -0.0695f, 0.0824f, 0.2183f,
  0.3345f, 0.4328f, 0.5159f, 0.5858f, 0.6452f, 0.6969f, 0.7432f, 0.7860f,
  0.8272f, 0.8696f, 0.9188f, 1.0000f,
};

float NearEven(float source) {
  const float* q = kNearDriftQuantiles;
  if (source <= q[0]) return -1.0f;
  if (source >= q[32]) return 1.0f;
  int low = 0;
  int high = 32;
  while (high - low > 1) {
    const int middle = (low + high) >> 1;
    if (q[middle] <= source) low = middle; else high = middle;
  }
  const float fraction = (source - q[low]) / (q[low + 1] - q[low]);
  return -1.0f + 2.0f * (static_cast<float>(low) + fraction) / 32.0f;
}

// The firmware's range law (ParameterRandomizer::ApplyVoltage): one reach for
// both sides, opening with depth toward the end the offset heads for; Near
// cubes its evenly spread source, so most values stay close to the knob
// setting and a few go far, while Far spreads them across the whole reach.
float RandomizedParameter(
    float base,
    float amount,
    const ChaosOrbit& near_orbit,
    const ChaosOrbit& far_orbit) {
  const float excursion = RandomizerExcursion(amount);
  if (excursion <= 0.0f) return base;
  const bool near = amount < 0.0f;
  // Near reads a compact z-axis undulation. Far reads the broad bipolar x-axis
  // motion between the orbit's two lobes.
  float voltage = near
      ? SoftBipolar(near_orbit.z - 25.0f, 0.05f) / kNearDriftNorm
      : SoftBipolar(far_orbit.x, 0.08f);
  voltage /= kDriftPeak;
  if (voltage < -1.0f) voltage = -1.0f;
  if (voltage > 1.0f) voltage = 1.0f;
  if (near) {
    voltage = NearEven(voltage);
    voltage = voltage * voltage * voltage;
  }
  const float symmetric_headroom = base < 1.0f - base ? base : 1.0f - base;
  const float directional_headroom =
      voltage < 0.0f ? base : 1.0f - base;
  const float headroom = symmetric_headroom +
      (directional_headroom - symmetric_headroom) * excursion;
  float value = base + voltage * headroom * excursion;
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
//
// PLUCKED is also TRIG patched, as on hardware and in render_model.cc: between
// strikes the engine sees TRIGGER_LOW, not TRIGGER_UNPATCHED. Engines with a
// distinct unpatched behavior otherwise never fell silent between strikes:
// the snares free-ran and the struck bell and drum droned.
// A PLUCKED strike holds TRIG high like a sequencer gate, since gate-aware
// engines (the DX7 banks, reed pipe) only sound while it is high.
#ifndef PLAITS_LAB_STRIKE_GATE_SAMPLES
#define PLAITS_LAB_STRIKE_GATE_SAMPLES 2400
#endif
enum EnvMode { ENV_SUSTAINED = 0, ENV_PLUCKED = 1 };
int g_env_mode = ENV_SUSTAINED;
int g_gate_remaining = 0;
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
  ResetChaosOrbit(&g_timbre_near, 1.0f, 1.0f, 20.0f);
  ResetChaosOrbit(&g_timbre_far, -8.0f, 7.0f, 27.0f);
  ResetChaosOrbit(&g_morph_near, 5.0f, -3.0f, 18.0f);
  ResetChaosOrbit(&g_morph_far, -4.0f, -6.0f, 24.0f);
  SettleChaosOrbit(&g_timbre_near);
  SettleChaosOrbit(&g_timbre_far);
  SettleChaosOrbit(&g_morph_near);
  SettleChaosOrbit(&g_morph_far);
  g_block_fill = 0;
  g_block_pos = 0;
  g_retrigger = false;
  g_gate_remaining = 0;
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
  g_gate_remaining = PLAITS_LAB_STRIKE_GATE_SAMPLES;
  if (g_env_mode == ENV_PLUCKED) g_env = 1.0f;
}

// Render `size` (<= kMaxRender) samples into g_out_main / g_out_aux.
void render(int size) {
  if (size > kMaxRender) size = kMaxRender;
  if (g_engine == nullptr) return;
  int produced = 0;
  while (produced < size) {
    if (g_block_fill == 0) {
      // 48 kHz / 12 samples = 4 kHz control rate. Intensity changes speed
      // gently (0.65x to 2x) while excursion follows the stronger square law.
      const float timbre_rate = RandomizerRateMultiplier(g_randomizer_timbre);
      const float morph_rate = RandomizerRateMultiplier(g_randomizer_morph);
      AdvanceChaosOrbit(
          &g_timbre_near, kRandomizerProfile.timbre.near_rate * timbre_rate);
      AdvanceChaosOrbit(
          &g_timbre_far, kRandomizerProfile.timbre.far_rate * timbre_rate);
      AdvanceChaosOrbit(
          &g_morph_near, kRandomizerProfile.morph.near_rate * morph_rate);
      AdvanceChaosOrbit(
          &g_morph_far, kRandomizerProfile.morph.far_rate * morph_rate);
      g_params.timbre = RandomizedParameter(
          g_base_timbre, g_randomizer_timbre,
          g_timbre_near, g_timbre_far);
      g_params.morph = RandomizedParameter(
          g_base_morph, g_randomizer_morph,
          g_morph_near, g_morph_far);
      if (g_retrigger) {
        g_params.trigger = TRIGGER_HIGH | TRIGGER_RISING_EDGE;
      } else if (g_env_mode == ENV_PLUCKED) {
        g_params.trigger = g_gate_remaining > 0 ? TRIGGER_HIGH : TRIGGER_LOW;
      } else {
        g_params.trigger = TRIGGER_UNPATCHED;
      }
      g_retrigger = false;
      g_gate_remaining -= static_cast<int>(kBlockSize);
      if (g_gate_remaining < 0) g_gate_remaining = 0;
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
