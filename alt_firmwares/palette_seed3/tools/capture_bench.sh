#!/bin/bash
# Capture the bench CSV the Seed3 prints over USB serial after flashing.
#   tools/capture_bench.sh [out.csv]     (default: bench_<date>.csv in the current dir)
# Waits for the CDC port to appear, then records until the "# bench done" line
# (or Ctrl-C). The board prints the header comment lines first; keep them.
set -eu
OUT=${1:-bench_$(date +%Y%m%d_%H%M%S).csv}
PORT=""
for i in $(seq 1 60); do
  PORT=$(ls /dev/tty.usbmodem* 2>/dev/null | head -1 || true)
  [ -n "$PORT" ] && break
  sleep 0.5
done
[ -n "$PORT" ] || { echo "no /dev/tty.usbmodem* port (is the app running and USB connected?)" >&2; exit 1; }
echo "reading $PORT -> $OUT (Ctrl-C to stop)" >&2
stty -f "$PORT" 115200 raw -echo
# `sed -u` keeps lines flowing; quit after the terminator.
sed -u '/^# bench done/q' < "$PORT" | tee "$OUT"
echo "saved $OUT" >&2
