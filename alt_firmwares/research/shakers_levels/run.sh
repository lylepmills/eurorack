#!/bin/sh
# Build and run the shaker level measurement against the REAL engine sources
# (not a copy), so a re-run after retuning measures what actually ships.
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
g++ -O2 -w -DTEST -I"$ROOT" \
  "$(dirname "$0")/measure.cc" \
  "$ROOT/plaits/dsp/engine2/shakers_engine.cc" \
  "$ROOT/plaits/resources.cc" \
  "$ROOT/stmlib/dsp/units.cc" \
  "$ROOT/stmlib/utils/random.cc" \
  -o /tmp/shakers_levels -lm
/tmp/shakers_levels "$@"
