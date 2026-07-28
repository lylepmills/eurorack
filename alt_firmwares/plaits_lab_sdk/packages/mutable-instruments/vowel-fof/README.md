# Vowel FOF

A port of Braids' VFOF: five very narrow state-variable filters excited by a
band-limited saw, sweeping five vowels across five vocal registers.

`RenderVowelFof` ends in `size -= 2`, so it is a 48 kHz algorithm and all
three of its half-rate compensations drop out here — the doubled increment,
the `+ (12 << 7)` octave offset, and the output averager.

**The finding that justifies the engine is real**, and confirmed in the
source: `out += svf_bp[i] * amplitudes[0] >> 17` reads `amplitudes[0]`, not
`amplitudes[i]`, and column 0 of the amplitude table is 16384 in all 25 rows —
so 100 of the 125 amplitude entries have been dead since 2013 and every
formant is voiced flat. MACRO restores them, with the detent reproducing
hardware exactly.

Those amplitudes are **linear** with 16384 as unity, not semitone
attenuations, so the tilt is a linear interpolation between flat and true.

TIMBRE and MORPH are swapped relative to Braids so register and vowel land on
the same knobs as `speech` — two vocal engines with inverted axes in one
palette is worse than a naming deviation. HARMONICS is new: Braids' excitation
is a bare saw, and this crossfades toward noise so the bank can whisper.

The tables are vendored rather than shared with `NaiveSpeechSynth`, which
holds the same grid. Its uint8 quantisation carries about half a semitone of
centre error against filters whose bandwidth is 0.24 semitones — half a
semitone of error on a quarter-semitone filter is a different formant, not a
rounding difference.

Both copyright lines are carried in `LICENSE` and in each source file.
