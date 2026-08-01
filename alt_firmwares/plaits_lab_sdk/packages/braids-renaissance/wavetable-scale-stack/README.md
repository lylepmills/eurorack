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

The calibrated Cortex-M4 sweep estimates 73% worst-case (64–94%), substantially
below the combined-engine prototype because there is no square-to-table
crossfade. A hardware DWT probe remains the publication authority.
