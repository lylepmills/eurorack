# Wavetable Chord

A port of WTCH from Tom Burns'
[Braids Renaissance](https://github.com/boourns/eurorack-renaissance): the
scale-degree chord construction from `diatonic-chord`, played through Braids'
33-wave `mini_wave_line`.

WTCH is deliberately separate from Diatonic Chord. HARMONICS still selects the
chord, TIMBRE controls voice spread, MORPH scans the wavetable, and MACRO selects
the scale. No axis has two musically significant jobs, and the classical engine
keeps its original four-wave MORPH spacing.

## The wavetable without a second wave bank

Renaissance scans `mini_wave_line` through its 33,024-byte `wt_waves`. This port
maps that line onto Plaits' already-shipped `wav_integrated_waves`: sixteen slots
use the same source waveform and seventeen use the measured nearest counterpart.
The source `waves.bin` is byte-identical in Braids Renaissance and Plaits, and
the listening A/B could not reliably distinguish the substitution from the true
Braids section.

Each slot's relative RMS is restored before crossfading, the reconstructed table
uses Braids-style linear sample interpolation, and the approved 2.5x makeup
matches the classical square's useful level. The large 50,688-byte Plaits table
has zero marginal cost whenever another selected engine already retains it;
otherwise that shared table is the honest cold cost of choosing WTCH or WTx6.

ARM 4.8.3 recipe differencing measured WTCH at 54,352 bytes cold and 3,648
bytes when stock Wavetable already retains the wave bank. WTx6 measured 54,192
bytes cold and 3,488 bytes warm. Selecting both together costs 54,736 bytes
cold or 4,032 bytes warm, so the table and scale machinery are paid only once.
The website's full-palette budget rows use the paired-engine differential from
the same sweep, calibrated against the already-published classical rows.

## Chords, scales, and outputs

Chord construction, the sixteen-scale bank, microtonal pitch precision, and the
decision to interpret Renaissance's chord offsets as absolute scale degrees are
shared with `diatonic-chord` and documented in its README. OUT is the complete
chord; AUX is the root voice alone.

The calibrated Cortex-M4 sweep estimates 74% worst-case (64–95%), substantially
below the combined-engine prototype because there is no square-to-table
crossfade. A hardware DWT probe remains the publication authority.
