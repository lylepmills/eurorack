// Copyright 2026 Rubato Audio.
//
// Host checks for rewritable user-data regions (plaits/user_data.h): FM regions
// keyed by bank, custom Terrain/Wavetable regions keyed by palette slot, the tag
// separating transferred data from the baked seed in the same flash, and the
// patch count a transferred FM bank declares.
//
// The point of the feature is the case a single-region build cannot express:
// two banks holding DIFFERENT transferred data at the same time, instead of one
// tag that moves and silently reverts whichever bank held it last.
//
//   g++ -std=c++11 -DTEST -I<repo-root> plaits/test/user_data_region_test.cc \
//       -o /tmp/udrt && /tmp/udrt

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "plaits/user_data_region.h"

// Exercise the mixed case: replaceable FM banks and custom-model regions are
// independent recipe features, but may coexist in one generated build.
#define PLAITS_HAS_USER_DATA_BANK 1
#define PLAITS_HAS_SWAPPABLE_USER_DATA_BANKS 1
#define PLAITS_HAS_CUSTOM_MODEL_DATA 1

namespace plaits {

enum { kRegionSize = 0x1000 };

// Stands in for the generator's output. Six slots: two mapped to bank 0 (the
// shared-factory-bank case), one to bank 1, two independently rewritable model
// slots, and one ordinary non-FM slot that retains the legacy region.
static uint8_t bank_0[kRegionSize];
static uint8_t bank_1[kRegionSize];
static uint8_t terrain_0[kRegionSize];
static uint8_t wavetable_0[kRegionSize];
// Bank 2 exists in the engine table but was stripped, so it owns no region.
static const int8_t kEngineUserDataBank[6] = { 0, 0, 1, -1, -1, -1 };
static const uint8_t* const kEngineCustomModelData[6] = {
  NULL, NULL, NULL, terrain_0, wavetable_0, NULL,
};
static const UserDataRegion kUserDataRegions[2] = { { bank_0, 0 }, { bank_1, 1 } };
static const int kNumUserDataRegions = 2;

}  // namespace plaits

#include "plaits/user_data.h"

using plaits::UserData;
using plaits::bank_0;
using plaits::bank_1;
using plaits::kRegionSize;
using plaits::terrain_0;
using plaits::wavetable_0;

namespace {

// What Save writes: a payload tagged for `bank`, accepting any slot.
void Tag(uint8_t* region, int bank) {
  region[kRegionSize - 2] = 'U';
  region[kRegionSize - 1] = ' ' + bank;
}

void FillPayload(uint8_t* payload, uint8_t first_byte) {
  std::memset(payload, 0, kRegionSize);
  payload[0] = first_byte;
  payload[kRegionSize - 2] = 0;    // slotMin — the encoder accepts every slot
  payload[kRegionSize - 1] = 31;   // slotMax
}

// The TEST flash mock narrates every programmed word — 1024 lines per Save.
// Redirect the fd rather than freopen()ing stdout: restoring via /dev/tty only
// works under a terminal, and when it silently fails the test's own result line
// disappears with it.
void Quietly(void (*body)()) {
  std::fflush(stdout);
  int saved = dup(STDOUT_FILENO);
  int sink = open("/dev/null", O_WRONLY);
  assert(saved >= 0 && sink >= 0);
  dup2(sink, STDOUT_FILENO);
  body();
  std::fflush(stdout);
  dup2(saved, STDOUT_FILENO);
  close(sink);
  close(saved);
}

UserData user_data;
uint8_t payload[kRegionSize];
bool save_result;
int save_slot;

void DoSave() { save_result = user_data.Save(payload, save_slot); }

}  // namespace

