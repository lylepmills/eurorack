// Copyright 2026 Rubato Audio.
//
// Host check for a generated bank layout: build_config.h's PLAITS_BANK_SIZES and
// the 1..32 guard, plus the sum/max/count invariants the config generator
// guarantees (alt_firmwares/plaits_lab_builder). The firmware itself builds as
// C++98 and trusts the generated table, so this host-only check (using constexpr
// on a modern toolchain) stands in for a firmware-side static_assert.
//
//   g++ -std=c++11 -I<repo-root> plaits/test/bank_config_test.cc -o /tmp/bct && /tmp/bct

// A short-bank layout: green 8, red 3, amber 8 = 19 engines. The red bank is
// SPARSE — its three engines sit at physical rows 0, 2, 5 (gaps kept in place),
// so its PLAITS_ENGINE_ROWS entries are non-contiguous while navigation stays
// compact (indices 8, 9, 10).
#define PLAITS_ENGINE_COUNT 19
#define PLAITS_BANK_SIZES { 8, 3, 8 }
#define PLAITS_ENGINE_ROWS \
    { 0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 5, 0, 1, 2, 3, 4, 5, 6, 7 }

#include "plaits/build_config.h"
#include "plaits/bank_navigation.h"

#include <cassert>
#include <cstdio>

// ---- Mirror of the constexpr block in ui.cc ----
static constexpr uint8_t kBankSizes[] = PLAITS_BANK_SIZES;
static constexpr int kNumBanks = sizeof(kBankSizes) / sizeof(kBankSizes[0]);
static constexpr uint8_t kEngineRows[] = PLAITS_ENGINE_ROWS;
static_assert(sizeof(kEngineRows) / sizeof(kEngineRows[0]) == PLAITS_ENGINE_COUNT,
              "one physical row per engine");
static constexpr int SumBankSizes(int n) {
  return n == 0 ? 0 : kBankSizes[n - 1] + SumBankSizes(n - 1);
}
static constexpr uint8_t MaxBankSize(int n) {
  return n == 0 ? 0
      : (kBankSizes[n - 1] > MaxBankSize(n - 1) ? kBankSizes[n - 1]
                                                : MaxBankSize(n - 1));
}
static_assert(kNumBanks >= 1 && kNumBanks <= 4, "one to four banks");
static_assert(SumBankSizes(kNumBanks) == PLAITS_ENGINE_COUNT, "sizes sum to count");
static_assert(MaxBankSize(kNumBanks) <= 8, "each bank <= 8");

int main() {
  assert(kNumBanks == 3);
  assert(SumBankSizes(kNumBanks) == 19);
  assert(MaxBankSize(kNumBanks) == 8);
  // Sanity: the derived table drives bank_navigation.h consistently.
  assert(plaits::BankOfEngine(kBankSizes, kNumBanks, 10) == 1);   // last red
  assert(plaits::RowOfEngine(kBankSizes, kNumBanks, 10) == 2);    // compact row (navigation)
  // The LED lights the engine's PHYSICAL row (ui.cc uses kEngineRows), which for
  // a sparse bank differs from the compact RowOfEngine: red's three engines
  // (compact 8, 9, 10) sit at physical rows 0, 2, 5.
  assert(kEngineRows[8] == 0);
  assert(kEngineRows[9] == 2);
  assert(kEngineRows[10] == 5);
  std::printf("bank_config_test: constexpr layout {8,3,8}=19 + sparse rows validated\n");
  return 0;
}
