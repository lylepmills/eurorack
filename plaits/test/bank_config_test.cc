// Copyright 2026 Rubato Audio.
//
// Host compile+run check for the compile-time bank layout: build_config.h's
// PLAITS_BANK_SIZES / guard and the constexpr validators mirrored from ui.cc.
// This is what the ARM build would enforce at compile time; here it also lets us
// confirm the derivation on a normal host toolchain.
//
//   g++ -std=c++11 -I<repo-root> plaits/test/bank_config_test.cc -o /tmp/bct && /tmp/bct

// A short-bank layout: green 8, red 3, amber 8 = 19 engines.
#define PLAITS_ENGINE_COUNT 19
#define PLAITS_BANK_SIZES { 8, 3, 8 }

#include "plaits/build_config.h"
#include "plaits/bank_navigation.h"

#include <cassert>
#include <cstdio>

// ---- Mirror of the constexpr block in ui.cc ----
static constexpr uint8_t kBankSizes[] = PLAITS_BANK_SIZES;
static constexpr int kNumBanks = sizeof(kBankSizes) / sizeof(kBankSizes[0]);
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
  assert(plaits::RowOfEngine(kBankSizes, kNumBanks, 10) == 2);
  std::printf("bank_config_test: constexpr layout {8,3,8}=19 validated\n");
  return 0;
}
