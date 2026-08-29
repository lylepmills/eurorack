// Copyright 2026 Rubato Audio. SPDX-License-Identifier: MIT
// Standalone host regression for the production algorithm-32 carrier tilt.
// Compile against a generated three-bank diagnostic engine_config.h so the
// actual SixOpEngine and the same baked voices used on hardware are exercised.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "plaits/dsp/engine2/six_op_engine.h"
#include "stmlib/utils/random.h"

using namespace plaits;

static char memory[65536];

std::vector<float> Render(
    const uint8_t* data,
    bool tilt,
    float timbre,
    bool drone,
    float note = 48.0f,
    float velocity = 1.0f) {
  std::memset(memory, 0, sizeof(memory));
  stmlib::BufferAllocator allocator(memory, sizeof(memory));
  SixOpEngine engine;
  stmlib::Random::Seed(0x12345678);
  engine.Init(&allocator);
  engine.LoadUserData(data, 128);
  engine.Reset();
  engine.set_carrier_timbre_enabled(tilt);
  EngineParameters p = {};
  p.note = note;
  p.accent = velocity;
  p.timbre = timbre;
  p.morph = 0.5f;
  p.macro = 0.5f;
  std::vector<float> pcm;
  for (int block = 0; block < 1000; ++block) {
    p.trigger = drone
        ? TRIGGER_UNPATCHED
        : (block == 0
            ? TRIGGER_RISING_EDGE | TRIGGER_HIGH
            : block < 500 ? TRIGGER_HIGH : 0);
    float out[12];
    float aux[12];
    bool enveloped = false;
    engine.Render(p, out, aux, 12, &enveloped);
    for (int i = 0; i < 12; ++i) {
      assert(std::isfinite(out[i]) && std::fabs(out[i]) <= 1.01f);
      pcm.push_back(out[i]);
    }
  }
  return pcm;
}

void DisableAllButOneCarrier(uint8_t* data) {
  for (int i = 0; i < 6; ++i) data[i * 17 + 14] = i == 0 ? 90 : 0;
}

void MakeEveryCarrierUnison(uint8_t* data) {
  for (int i = 0; i < 6; ++i) {
    data[i * 17 + 15] = 2;  // ratio mode, coarse = 1
    data[i * 17 + 16] = 0;
  }
}

int main() {
  assert(PLAITS_ENGINE_COUNT == 3);
  int patches = 0;
  for (int engine_index = 0; engine_index < PLAITS_ENGINE_COUNT; ++engine_index) {
    const int bank = kEngineUserDataBank[engine_index];
    const uint8_t* data = kResolvedUserDataBank[bank];
    const size_t length = kResolvedUserDataBankSize[bank];
    assert(data && length && !(length % 128));
    for (size_t offset = 0; offset < length; offset += 128) {
      const uint8_t* patch = data + offset;
      assert((patch[110] & 31) == 31);
      for (int drone = 0; drone < 2; ++drone) {
        const std::vector<float> stock_center = Render(patch, false, 0.5f, drone);
        assert(Render(patch, true, 0.5f, drone) == stock_center);
        assert(Render(patch, false, 0.0f, drone) ==
            Render(patch, false, 1.0f, drone));
        assert(Render(patch, true, 0.0f, drone) !=
            Render(patch, true, 1.0f, drone));
      }
      // Exercise the fixed/ratio ordering and headroom law across the playable
      // range. Exact spectral expectations belong to the WASM audio audit;
      // this gate catches instability and out-of-bounds output in the firmware.
      for (float note = 12.0f; note <= 108.0f; note += 32.0f) {
        Render(patch, true, 0.0f, true, note, 0.1f);
        Render(patch, true, 1.0f, true, note, 1.0f);
      }
      ++patches;
    }
  }

  // Non-32 algorithms bypass Tilt exactly.
  uint8_t edge[128];
  const uint8_t* first = kResolvedUserDataBank[kEngineUserDataBank[0]];
  std::memcpy(edge, first, sizeof(edge));
  edge[110] = (edge[110] & ~31) | 0;
  for (float timbre = 0.0f; timbre <= 1.0f; timbre += 0.25f) {
    assert(Render(edge, true, timbre, true) ==
        Render(edge, false, timbre, true));
  }

  // Degenerate algorithm-32 voices remain stable no-ops rather than inventing
  // arbitrary carrier ordering.
  std::memcpy(edge, first, sizeof(edge));
  DisableAllButOneCarrier(edge);
  assert(Render(edge, true, 0.0f, true) == Render(edge, true, 1.0f, true));
  std::memcpy(edge, first, sizeof(edge));
  MakeEveryCarrierUnison(edge);
  assert(Render(edge, true, 0.0f, true) == Render(edge, true, 1.0f, true));

  std::printf(
      "PASS: %d algorithm-32 patches preserve stock noon, move at the ends, "
      "stay bounded across pitch/velocity, and bypass all edge no-op cases.\n",
      patches);
}