int main() {
  // ---- Nothing transferred: a baked bank must not read as a transferred one.
  std::memset(bank_0, 0, kRegionSize);
  std::memset(bank_1, 0, kRegionSize);
  assert(user_data.ptr(0) == NULL);
  assert(user_data.ptr(2) == NULL);

  // ---- THE feature: two banks hold different transferred data at once.
  Tag(bank_0, 0);
  Tag(bank_1, 1);
  bank_0[0] = 0xaa;
  bank_1[0] = 0xbb;
  assert(user_data.ptr(0) == bank_0);   // slot 0 -> bank 0
  assert(user_data.ptr(2) == bank_1);   // slot 2 -> bank 1
  assert(user_data.ptr(0)[0] == 0xaa);
  assert(user_data.ptr(2)[0] == 0xbb);

  // ---- Regions are keyed on BANK, not slot. Slots 0 and 1 share factory bank
  // 0, so a transfer through either must be visible to both. Keying on slot
  // would leave slot 1 reading the new bytes at the old bank's declared length.
  assert(user_data.ptr(1) == bank_0);

  // ---- A tag for the wrong bank reads as absent, so the firmware falls back to
  // the baked bank rather than handing one bank's patches to another.
  Tag(bank_1, 0);
  assert(user_data.ptr(2) == NULL);
  Tag(bank_1, 1);

  // ---- Customized model slots own independent regions, keyed by SLOT. Their
  // untagged baked seeds read through Voice's fallback; after transfer, ptr()
  // exposes the rewritten region and it wins over that seed fallback.
  std::memset(terrain_0, 0, kRegionSize);
  std::memset(wavetable_0, 0, kRegionSize);
  assert(user_data.ptr(3) == NULL);
  assert(user_data.ptr(4) == NULL);
  Tag(terrain_0, 3);
  Tag(wavetable_0, 4);
  terrain_0[0] = 0x33;
  wavetable_0[0] = 0x44;
  assert(user_data.ptr(3) == terrain_0);
  assert(user_data.ptr(4) == wavetable_0);
  assert(user_data.ptr(3)[0] == 0x33);
  assert(user_data.ptr(4)[0] == 0x44);

  // Saving another payload for one model tags that slot, not an FM bank, and
  // does not address the other model's region.
  FillPayload(payload, 0x55);
  save_slot = 3;
  Quietly(DoSave);
  assert(save_result);
  assert(payload[kRegionSize - 2] == 'U');
  assert(payload[kRegionSize - 1] == ' ' + 3);

  // ---- An uncustomized non-FM slot keeps the module's legacy area. Slot 5 is
  // such a slot, so old terrain/wavetable transfer files still work there.
  assert(user_data.ptr(5) == NULL);          // nothing transferred there yet
  FillPayload(payload, 0x11);
  save_slot = 5;
  Quietly(DoSave);
  assert(save_result);
  assert(payload[kRegionSize - 2] == 'U');
  assert(payload[kRegionSize - 1] == ' ' + 5);   // legacy region: tagged by SLOT

  // ---- Save still honours the payload's declared slot range.
  FillPayload(payload, 0x11);
  payload[kRegionSize - 2] = 5;
  payload[kRegionSize - 1] = 9;
  save_slot = 0;
  Quietly(DoSave);
  assert(!save_result);

  // ---- An owned slot is accepted, and tagged with its BANK.
  FillPayload(payload, 0x11);
  save_slot = 2;              // slot 2 -> bank 1
  Quietly(DoSave);
  assert(save_result);
  assert(payload[kRegionSize - 2] == 'U');
  assert(payload[kRegionSize - 1] == ' ' + 1);

  // ---- Patch count declared by a transferred bank.
  uint8_t bank[kRegionSize];
  std::memset(bank, 0, kRegionSize);
  // No magic -> a full 32 patches. This is the compatibility guard: a payload
  // from the encoder that predates the count must keep reading as 32.
  assert(UserData::bank_length(bank) == 0x1000);
  bank[kRegionSize - 4] = 0xa5;
  bank[kRegionSize - 3] = 8;
  assert(UserData::bank_length(bank) == 8 * 128);
  bank[kRegionSize - 3] = 1;
  assert(UserData::bank_length(bank) == 128);
  bank[kRegionSize - 3] = 32;
  assert(UserData::bank_length(bank) == 0x1000);
  // Out of range -> fall back to a full bank rather than trusting it.
  bank[kRegionSize - 3] = 0;
  assert(UserData::bank_length(bank) == 0x1000);
  bank[kRegionSize - 3] = 33;
  assert(UserData::bank_length(bank) == 0x1000);
  bank[kRegionSize - 3] = 255;
  assert(UserData::bank_length(bank) == 0x1000);
  // A plausible name character where the magic should be is NOT a count. This
  // is why the magic exists: printable ASCII is 32..127, so without it a stray
  // 1..31 at SIZE-3 would silently shorten someone's bank.
  bank[kRegionSize - 4] = 'S';
  bank[kRegionSize - 3] = 4;
  assert(UserData::bank_length(bank) == 0x1000);
  assert(UserData::bank_length(NULL) == 0x1000);

  std::printf("user_data_region_test: simultaneous FM and custom-model regions, "
              "bank/slot tags, Save refusals, and patch counts validated\n");
  return 0;
}
