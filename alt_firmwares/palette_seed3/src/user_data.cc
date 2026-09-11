// Built-in user data for the four engines that are inert without it.
#include "fw_voice.h"

namespace palette_fw {

namespace {
int8_t g_terrain[4096];
bool g_terrain_built = false;
}  // namespace

const uint8_t* DefaultWaveTerrain() {
  if (!g_terrain_built) {
    // Bit-for-bit the surface in plaits_test.cc; keep the float math and cast
    // order exactly as written.
    for (int x = 0; x < 64; ++x) {
      for (int y = 0; y < 64; ++y) {
        g_terrain[x + 64 * y] =
            static_cast<int8_t>(127.0f * sinf((x * y) / 300.0f));
      }
    }
    g_terrain_built = true;
  }
  return reinterpret_cast<const uint8_t*>(g_terrain);
}

const uint8_t* BuiltInUserData(UserData kind) {
  switch (kind) {
    case kFmBankA: return plaits::fm_patches_table[0];
    case kFmBankB: return plaits::fm_patches_table[1];
    case kFmBankC: return plaits::fm_patches_table[2];
    case kWaveTerrain: return DefaultWaveTerrain();
    case kNone: break;
  }
  return nullptr;
}

}  // namespace palette_fw
