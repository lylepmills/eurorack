// Copyright 2026 Rubato Audio.
//
// Host check for a Plaits Palette build with the Advanced "lock FM banks"
// preference set: FM banks are emitted at their natural length, unaligned, and
// no FM bank can be replaced over TIMBRE.
//
// The refusal has to be explicit. Falling back to the module's legacy
// 0x08007000 area would still ACCEPT a bank — writing it into an area no FM
// slot in this build ever reads — so the transfer would report success and do
// nothing, which is worse than refusing.
//
// A slot that reads user data but has no BANK (wavetable, wave terrain) keeps
// the legacy area either way: locking FM banks is not meant to disable those,
// and they have no baked blob that could have been made rewritable.
//
//   g++ -std=c++11 -DTEST -I<repo-root> plaits/test/user_data_locked_test.cc \
//       -o /tmp/udlt && /tmp/udlt

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "plaits/user_data_region.h"

// What the generator emits for a locked build: the engine bank table is still
// there, but no regions and no swappable banks.
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
  // ---- An FM bank slot is refused outright. Nothing is erased, and the module
  // shows its existing transfer-error state.
  for (int slot = 0; slot < 2; ++slot) {
    FillPayload();
    save_slot = slot;
    Quietly(DoSave);
    assert(!save_result);
    // The payload's slot range is left untouched, i.e. Save bailed before
    // stamping a tag — it did not write anywhere and then report failure.
    assert(payload[kRegionSize - 2] == 0);
    assert(payload[kRegionSize - 1] == 31);
    assert(user_data.ptr(slot) == NULL);
  }

  // ---- A non-FM user-data slot still resolves to the legacy region, so custom
  // wavetable / wave-terrain data keeps working exactly as on stock firmware.
  FillPayload();
  save_slot = 2;
  Quietly(DoSave);
  assert(save_result);
  assert(payload[kRegionSize - 2] == 'U');
  assert(payload[kRegionSize - 1] == ' ' + 2);

  std::printf("user_data_locked_test: FM transfers refused without writing, "
              "non-FM user data unaffected\n");
  return 0;
}
