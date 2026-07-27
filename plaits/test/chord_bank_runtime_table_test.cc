// Guard for the PLAITS_CHORD_RUNTIME_TABLE hook in chord_bank.cc.
//
// A firmware build's chord tables are compile-time constants. Host builds (the
// website's in-browser chord preview) need to sound the table the user is
// editing, so under -DPLAITS_CHORD_RUNTIME_TABLE a ChordBank reads its tables
// through repointable pointers. This asserts that:
//   1. before any SetRuntimeTables call, a bank reads the compiled-in tables
//      (so defining the macro alone changes nothing);
//   2. after SetRuntimeTables, it reads the caller's cents + arp lengths, and
//      honors the caller's per-table offsets/sizes;
//   3. passing nulls restores the compiled-in tables.
//
// Build + run: plaits/test/chord_bank_runtime_table_test.sh

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "plaits/dsp/chords/chord_bank.h"

#include "stmlib/utils/buffer_allocator.h"

using plaits::ChordBank;
using plaits::kChordNumNotes;

namespace {

int g_failures = 0;

void ExpectNear(const char* what, float actual, float expected) {
  const float tolerance = 1e-3f;
  if (std::fabs(actual - expected) > tolerance) {
    printf("FAIL %s: got %.6f, want %.6f\n", what, actual, expected);
    ++g_failures;
  }
}

void ExpectEq(const char* what, int actual, int expected) {
  if (actual != expected) {
    printf("FAIL %s: got %d, want %d\n", what, actual, expected);
    ++g_failures;
  }
}

// SemitonesToRatio for a whole number of cents, computed independently of the
// stmlib lookup table the implementation uses.
float CentsToRatio(float cents) {
  return std::pow(2.0f, cents / 1200.0f);
}

char g_ram_block[1024];

}  // namespace

int main() {
  stmlib::BufferAllocator allocator(g_ram_block, sizeof(g_ram_block));

  // 1. Untouched: the compiled-in first chord of the first table. The stock
  // table starts with the octave voicing { 0, 1, 1199, 1200 }.
  {
    ChordBank bank;
    bank.Init(&allocator);
    bank.Reset();
    bank.set_chord(0.0f, 0);
    ExpectNear("stock ratio[0]", bank.ratio(0), CentsToRatio(0.0f));
    ExpectNear("stock ratio[1]", bank.ratio(1), CentsToRatio(1.0f));
    ExpectNear("stock ratio[2]", bank.ratio(2), CentsToRatio(1199.0f));
    ExpectNear("stock ratio[3]", bank.ratio(3), CentsToRatio(1200.0f));
    ExpectEq("stock num_notes", bank.num_notes(), 1);
  }

  // 2. A caller-supplied table: two tables of two chords each, so the offsets
  // and sizes are exercised rather than assumed.
  static const int16_t kCents[4][kChordNumNotes] = {
    {    0,  400,  700, 1200 },   // table 0, chord 0 — major
    {    0,  300,  700, 1200 },   // table 0, chord 1 — minor
    {    0,  700, 1400, 2100 },   // table 1, chord 0 — stacked fifths
    {    0,  200,  400,  600 },   // table 1, chord 1 — whole-tone cluster
  };
  static const uint8_t kArpLengths[4] = { 3, 3, 4, 4 };
  static const uint8_t kOffsets[2] = { 0, 2 };
  static const uint8_t kSizes[2] = { 2, 2 };

  ChordBank::SetRuntimeTables(kCents, kArpLengths, kOffsets, kSizes, 2);

  {
    ChordBank bank;
    bank.Init(&allocator);
    bank.Reset();

    bank.set_chord(0.0f, 0);
    ExpectNear("runtime t0c0 ratio[1]", bank.ratio(1), CentsToRatio(400.0f));
    ExpectNear("runtime t0c0 ratio[2]", bank.ratio(2), CentsToRatio(700.0f));
    ExpectEq("runtime t0c0 num_notes", bank.num_notes(), 3);

    // Top of table 0 selects its second chord.
    bank.set_chord(1.0f, 0);
    ExpectNear("runtime t0c1 ratio[1]", bank.ratio(1), CentsToRatio(300.0f));
    ExpectEq("runtime t0c1 num_notes", bank.num_notes(), 3);

    // Table 1 starts at offset 2.
    bank.set_chord(0.0f, 1);
    ExpectNear("runtime t1c0 ratio[1]", bank.ratio(1), CentsToRatio(700.0f));
    ExpectNear("runtime t1c0 ratio[3]", bank.ratio(3), CentsToRatio(2100.0f));
    ExpectEq("runtime t1c0 num_notes", bank.num_notes(), 4);

    // An out-of-range table option falls back to table 0, per set_chord.
    bank.set_chord(0.0f, 9);
    ExpectNear("runtime clamp ratio[1]", bank.ratio(1), CentsToRatio(400.0f));
  }

  // 3. Nulls restore the compiled-in tables.
  ChordBank::SetRuntimeTables(NULL, NULL, NULL, NULL, 0);
  {
    ChordBank bank;
    bank.Init(&allocator);
    bank.Reset();
    bank.set_chord(0.0f, 0);
    ExpectNear("restored ratio[2]", bank.ratio(2), CentsToRatio(1199.0f));
    ExpectEq("restored num_notes", bank.num_notes(), 1);
  }

  if (g_failures) {
    printf("%d assertion(s) failed\n", g_failures);
    return 1;
  }
  printf("OK: chord_bank runtime-table hook\n");
  return 0;
}
