# Morph

A port of Braids' MORPH — one oscillator walked from triangle through saw to a
narrowing pulse by a single knob, then run into a low-pass that tracks the
played note and a `tanh(8x)` fuzz.

The DSP is Emilie Gillet's `MacroOscillator::RenderMorph` over
`AnalogOscillator::RenderTriangle`, `RenderSaw` and `RenderSquare`.

## What it has that Virtual analog does not

Virtual analog is the neighbour: a variable-shape oscillator against a sync
square, MORPH sweeping the shape. It shares the triangle/saw/square/pulse
vocabulary and nothing else. Its two waveforms are a detuned pair rather than a
crossfade through one oscillator, and — the part that matters — it has no fuzz
stage and no filter feeding one. What makes MORPH sound like MORPH is a violent
overdrive sitting behind a low-pass that closes all the way down onto the
fundamental, and none of that is reachable on the Plaits engine.

## The controls

Braids has two knobs and this port keeps both where they were.

TIMBRE is the shape walk. It picks one of three regions and the crossfade
inside it: triangle against saw up to 1/3, saw against a 50% square up to 2/3,
then the square alone narrowing to a 1.2% pulse. The weights are continuous
across both boundaries, so the sweep is smooth even though the branch is not.

COLOR does **two** jobs at once, and that coupling is the design. It pulls the
low-pass cutoff down from 128 semitones above the note to the note itself
(`macro_oscillator.cc:99`) *and* raises the fuzz mix from dry to fully wet
(`:107`). One knob, welded. Here it is Fuzz.

Tone and Drive are the two this port adds, both stock at noon. Tone offsets the
cutoff five octaves either side of where Fuzz put it, which is that coupling
taken apart — at the top of Fuzz the module's filter sits on the fundamental
and there is no way back out to a bright fuzz. Drive is gain into the
overdrive: the fuzz mix crossfades dry against wet, while drive changes how
hard the `tanh` is hit, and Braids welds it at unity.

## One thing worth recording about the model

In the top third of TIMBRE, `:90` sets the second oscillator to
`OSC_SHAPE_SINE` and `:91` then sets `balance = 0` — and `Mix(a, b, 0)` returns
`a`. The sine is rendered into a scratch buffer every block and never mixed in.
The top third of the knob is a variable-width pulse alone, and the port does
not render a sine at all.

## Rate

`RenderMorph` does not end in `size -= 2` — its loop writes one sample per
iteration — so it is a 96 kHz algorithm, and its one rate-bearing constant is
the one-pole coefficient read from `lut_svf_cutoff`, a table generated against
`sample_rate = 96000`. The port therefore runs its whole inner chain 2x
oversampled at that same 96 kHz, so the coefficient transfers verbatim instead
of being re-derived. `RenderTriangle` is additionally 2x oversampled inside and
box-averaged, and the port reproduces that too.

The decimator is a 31-tap Kaiser halfband. Against the reference renderer's own
127-tap decimator it is within 0.1 dB to 18 kHz, −0.65 dB at 20 kHz and
−2.3 dB at 22 kHz, with the stopband under −78 dB above 32 kHz.

## Tables

`ws_violent_overdrive` reproduces at 0 LSB across all 257 entries from
`scale(tanh(8x))` in `braids/resources/waveshapers.py`, and is embedded because
it is read once per 96 kHz sub-sample. 86 of those entries sit within 1 LSB of
full scale — every one at |x| ≥ 0.671875 — so past that point the curve is a
hard clip rather than a soft one, and that flattening is part of the sound.

`lut_svf_cutoff` reproduces at 0 LSB from `2*sin(pi*min(f/96000, 1/8))*32767`,
and is *not* embedded — it is read once per block, so the port evaluates the
closed form. Differenced against Braids' interpolated read at all 32768
reachable indices, the closed form is within 0.98% of the coefficient
(0.17 semitones of cutoff) at or above a cutoff of MIDI 48, 2.2%
(0.38 semitones) at or above MIDI 24 and 4.3% (0.72 semitones) at or above
MIDI 12, rising to 9.7% only below MIDI 12 — where the stored coefficient has
fallen to a two-digit integer and its own quantisation dominates.

