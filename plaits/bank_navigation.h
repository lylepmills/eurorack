// Copyright 2026 Rubato Audio.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, subject to the conditions in the MIT
// license. See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Banked engine navigation with per-bank memory.
//
// A build's engines are grouped into up to four banks (green / red / amber /
// orange on the LEDs). A bank may hold FEWER than eight engines — a "short
// bank" — so its select-button cycle wraps after its real last engine rather
// than at a fixed row 7. Because banks can differ in length, the row that a
// short bank lacks would otherwise be undefined when you change into it; this
// module resolves that with PER-BANK MEMORY (design "B"): changing bank lands
// on the row last selected in the destination bank (skipping empty banks), so
// the phantom-row case cannot arise. That per-bank row is persisted across
// power cycles by the caller (Ui, via the saved State).
//
// The engine index is a flat, compact position 0..count-1; bank/row are derived
// from the per-bank size table. Everything here is pure integer math with no
// hardware dependency, so it is host-tested directly (test/bank_navigation_test.cc)
// without the UI's driver stack.

#ifndef PLAITS_BANK_NAVIGATION_H_
#define PLAITS_BANK_NAVIGATION_H_

#include <stdint.h>

namespace plaits {

// Flat index of the first engine in `bank`.
inline int BankOffset(const uint8_t* bank_sizes, int bank) {
  int offset = 0;
  for (int i = 0; i < bank; ++i) {
    offset += bank_sizes[i];
  }
  return offset;
}

// The bank an engine belongs to.
inline int BankOfEngine(const uint8_t* bank_sizes, int num_banks, int engine) {
  int bank = 0;
  while (bank + 1 < num_banks && engine >= BankOffset(bank_sizes, bank + 1)) {
    ++bank;
  }
  return bank;
}

// The row (0-based position within its bank) of an engine.
inline int RowOfEngine(const uint8_t* bank_sizes, int num_banks, int engine) {
  return engine - BankOffset(bank_sizes,
                             BankOfEngine(bank_sizes, num_banks, engine));
}

// Step to the next engine within the current bank, wrapping back to the bank's
// first engine after its real last one (so a 3-engine bank cycles 0->1->2->0).
inline int StepWithinBank(
    const uint8_t* bank_sizes, int num_banks, int engine) {
  const int bank = BankOfEngine(bank_sizes, num_banks, engine);
  const int base = BankOffset(bank_sizes, bank);
  const int row = engine - base;
  const int next_row = (row + 1 >= bank_sizes[bank]) ? 0 : row + 1;
  return base + next_row;
}

// Change to the next non-empty bank, landing on the row last selected there
// (per-bank memory). `bank_last_row[b]` is the caller's remembered row for bank
// b; it is clamped to the destination's real size for safety (e.g. after a
// rebuild changed a bank's length). Empty banks (size 0) are skipped; if no
// other bank is non-empty the current bank's remembered row is used, so the
// result is always a valid engine.
inline int ChangeBank(
    const uint8_t* bank_sizes, int num_banks, int engine,
    const uint8_t* bank_last_row) {
  const int bank = BankOfEngine(bank_sizes, num_banks, engine);
  int next = bank;
  for (int i = 0; i < num_banks; ++i) {
    next = (next + 1) % num_banks;
    if (bank_sizes[next] > 0) {
      break;
    }
  }
  int row = bank_last_row[next];
  if (row >= bank_sizes[next]) {
    row = bank_sizes[next] - 1;
  }
  if (row < 0) {
    row = 0;
  }
  return BankOffset(bank_sizes, next) + row;
}

}  // namespace plaits

#endif  // PLAITS_BANK_NAVIGATION_H_
