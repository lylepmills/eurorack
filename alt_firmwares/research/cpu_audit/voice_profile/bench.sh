#!/bin/sh
# Voice-level regression bench for a fork export built with the five stock
# engines (virtual-analog, modal-resonator, chords, inharmonic-string, swarm):
# instructions per block + an output hash per configuration.
B=${1:?export dir}
H=$(dirname "$0")
python3 "$H/vh.py" "$B" --engine 1 --tag b_modal_plain
python3 "$H/vh.py" "$B" --engine 1 --aux 2 --trig 1 --tag b_modal_subosc_trig
python3 "$H/vh.py" "$B" --engine 2 --trig 1 --tag b_chords_trig
python3 "$H/vh.py" "$B" --engine 0 --aux 1 --trig 1 --tag b_va_stereo_trig
python3 "$H/vh.py" "$B" --engine 4 --trig 1 --tag b_swarm_trig
python3 "$H/vh.py" "$B" --engine 3 --trig 1 --note 48 --tag b_string_trig
