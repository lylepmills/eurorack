#!/usr/bin/env bash
# Build + run the ChiptuneEngine::Init guard (chiptune_init_test.cc).
set -euo pipefail
cd "$(dirname "$0")/../.."
REPO="$(pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
c++ -std=c++17 -DTEST -O1 -I"$REPO" -o "$OUT/chiptune_init_test" \
  plaits/test/chiptune_init_test.cc \
  plaits/dsp/engine2/chiptune_engine.cc \
  plaits/dsp/chords/chord_bank.cc \
  plaits/resources.cc \
  stmlib/dsp/units.cc \
  stmlib/utils/random.cc
"$OUT/chiptune_init_test"
