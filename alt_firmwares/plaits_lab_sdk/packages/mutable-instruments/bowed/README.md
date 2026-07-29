# Bowed

A port of Braids' BOWD: a bowed-string waveguide driven by a stick-slip
friction exciter. Nothing in the stock palette is a continuous friction voice.

The DSP is Emilie Gillet's `DigitalOscillator::RenderBowed`, which ends in
`size -= 2` and is therefore a 48 kHz algorithm writing a 96 kHz stream
through a 2× linear interpolator. Every rate constant, the bridge filter and
the body biquad transfer verbatim; only the output stage is re-derived,
because that interpolator is an upsampler rather than a filter and copying it
literally would land 1.6 dB darker than the hardware.

MORPH and MACRO are new — Braids welds the nut reflection to −1.0 and the body
resonance to one constant.

The delay lines stay int8 at Braids' own lengths, 1024 + 4096. Braids
quantizes every write to int8 regardless, so float storage would spend four
times the memory on values that have already been rounded — and keeping the
lengths keeps Braids' own octave-fold floor, 11.44 Hz at HARMONICS 0, rather
than the 22.9 Hz a halved float line would force.

`bridge-underflow` pins the corner the in-tree extremes sweep would miss,
which renders at note 60: the bridge tap goes degenerate from MIDI 84.5
upward at HARMONICS 0. `low-fold` is misnamed and does not reach the octave
fold — the fold floor is 9.4–12.6 Hz depending on bow position (11.44 Hz at
HARMONICS 0, 12.64 at HARMONICS 1, a minimum of 9.38 at `parameter_1` = 51),
so nothing halves at its MIDI 21; `octave-fold` in `tests/ab.json` renders at
MIDI 4, where both sides do fold.

## A/B against the module

`tests/ab.json` is the reproducible comparison against Braids BOWD, run with
`python3 ab_engine.py packages/mutable-instruments/bowed --bands`. Sixteen
cases sweep both ends of both Braids axes, four notes, a re-strike and the
octave fold, with MORPH pinned at 1.0 — that, not the detent, is where the
port's nut gain equals Braids' welded −1.0 — and MACRO at its detent.

Note that the case ids `pressure-hard` and `pressure-light` read off
`parameter_0`'s magnitude and are therefore backwards against the physical
quantity: `pressure-hard` is TIMBRE 0.0, the *lightest* bow force. Read the
case names, not the ids.

Nine of the sixteen agree: pitch inside ±2 cents, octave-band spectra 0.22 to
3.19 dB apart, level +4.10 dB at stock, which is the declared 1.6× make-up.
`high-bridge-clamp` declares no tolerance and counts as neither — both sides
are dead there, so its figures are the ratio of two residuals.

**Six fail, and they are a real defect rather than a tolerance to widen.**
Braids interpolates between delay-line taps at int8 resolution — `Mix()`
returns an integer before the shift — and this port interpolates in float.
That truncation is a loss inside the feedback loop, and it is what lets the
module's bow slip. Braids is *bistable* along TIMBRE: at note 45 it collapses
to −44 dBFS at TIMBRE 0–0.075 and again at 0.175–0.225 while bowing normally
between, and it collapses at mid and high bow force too, given the right note
and bow position — at note 24, TIMBRE 0.5, HARMONICS 0.0 it falls to
−49 dBFS. The port is flat across all of it, −14 to −7 dBFS, so the "thin
whistle" end of its own control copy is missing everywhere, by up to 37 dB.
Rebuilding the engine with only that one truncation restored brings all 36
points of a note × bow-position × bow-force grid back inside the declared
make-up; restoring Braids' int8 wrap, or flooring the friction curve to its
integer table, moves nothing. The engine header carries the evidence and the
isolation. It is reported, not fixed: a fix moves the package digest and is
Lyle's call.

Both copyright lines are carried in `LICENSE` and in each source file.
