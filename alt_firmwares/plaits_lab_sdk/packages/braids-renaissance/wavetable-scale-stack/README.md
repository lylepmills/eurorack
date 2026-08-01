# Wavetable Scale Stack

A port of WTx6 from Tom Burns'
[Braids Renaissance](https://github.com/boourns/eurorack-renaissance): five
voices spaced by equal scale-degree spans and played through Braids' 33-wave
`mini_wave_line`.

WTx6 is deliberately separate from Scale Stack. HARMONICS controls the span,
TIMBRE controls voice spread, MORPH scans the wavetable, and MACRO selects the
scale. The table position no longer drags the ensemble detune with it, and the
classical engine keeps its original four-wave MORPH mapping.

The Plaits-native wave substitution, relative-level restoration, linear table
read, approved 2.5x makeup, and cold/shared table cost are documented in
`wavetable-chord`'s README. The two wavetable scale engines use the same mapping
and the dedicated `scale_wavetable_voices.cc` implementation.

Scale quantisation, the sixteen-scale bank, microtonal precision, wide-span
thinning, and Renaissance's double-accumulation defect are shared with
`scale-stack` and documented there. OUT is the complete stack; AUX is the root
voice alone.

The shared wavetable-read optimization reduces the calibrated Cortex-M4 sweep
from 73% to **63% worst-case (55–82%)**, while retaining all five voices and the
same linear table interpolation. Before/after WTCH renders differ by at most
one 16-bit PCM LSB with 0.00 dB RMS and spectral deltas; WTx6 uses that identical
readout. A hardware DWT probe remains the publication authority.
