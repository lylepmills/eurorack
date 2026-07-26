#!/bin/sh
# Pair QEMU counts with the hardware sweep: each workload at its low and high end.
for spec in "0.05 ALU" "0.30 FLASH" "0.55 RAM" "0.80 BRANCH"; do
  h=$(echo $spec | cut -d' ' -f1); name=$(echo $spec | cut -d' ' -f2)
  for m in 0.0 1.0; do
    echo "== $name h=$h macro=$m =="
    python3 estimate.py ../packages/community/cpucal --harmonics $h --macro $m --quiet
  done
done