## What the A/B measures

Thirteen cases. Twelve of them are held to 1.0 dB AC RMS, 1.0 cent and 1.5 dB
of spectrum, and measure within 0.71 dB, 0.6 cents and 1.29 dB. The thirteenth
is the narrowest pulse two octaves up, where 1.2% of a cycle is under one
96 kHz sample and both sides alias: AC RMS +1.05 dB and spectrum 2.22 dB. It
declares no cents tolerance — not because the estimator returns nothing, it
returns a number on both sides, but because on that aliased waveform each side
locks onto a different sub-harmonic of the played 1046.5 Hz (reference
149.5 Hz = f0/7, port 262.3 Hz = f0/4, a 969-cent gap). That figure measures
the estimator, not the oscillator. The case is carried because it is where the
port stops tracking.

## The one widened tolerance, and why it is Braids' doing

`AnalogOscillator::Render` calls `Init()` when the shape changes, and `Init()`
resets `pitch_` to `60 << 7` *after* `RenderMorph` has already called
`set_pitch` — so whichever oscillator changed shape renders its first
24-sample block at MIDI 60 instead of the played note, and the phase error that
leaves never comes back. `braids.cc` declares the oscillator at file scope, so
`previous_shape_` starts zero-initialised at `OSC_SHAPE_SAW`: the square or the
triangle re-Inits, the saw does not, and the two end up
`24 * (f60 - fnote) / 96000` cycles apart. That is zero at MIDI 60, grows in
either direction, and flips sign through it.

Measured in the reference renderer, by DFT-ing the fundamental of a saw-alone
render against a square-alone render at the same note and referencing both to
MIDI 60: +0.01039 cycles at MIDI 57 against +0.01041 predicted, +0.03785 at 45
against +0.03791, +0.05159 at 33 against +0.05166, −0.06534 at 72 against
−0.06541, −0.19632 at 84 against −0.19622, −0.35043 at 92 against −0.34990 and
−0.45839 at 96 against −0.45784 — agreement to 1e−4 of a cycle across four
octaves.

The port runs all three shapes off one phase accumulator and does not
reproduce this, because Braids has no canonical offset to copy: its own depends
on the note *and* on where TIMBRE has been, since crossing a region boundary
re-Inits an oscillator and re-rolls it. It is a declared deviation.

What it costs is small under a note and large above one. For a 50/50
saw-square mix the level follows `RMS² = 1/12 + 1/4 + (0.5 − 2δ)/2`, which
predicts the port reading +0.00 dB at MIDI 60, +0.29 at 45, +0.40 at 33, +1.78
at 84 and +3.98 at 92; the dry mix measures +0.01, +0.26, +0.33, +1.77 and
+4.10. So twelve cases hold at 1.0 dB, and only `guard-band` at MIDI 92 needs
more: it measures +2.36 dB and 1.50 dB of spectrum — the fuzz at COLOR 0.8
compressing that dry +4.10 — and carries 2.5 dB and 1.8 dB.

Two control cases keep that honest. `mix-at-60` is `guard-band`'s own mix and
fuzz at the note where the module's offset is exactly zero (−0.01 dB, 0.11 dB),
and `rolloff-92` is a single shape at `guard-band`'s own note and fuzz
(−0.01 dB, 0.10 dB). Neither the mix nor the note widens anything alone; only
the two together do.

Note for anyone comparing against older figures: until 2026-07 the reference
renderer declared its `MacroOscillator` as a stack *local* rather than at file
scope, which left `previous_shape_` and `previous_phase_increment_`
indeterminate and gave it a note-*independent* offset of its own, near 0.166
cycles. Every morph number taken against that renderer is void, including the
"1.4 to 1.7 dB on every mixed-shape case" reading and the 2.0 dB tolerance it
justified.

Both copyright lines are carried in `LICENSE` and in each source file; declared
deviations are listed in the header comment of
`plaits/dsp/engine2/morph_engine.h`, and `tests/ab.json` holds the measured
comparison against the module.
