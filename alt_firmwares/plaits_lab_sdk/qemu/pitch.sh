#!/bin/sh
# Does per-sample cost track pitch? inharmonic-string is the suspect;
# two-op-fm is the control (an FM engine should be pitch-independent).
for e in inharmonic-string two-op-fm; do
  for n in 24 36 48 60 72 84; do
    echo "== $e note=$n =="
    python3 estimate.py --builtin $e --note $n --quiet 2>&1 | tail -1
  done
done
