#!/bin/sh
for e in two-op-fm particle-noise inharmonic-string modal-resonator \
         analog-bass-drum granular-formant harmonic chords phase-distortion \
         string-machine swarm virtual-analog wave-terrain waveshaping \
         filtered-noise pulsar; do
  echo "== $e =="
  python3 estimate.py --builtin $e --quiet 2>&1 | tail -2
done
