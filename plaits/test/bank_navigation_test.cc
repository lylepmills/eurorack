// Copyright 2026 Rubato Audio.
//
// Host test for plaits/bank_navigation.h — the pure banked-navigation math with
// per-bank memory (design "B"). No hardware dependency; build and run with:
//
//   g++ -std=c++11 -I<repo-root> plaits/test/bank_navigation_test.cc -o /tmp/bnt \
//       && /tmp/bnt

#include "plaits/bank_navigation.h"

#include <cassert>
#include <cstdio>

namespace plaits {

// Mirrors Ui::Navigate's bookkeeping: after any move, remember the row within
// whichever bank we landed in, so change-bank can restore it later.
struct NavSim {
  const uint8_t* sizes;
  int num_banks;
  int engine;
  uint8_t last_row[4];

  int bank() const { return BankOfEngine(sizes, num_banks, engine); }
  int row() const { return RowOfEngine(sizes, num_banks, engine); }

  void RememberRow() { last_row[bank()] = static_cast<uint8_t>(row()); }

  void StepWithin() {
    engine = StepWithinBank(sizes, num_banks, engine);
    RememberRow();
  }
  void Change() {
    engine = ChangeBank(sizes, num_banks, engine, last_row);
    RememberRow();
  }
};

static NavSim MakeSim(const uint8_t* sizes, int num_banks, int engine) {
  NavSim s;
  s.sizes = sizes;
  s.num_banks = num_banks;
  s.engine = engine;
  for (int i = 0; i < 4; ++i) s.last_row[i] = 0;
  s.RememberRow();
  return s;
}

}  // namespace plaits

using namespace plaits;

static int checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { \
  std::printf("FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while (0)

int main() {
  // --- Bank/row/offset decomposition for a mixed layout {8,3,8} = 19 engines.
  {
    const uint8_t sizes[] = {8, 3, 8};
    CHECK(BankOffset(sizes, 0) == 0);
    CHECK(BankOffset(sizes, 1) == 8);
    CHECK(BankOffset(sizes, 2) == 11);
    CHECK(BankOfEngine(sizes, 3, 0) == 0);
    CHECK(BankOfEngine(sizes, 3, 7) == 0);
    CHECK(BankOfEngine(sizes, 3, 8) == 1);   // first red
    CHECK(BankOfEngine(sizes, 3, 10) == 1);  // last red
    CHECK(BankOfEngine(sizes, 3, 11) == 2);  // first amber
    CHECK(BankOfEngine(sizes, 3, 18) == 2);  // last amber
    CHECK(RowOfEngine(sizes, 3, 8) == 0);
    CHECK(RowOfEngine(sizes, 3, 10) == 2);
    CHECK(RowOfEngine(sizes, 3, 11) == 0);
  }

  // --- Within-bank stepping wraps at the bank's REAL size, not row 7.
  {
    const uint8_t sizes[] = {8, 3, 8};
    // Red bank (engines 8,9,10) is short: 8 -> 9 -> 10 -> 8.
    NavSim s = MakeSim(sizes, 3, 8);
    s.StepWithin(); CHECK(s.engine == 9);
    s.StepWithin(); CHECK(s.engine == 10);
    s.StepWithin(); CHECK(s.engine == 8);   // wraps after 3, not 8
    // A full bank still cycles all eight: green 7 -> 0.
    NavSim g = MakeSim(sizes, 3, 7);
    g.StepWithin(); CHECK(g.engine == 0);
  }

  // --- THE phantom-row case: changing into a shorter bank from a row it lacks.
  //     Green row 6, red only has rows 0..2. B lands on red's remembered row,
  //     never a nonexistent row 6.
  {
    const uint8_t sizes[] = {8, 3, 8};
    NavSim s = MakeSim(sizes, 3, 6);   // Green row 6
    // Red has never been visited -> remembered row 0.
    s.Change();
    CHECK(s.bank() == 1);
    CHECK(s.engine == 8);              // red row 0, NOT red row 6 (doesn't exist)
  }

  // --- Per-bank memory: each bank remembers its own row across round-trips.
  {
    const uint8_t sizes[] = {8, 3, 8};
    NavSim s = MakeSim(sizes, 3, 0);   // Green row 0
    // Move to green row 5.
    for (int i = 0; i < 5; ++i) s.StepWithin();
    CHECK(s.engine == 5);
    // Change to red, step to red row 2.
    s.Change(); CHECK(s.engine == 8);          // red row 0
    s.StepWithin(); s.StepWithin(); CHECK(s.engine == 10);  // red row 2
    // Change to amber (row 0), then back around to green: green restored to 5.
    s.Change(); CHECK(s.bank() == 2); CHECK(s.engine == 11);  // amber row 0
    s.Change(); CHECK(s.bank() == 0); CHECK(s.engine == 5);   // green REMEMBERED 5
    // And red still remembers row 2.
    s.Change(); CHECK(s.bank() == 1); CHECK(s.engine == 10);  // red REMEMBERED 2
  }

  // --- Empty bank is skipped by change-bank: {8,0,8}, red empty.
  {
    const uint8_t sizes[] = {8, 0, 8};
    NavSim s = MakeSim(sizes, 3, 3);   // green row 3
    s.Change();
    CHECK(s.bank() == 2);              // skipped empty red -> amber
    CHECK(s.engine == 8);             // amber row 0 (offset 8+0=8)
    s.Change();
    CHECK(s.bank() == 0);             // wraps back to green, skipping red again
    CHECK(s.engine == 3);            // green remembered row 3
  }

  // --- Four banks incl. a short orange: {8,8,8,2}. Change wraps orange->green.
  {
    const uint8_t sizes[] = {8, 8, 8, 2};
    NavSim s = MakeSim(sizes, 4, 24);  // orange row 0 (offset 24)
    CHECK(s.bank() == 3);
    s.StepWithin(); CHECK(s.engine == 25);  // orange row 1
    s.StepWithin(); CHECK(s.engine == 24);  // wraps after 2
    s.Change(); CHECK(s.bank() == 0); CHECK(s.engine == 0);  // -> green
  }

  // --- Clamp guard: a remembered row past a (shrunk) bank's size is clamped.
  {
    const uint8_t sizes[] = {8, 3, 8};
    uint8_t last_row[4] = {0, 7, 0, 0};  // red remembers row 7, but red size is 3
    int e = ChangeBank(sizes, 3, 0, last_row);  // from green -> red
    CHECK(BankOfEngine(sizes, 3, e) == 1);
    CHECK(RowOfEngine(sizes, 3, e) == 2);  // clamped to last real row (2)
  }

  // --- Only one non-empty bank: change-bank is a no-op landing.
  {
    const uint8_t sizes[] = {5, 0, 0};
    NavSim s = MakeSim(sizes, 3, 2);
    s.Change();
    CHECK(s.bank() == 0);
    CHECK(s.engine == 2);
  }

  std::printf("bank_navigation_test: all %d checks passed\n", checks);
  return 0;
}
