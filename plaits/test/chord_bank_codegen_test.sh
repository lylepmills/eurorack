#!/usr/bin/env bash
# Guard: the PLAITS_CHORD_RUNTIME_TABLE hook in chord_bank.cc must be invisible
# to firmware builds.
#
# chord_bank.cc reads its four chord tables through PLAITS_CHORD_*_REF macros so
# that a HOST build (-DPLAITS_CHORD_RUNTIME_TABLE, used by the website's
# in-browser chord preview) can repoint them at a table the user is editing. A
# firmware build never defines the macro, so those macros must expand to exactly
# the same static arrays the code read before the hook existed.
#
# This compiles chord_bank.cc WITHOUT the macro at the merge-base revision and
# at the working revision and diffs the generated assembly. Any difference means
# the hook leaked into the firmware path.
#
# Usage: plaits/test/chord_bank_codegen_test.sh [baseline-rev]
#
# The default baseline is PINNED to the last revision before the hook existed,
# NOT to origin/master. Once this landed, comparing against master would compare
# the hook to itself and the guard would silently stop guarding. If chord_bank.cc
# ever changes for an unrelated reason this will fail — that is the point: look
# at the diff, confirm the change is real and intended, then move the pin.
set -euo pipefail

cd "$(dirname "$0")/../.."
REPO="$(pwd)"
PRE_HOOK_BASELINE=774bbe3696c341ddbae1129b54df623f96728cdf
BASE="${1:-$PRE_HOOK_BASELINE}"
SRC="plaits/dsp/chords/chord_bank.cc"

if ! git cat-file -e "$BASE:$SRC" 2>/dev/null; then
  echo "baseline $BASE has no $SRC" >&2
  exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Same filename in both trees so the compiler cannot bake a differing path into
# the output; only the file CONTENTS differ.
mkdir -p "$WORK/base" "$WORK/head"
git show "$BASE:$SRC" > "$WORK/base/chord_bank.cc"
cp "$SRC" "$WORK/head/chord_bank.cc"

compile() {  # compile <dir> -> <dir>/out.s
  (cd "$1" && c++ -std=c++17 -DTEST -O2 -S -I"$REPO" \
      -o out.s chord_bank.cc)
  # Drop assembler directives that carry the source path or toolchain identity.
  grep -v -E '^\s*\.(file|ident)' "$1/out.s" > "$1/clean.s"
}

compile "$WORK/base"
compile "$WORK/head"

if diff -q "$WORK/base/clean.s" "$WORK/head/clean.s" >/dev/null; then
  echo "OK: firmware-path codegen for $SRC is unchanged vs $BASE"
else
  echo "FAIL: firmware-path codegen for $SRC changed vs $BASE" >&2
  diff -u "$WORK/base/clean.s" "$WORK/head/clean.s" | head -60 >&2
  exit 1
fi
