#!/usr/bin/env bash
# Build + run the PLAITS_CHORD_RUNTIME_TABLE guard (chord_bank_runtime_table_test.cc).
set -euo pipefail

cd "$(dirname "$0")/../.."
REPO="$(pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

c++ -std=c++17 -DTEST -DPLAITS_CHORD_RUNTIME_TABLE -O1 -I"$REPO" \
  -o "$OUT/chord_bank_runtime_table_test" \
  plaits/test/chord_bank_runtime_table_test.cc \
  plaits/dsp/chords/chord_bank.cc \
  stmlib/dsp/units.cc

"$OUT/chord_bank_runtime_table_test"
