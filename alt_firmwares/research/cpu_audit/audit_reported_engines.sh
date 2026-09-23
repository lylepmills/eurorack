#!/bin/bash
# CPU audit of the engines a beta tester reported as "weird aliasing".
#
# The report (2026-09-20) named Braids' Folder, Swarm, Morph and Ring Mod, and
# added that it "sometimes happens only when the trig LPG is effective". That
# detail is the whole diagnosis: PERFORMANCE.md says the ~1,500 cycles/sample
# budget "also covers the low-pass gate, output stage, UI and ADCs", so an
# engine already near the limit overruns exactly when TRIG is patched. The
# tester also said it happens "with one mode loaded", which rules out flash
# pressure and points at per-block cost.
#
# These are emulated estimates with a fitted band, not hardware truth. Confirm
# with `plaits_lab.py build --hardware --cpu-probe` before acting.
set -euo pipefail
SDK="$(cd "$(dirname "$0")/../../plaits_lab_sdk" && pwd)"
cd "$SDK"

echo "REPORTED by the tester:"
for e in fold saw-swarm morph swarm ring-mod; do
  u=$(python3 qemu/estimate.py --builtin "$e" --quiet 2>&1 | grep -o "usage=[0-9]*%")
  printf "  %-16s %s\n" "$e" "$u"
done

echo
echo "NOT reported, for contrast:"
for e in virtual-analog two-op-fm plucked harmonics vosim csaw; do
  u=$(python3 qemu/estimate.py --builtin "$e" --quiet 2>&1 | grep -o "usage=[0-9]*%")
  printf "  %-16s %s\n" "$e" "$u"
done

echo
echo "PERFORMANCE.md: healthy below ~75%, at risk above ~90%, and the engine"
echo "measurement excludes the LPG and output stage that land on top of it."
