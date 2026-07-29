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

**Unison at noon is load-bearing.** What makes it reachable in both the module
and the port is index 32 with a zero crossfade: every parameter from 16384 to
16639 lands on that rung and detunes by exactly nothing. The 255/256 crossfade
one step below it does not — Braids ends its crossfade with an integer shift,
which floors that position to −0.781 cents rather than the −0.012 cents exact
arithmetic would give. The port evaluates the same expression in float and does
not floor it, so its detune runs up to 0.769 cents sharp of the module's, worst
at the knob centre. That is an open deviation rather than a settled one: the
`saw-unison` / `saw-unison-exact` pair in `tests/ab.json` measures it, and
`square-high` is left failing its tolerance so it cannot be forgotten. The
index and crossfade arithmetic is otherwise reproduced rather than tidied,
because a cleaner re-parameterisation breaks the plateau silently. (16384–16639
is the range where `i1 == i2 == 32`, which is what both sides share. The
module's zero-detune plateau is in fact wider — 16384–16703 — because the same
integer floor swallows the first 64 steps of the crossfade toward index 33,
while the port's stops at 16640. Same cause, second symptom.)

MACRO's range is minimum-equals-stock, not bidirectional: a pulse of duty *d*
and one of duty *1−d* have identical harmonic magnitudes, so a bidirectional
range would be a knob whose halves are spectral mirror images at half the
resolution. It is inert in the sine region, stated rather than papered over.

**Two further open deviations, found by the independent audit pass on
2026-07-29 and recorded here rather than in the header, because
`triple_engine.h` is hashed byte-for-byte into `package_digest`
(`validate_catalog.py:27-38`) and writing to it again would move this engine's
digest a second time.**

1. **The DC blocker has no Braids counterpart and is audible in the bass.**
   `triple_engine.cc:133-143` runs a one-pole blocker on both outputs at all
   times; `RenderTriple` has no filter of any kind. Its ~7.6 Hz corner is
   inaudible at the notes the rest of the suite uses, but note 21 (A0) with the
   interval knob on its bottom rung puts a voice at 6.875 Hz, on the corner:
   energy below 20 Hz falls from 12.73 % of the module's total to 8.48 % of the
   port's, −2.9 dB absolute, and the headline AC RMS reads −1.16 dB. The
   `saw-low-dc` case measures it and is left failing at the guide tolerance.
   The blocker is also inert-by-construction at MACRO ≤ 0.5, where all three
   waveforms are symmetric and carry no DC.
2. **TRIGGER is a port addition, not a ported behaviour.**
   `MacroOscillator::Strike` (`braids/macro_oscillator.h:80-81`) forwards only
   to `digital_oscillator_`, and `RenderTriple` runs entirely on
   `analog_oscillator_[0..2]`, so the module ignores the trigger here
   completely. The port's `Reset()` realigns all three phases, which the
   catalog manual states, and also reseeds `slave_frequency_` to 0.01 through
   `VariableShapeOscillator::Init()`, which the manual does not — so each
   trigger re-applies the first-block frequency ramp worth +39.0° on a 27.5 Hz
   voice against +31.9° on a 110 Hz one. Cost at two triggers a second:
   +6.04 dB in the 20–40 Hz band against +0.04 dB untriggered, on 0.1 % of the
   energy. The `saw-trigger` case measures it and passes.

The interval table itself lives in `plaits/dsp/engine2/triple_engine_data.h`,
which is **not** covered by `package_digest` — the catalog schema accepts only
`.cc` files in `source.files` (`plaits_lab.py:489`), so all 65 tuning values
could change without moving this engine's digest. `vowel-fof` avoids this by
keeping its tables in a declared `vowel_fof_data.cc` behind an extern-only
header. Fixing it here is itself a digest move, so it is reported, not made.

Both copyright lines are carried in `LICENSE` and in each source file.
