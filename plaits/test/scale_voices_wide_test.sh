#!/usr/bin/env bash
# Build + run scale_voices_wide_test.cc against a bank holding Chromatic (12
# degrees, the kScaleVoicesMaxDegrees ceiling), Diminished W/H (8) and a
# pentatonic, the way a hosted recipe overrides PLAITS_SCALE_BANK.
set -euo pipefail

cd "$(dirname "$0")/../.."
REPO="$(pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

BANK='{ { { 0, 128, 256, 384, 512, 640, 768, 896, 1024, 1152, 1280, 1408 }, 12 }, { { 0, 256, 384, 640, 768, 1024, 1152, 1408, 0, 0, 0, 0 }, 8 }, { { 0, 256, 512, 896, 1152, 0, 0, 0, 0, 0, 0, 0 }, 5 } }'

c++ -std=c++17 -DTEST -O1 -I"$REPO" \
  -DPLAITS_SCALE_BANK_COUNT=3 "-DPLAITS_SCALE_BANK=$BANK" \
  -o "$OUT/scale_voices_wide_test" \
  plaits/test/scale_voices_wide_test.cc \
  plaits/dsp/engine2/scale_voices.cc \
  plaits/dsp/engine2/scale_wavetable_voices.cc \
  plaits/dsp/engine2/diatonic_chord_engine.cc \
  plaits/dsp/engine2/scale_stack_engine.cc \
  plaits/dsp/engine2/wavetable_chord_engine.cc \
  plaits/dsp/engine2/wavetable_scale_stack_engine.cc \
  plaits/resources.cc \
  stmlib/dsp/units.cc

"$OUT/scale_voices_wide_test"
