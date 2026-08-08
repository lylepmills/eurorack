// Copyright 2026 Rubato Audio.
//
// Host check for a Plaits Palette build that did NOT opt in to replaceable FM
// banks — the DEFAULT, and therefore the shape almost every build has.
//
// The point is that opting out costs nothing and changes nothing: every slot,
// FM or not, resolves to the module's single legacy 0x08007000 area and is
// transferable exactly as it has always been. Replaceable banks are a pure
// addition on top.
//
// This matters because the default was inverted late: page-aligning the banks
// costs ~832 bytes, and the stock 24-model preset has under 800 bytes spare, so
// defaulting it ON pushed the DEFAULT build past the flash limit. Having made it
// opt-in, "off" must mean the historical behaviour rather than a third state
// that quietly refuses transfers.
//
//   g++ -std=c++11 -DTEST -I<repo-root> plaits/test/user_data_default_test.cc \
//       -o /tmp/uddt && /tmp/uddt

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "plaits/user_data_region.h"

// What the generator emits without the opt-in: the engine bank table is still
// there, but no regions and no replaceable banks.
#define PLAITS_HAS_USER_DATA_BANK 1
#define PLAITS_HAS_SWAPPABLE_USER_DATA_BANKS 0

namespace plaits {

enum { kRegionSize = 0x1000 };
// Slots 0 and 1 are FM bank engines; slot 2 reads user data but has no bank.
static const int8_t kEngineUserDataBank[3] = { 0, 1, -1 };

}  // namespace plaits

#include "plaits/user_data.h"

using plaits::UserData;
using plaits::kRegionSize;

namespace {

UserData user_data;
uint8_t payload[kRegionSize];
bool save_result;
int save_slot;

void DoSave() { save_result = user_data.Save(payload, save_slot); }

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

void FillPayload() {
  std::memset(payload, 0, kRegionSize);
  payload[kRegionSize - 2] = 0;
  payload[kRegionSize - 1] = 31;
}

}  // namespace

int main() {
  // ---- EVERY slot resolves to the legacy region and accepts a transfer: the
  // two FM bank slots and the non-FM one alike. A build that did not opt in must
  // behave exactly as it did before replaceable banks existed.
  for (int slot = 0; slot < 3; ++slot) {
    FillPayload();
    save_slot = slot;
    Quietly(DoSave);
    assert(save_result);
    // Tagged by SLOT, as the single-region firmware has always done — not by
    // bank, which is only meaningful once each bank owns its own flash.
    assert(payload[kRegionSize - 2] == 'U');
    assert(payload[kRegionSize - 1] == ' ' + slot);
  }

  // ---- The payload's declared slot range is still honoured.
  FillPayload();
  payload[kRegionSize - 2] = 5;
  payload[kRegionSize - 1] = 9;
  save_slot = 0;
  Quietly(DoSave);
  assert(!save_result);

  std::printf("user_data_default_test: without the opt-in every slot keeps the "
              "legacy region, tagged by slot\n");
  return 0;
}
