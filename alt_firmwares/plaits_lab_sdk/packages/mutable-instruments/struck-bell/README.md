# Struck Bell

A port of Braids' BELL — eleven fixed inharmonic partials, additively
synthesised and struck once per trigger, sharing one decay curve and one
odd/even detune.

The DSP is Emilie Gillet's `DigitalOscillator::RenderStruckBell`. The partial
data (ratios, initial amplitudes, two decay tables) is hand-authored directly
in `digital_oscillator.cc` — unlike most Braids tables it has no generator in
`braids/resources/*.py`, so it is embedded verbatim rather than reproduced
from a closed form.

## What it has that modal-resonator does not

Plaits' modal-resonator is a real filter bank: a click or noise burst through
a state-variable resonator per mode, with a continuous `structure` control
bending a stretched-harmonic series and `brightness`/`damping` shaping a
per-mode Q that falls off geometrically with mode index. BELL is not a
resonator — it has no feedback at all. It is eleven independent sine
partials, struck open-loop from a fixed 11-point table that was hand-tuned to
sound like a specific bell, not derived from a formula. modal-resonator's
`structure` can approximate a family of stiff-string/bell-like spectra, but
its stretch factor is one monotonic curve; it cannot land partial 6 at
exactly +12.000 st while partial 7 sits at +17.445 st and partial 9 at
+22.922 st. Conversely modal-resonator can detune its exciter continuously
into true inharmonic clusters BELL's fixed table never visits. The two sit in
the same "struck resonant metal" territory without overlapping.

## The controls

TIMBRE blends between two fixed per-partial decay tables — the long one at
the top of the range, the short one at the bottom — and past ~98% skips the
update entirely for an undecaying drone. The blend is quadratic, so it stays
close to the long table over most of the travel. COLOR spreads alternate
partials apart: every odd-indexed one up, every even-indexed one down,
around their fixed ratios. Struck Bell maps those directly to
Decay and Detune, keeping the module's two axes intact. Brightness and Decay
spread are the two new ones:

- **Brightness (MORPH)** tilts each partial's *initial strike amplitude* by
  its position in the table — the module's amplitudes are a hardcoded array
  no parameter ever touches, so there is no way on Braids to make the strike
  itself brighter or darker. Applied only at the strike, not continuously.
- **Decay spread (MACRO)** reshapes how much faster the high partials decay
  than the low ones, around the ensemble's mean rate — TIMBRE can only slide
  all eleven together between the two fixed tables, never change how far
  apart they are. Stock (noon) is the module's own spread exactly.

## Rate

`RenderStruckBell`'s sample loop writes two output samples and consumes two
units of `size` per one partial-sum computation (an explicit second `size--`
inside the loop body) — SPEC R5's pattern, so the additive recursion runs at
48 kHz and the 96 kHz output is a 2-tap linear reconstruction of it. This
port computes a genuine sample every output sample at its native 48 kHz
instead, and does not reproduce the reconstruction (there is nothing to
interpolate between at a rate the recursion already matches).

The per-partial *decay multiplier* is a separate question: Braids applies it
once per call to `RenderStruckBell`, and that call is always exactly 250 µs
of wall-clock time on the hardware. Plaits' own block size is 12 samples at
48 kHz — the same 250 µs — so the port reproduces the same multiplier once
per call, generalised to whatever block size it is actually called with.

That multiplier acts on an **int32** amplitude counter and truncates every
block, which is not a detail: for the long decay table the 1-LSB truncation
outweighs the multiply, so the module's partials fall linearly and reach
exact silence, where a float amplitude would ring on asymptotically. The
port carries Braids' integer counter for that reason.

Full derivation, the amplitude-quantisation note, the strike-phase detail
(partials start at a quarter cycle, not zero), the `TRIGGER_UNPATCHED`
sustain convention, and every declared deviation are in the header comment of
`plaits/dsp/engine2/struck_bell_engine.h`; the measured comparison against
the module is in `tests/ab.json`. Both copyright lines are carried in
`LICENSE` and in each source file.
