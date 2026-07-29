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

Eleven cases. Across the ten at musical settings, pitch is within 0.9 cents and
spectrum within 0.84 dB, and AC RMS within 0.42 dB on every case that plays a
single shape. The eleventh is the narrowest pulse two octaves up, where 1.2% of
a cycle is under one 96 kHz sample and both sides alias: AC RMS +1.12 dB and
spectrum 2.29 dB. It declares no cents tolerance — not because the estimator
returns nothing, it returns a number on both sides, but because on that aliased
waveform each side locks onto a different sub-harmonic of the played 1046.5 Hz
(reference 149.5 Hz = f0/7, port 262.3 Hz = f0/4, a 969-cent gap). That figure
measures the estimator, not the oscillator. The case is carried because it is
where the port stops tracking.

Every case that *mixes* two shapes is out by 1.4 to 1.7 dB — high on the five
saw-square mixes, and 1.71 dB *low* on the one triangle-saw mix, since the sign
of the cross term differs between the pairs. It is the
reference renderer that is off, not the port. `AnalogOscillator::Render` calls
`Init()` when the shape changes, and `Init()` resets `pitch_` to `60 << 7`
*after* `RenderMorph` has already called `set_pitch` — so whichever oscillator
changed shape renders its first 24-sample block at MIDI 60 instead of the
played note, and the phase error that leaves never comes back.

In the module that is bounded and note-dependent, because `braids.cc` declares
the oscillator as a zero-initialised global: 0.0378 cycles at MIDI 45 against
0.0379 predicted, 0.0516 at MIDI 33 against 0.0517, and exactly zero at
MIDI 60, all measured. `render_braids_model.cc` declares its oscillator as a
stack *local*; it does call `Init()`, but `AnalogOscillator::Init` sets neither
`previous_shape_` nor `previous_phase_increment_`, so both stay indeterminate
and the offset it produces is about 0.166 cycles at every note. Note-independence is the tell, and the A/B shows
it: the same 50/50 saw-square mix reads +1.45 / +1.39 / +1.46 dB at MIDI
45 / 33 / 57.

The port runs all three shapes off one phase accumulator, since Braids has no
canonical offset to copy — its own depends on the note and on where TIMBRE has
been. The mixed-shape cases carry a 2.0 dB AC RMS tolerance for that reason and
say so; the fix belongs in the harness.

Both copyright lines are carried in `LICENSE` and in each source file; declared
deviations are listed in the header comment of
`plaits/dsp/engine2/morph_engine.h`, and `tests/ab.json` holds the measured
comparison against the module.
