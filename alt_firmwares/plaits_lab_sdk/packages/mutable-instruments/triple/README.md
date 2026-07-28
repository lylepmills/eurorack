# Triple

Braids' four TRIPLE models — square, saw, triangle and sine ×3 — merged into
one slot. They differ only in the base waveform, so MORPH turns that into a
continuous axis and four models fit where one used to: the best ratio in the
port after `z-filter`.

**What this overlaps, so the A/B is fair.** `virtual-analog`'s first control is
literally a detune across musical intervals, `swarm` is a detuned cloud, and
the `chords` table is builder-configurable in cents with entries like
`{0, 1, 1199, 1200}` already present. Four voices at arbitrary cent intervals
are reachable today at zero marginal flash. What is *not* reachable is the
gesture: two interval knobs you sweep, rather than a table you author.

**Unison at noon is load-bearing.** Braids' ladder puts −3.125 cents at index
31 and zero at index 32, and its crossfade weight at the knob centre is
255/256 — so the two land within a hundredth of a cent of unison. The index
and crossfade arithmetic is reproduced rather than tidied, because a cleaner
re-parameterisation breaks that silently. The `unison` scenario pins it.

MACRO's range is minimum-equals-stock, not bidirectional: a pulse of duty *d*
and one of duty *1−d* have identical harmonic magnitudes, so a bidirectional
range would be a knob whose halves are spectral mirror images at half the
resolution. It is inert in the sine region, stated rather than papered over.

Both copyright lines are carried in `LICENSE` and in each source file.
