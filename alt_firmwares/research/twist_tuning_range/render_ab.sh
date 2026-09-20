#!/bin/bash
# Render the TWIST tuning-range A/B on the host, both flag settings.
#
#   ./render_ab.sh [outdir]
#
# Needs only a host C++ compiler and the stmlib submodule; the five affected
# engines all build under stmlib's -DTEST portable path, so no ARM toolchain
# and no flash cycle are involved. Writes <engine>.stock.wav and
# <engine>.narrowed.wav, each walking TWIST out from centre in five steps.
#
# The first step of every pair is bit-identical by construction: ApplyMacro
# returns the stock value at exactly 0.5 under either span. That is the
# regression worth watching -- if it ever stops being true, the flag has
# changed behavior at noon, which it must never do.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${1:-$HERE/out}"
mkdir -p "$OUT"

SRCS="plaits/dsp/engine/fm_engine.cc
plaits/dsp/engine/virtual_analog_dual_engine.cc
plaits/dsp/engine/virtual_analog_crossfade_engine.cc
plaits/dsp/engine2/phase_distortion_engine.cc
plaits/dsp/engine2/wave_paraphonic_engine.cc
plaits/resources.cc
plaits/dsp/chords/chord_bank.cc
stmlib/utils/random.cc
stmlib/dsp/units.cc"

for FLAG in 0 1; do
  [ "$FLAG" = 0 ] && TAG=stock || TAG=narrowed
  OBJ="$OUT/.obj.$FLAG"; mkdir -p "$OBJ"
  CXX="g++ -std=c++17 -O2 -DTEST -DPLAITS_BUILD_TWIST_TUNING_RANGE=$FLAG -I$ROOT"
  OBJS=""
  for s in $SRCS; do
    o="$OBJ/$(echo "$s" | tr / _).o"
    (cd "$ROOT" && $CXX -c "$s" -o "$o")
    OBJS="$OBJS $o"
  done
  $CXX "$HERE/render_twist.cc" $OBJS -o "$OBJ/render"
  "$OBJ/render" "$OUT" "$TAG"
done

echo
echo "Wrote $OUT/*.wav"
