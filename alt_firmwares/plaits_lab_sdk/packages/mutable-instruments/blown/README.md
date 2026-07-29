# Blown

A port of Braids' BLOW — a blown bore driven by a nonlinear reed, with the
turbulence of the breath as its main timbre control.

The DSP is Emilie Gillet's `DigitalOscillator::RenderBlown`
(`braids/digital_oscillator.cc:1303-1362`).

## What the model is

One delay line, one reflection, one reed. The bore is a single line read at a
fractional delay and closed by an inverting reflection of `-3891/4096`, so it
behaves as a stopped pipe and voices odd harmonics — measured on the reference
at MIDI 45, the even harmonics sit at the noise floor, 42 to 54 dB under the
odd ones either side of them. The excitation is a static breath pressure of
`26214/32768` multiplied by white noise, and the reed is a clipped line of
slope `-1229/4096` about an offset of `22938/32768` whose output multiplies the
pressure difference across it.

There is no excitation envelope. The pressure is a constant, which is why the
model sustains for as long as you hold it, and why a trigger only flushes the
bore.

## Controls

`TIMBRE` is Braids' `parameter = 28000 - (parameter_[0] >> 1)` (line 1322) —
the depth of the noise riding on the breath. It runs backwards, and it is a
loudness control as much as a timbre one, because turbulence disrupts the
standing wave. Measured at MIDI 45 with COLOR at noon, `TIMBRE 0.0` renders at
−31.1 dBFS AC RMS and `TIMBRE 1.0` at −6.3 dBFS: 24.8 dB across one knob.

`COLOR` moves the index into the body filter by up to +127 semitones (line
1324): the whole sum is shifted right by 7, so COLOR's contribution is
`parameter_[1] >> 8` — one index step per 256 counts. The filter is
**outside the loop** — line 1355 reads the bore's output
and never writes back into the line — so COLOR equalises the sound without
changing what the pipe is doing.

Blow and Reed are the two the module cannot reach: the static breath pressure
and the reed's rest offset, both constants in Braids, both stock at noon.

## A measured property of COLOR

The index is `(pitch_ - 8192 + (COLOR >> 1)) >> 7`, clamped to 0..127, and the
table saturates at entry 79. So COLOR's live travel depends on the note and is
never the whole knob: at MIDI 45 the index leaves 0 at COLOR 0.1563 — the raw
sum crosses zero at 0.1484, but the `>> 7` holds the index at 0 until the sum
reaches 128 — and hits the
ceiling at COLOR 0.766. Verified by rendering the module — COLOR 0.0, 0.10,
0.14, 0.15 and 0.156 come out bit-identical and 0.157 differs, as do 0.766,
0.78, 0.80 and 1.0 while 0.75 differs. The port
reproduces it rather than spreading the knob out, and the `body` scenario
sweeps the live range instead of the whole travel.

## Where Blow and Reed can go

Both new macros are bounded by the model's oscillation window, which is narrow
and closed at *both* ends. Linearising the loop about its static fixed point
puts the self-oscillating range at reed offset 0.53–0.79 and blow 0.55–1.04;
past the top of either, the reed's operating point reaches its own clip, the
loop linearises at a gain of 0.950, and the pipe stops speaking rather than
overblowing. Rendering confirms both walls and puts the upper one a little
lower than predicted: at reed offset 1.02 the engine measures 0.0001 RMS, at
0.78 still only 0.018.

Inside the window they do real spectral work. Held at MIDI 45 with TIMBRE 0.7,
Blow runs −33.4 dBFS at a 3430 Hz centroid, through −12.2 dB / 1826 Hz at noon,
to −17.3 dB / 2478 Hz at its ceiling — blowing harder brightens the tone while
barely moving its level. Reed runs −29.9 dB / 3868 Hz, through −9.8 dB /
1716 Hz just below noon, to −28.4 dB / 3439 Hz, thinning back into breath as
the reed pins at either end.

## Tables

RenderBlown reads exactly one, `lut_flute_body_filter`. It does **not** read
`lut_blowing_envelope` or `lut_blowing_jet` — those belong to `RenderFluted`
next door (lines 1419 and 1445), and BLOWN has no excitation envelope at all.

The table reproduces at 0 LSB across all 128 entries from its generator in
`braids/resources/lookup_tables.py:171`,
`floor(4096 * min(0.7, 0.4 * 2^((n - 69) / 12)))`. Rounding instead of flooring
misses by 1 LSB, so the floor is the reading that reproduces, and the port
stores the truncated integers rather than evaluating the closed form — the
truncation is proportionally largest exactly where the values are smallest
(−1.46% at n = 0, −0.02% at n = 48).

`RenderFluted` indexes that same 128-entry table with a raw
`lut_flute_body_filter[pitch_ >> 7]` while `Render()` clamps `pitch_` only to
`140 * 128`, so it reads past the end above MIDI 127. RenderBlown computes the
same index and clamps it to 0..127 first. Blown is on the safe side of that
bug.

## Rate

`RenderBlown`'s loop is `while (size--)` with no `size -= 2` — compare
`RenderBowed`, which has one at line 1285 — so it is a 96 kHz algorithm with no
internal oversampling. The port runs 2x from 48 kHz to reach the same 96 kHz
internal rate, and the bore delay, the half-sample averaging filter inside the
loop and the body-filter one-pole all transfer verbatim. The averager forced
the decision: its zero sits at the internal Nyquist, so a 48 kHz port would
have damped the upper harmonics twice as hard *inside the feedback path*, which
no gain correction can undo.

The pitch checks out against that reading. Braids' bore delay is half a period
less one sample at 96 kHz, and the averager adds half a sample, so the round
trip is a period less one sample and the model plays slightly sharp. At MIDI 45
that predicts 110.13 Hz against a nominal 110.00; the reference renders
110.129 Hz. The port keeps the same `1 / frequency - 1` and inherits the same
sharpening.

The 96 → 48 kHz decimator is a 31-tap Kaiser halfband, chosen by measurement
against the reference renderer's 127-tap Blackman-Harris on the case with the
most energy above 24 kHz: 0.11 dB of spectral difference, against 0.40 dB for a
19-tap and 0.05 dB for a 51-tap.

## The A/B is mostly triggered, and here is why

BLOWN is a noise-driven chaotic self-oscillator. Which limit cycle it settles
into depends on the noise realisation, so a held render of it is not
reproducible even against itself: twenty renders of the **module**, differing
only in RNG seed, span 2.59 dB of AC RMS and up to 8.49 dB of pairwise spectral
difference at MIDI 45 with both knobs at noon, and 10.49 dB / 9.86 dB with
COLOR at its floor. Longer renders make it worse, not better, because the model
wanders rather than averaging.

Retriggering fixes it — a strike flushes the bore, so both sides then average
over many independent settlings — and the same twenty-seed measurement at
`triggerHz 8` gives at most 1.18 dB of AC RMS spread and 0.69 dB of spectral
difference. The parameter-axis cases are therefore triggered, and against that
— across four different RNG seeds for the port — they read −0.49 to +0.43 dB of
AC RMS, 0.14 to 1.21 dB of spectrum and −0.3 to +0.2 cents. Two held cases are
kept to record the phenomenon, with tolerances taken from the
module-against-itself measurement rather than from what the port happens to do.

Two cases carry no cents tolerance. `ab_engine`'s f0 estimator is a 0.25 s
autocorrelation, and at body-dark (a 112 Hz-cornered tone with almost nothing
above the fundamental) and at MIDI 21 (four cycles per window) it fails on the
**module**: across the twenty reference seeds its own reading spans 5019 and
7420 cents, pinning at the 2000 Hz f_max on some of them. `low-sustained`,
where the module's own reading spans 0.05 cents, carries the low-octave pitch
check instead.

## Is this a re-skin of reed-pipe?

Not a re-skin, but the honest answer is narrower than "a different instrument".
Both are a reed driving one delay line closed by an inverting reflection, and
at settings where both are steady and mid-bright they occupy the same
territory.

Two things are not reachable from reed-pipe. Its breath noise has the same
*form* as Blown's — a term scaled by the mouth pressure — but its depth tops
out at 0.013, while Blown's *bottoms* out at 0.355 and reaches 0.855: 27 to 66
times reed-pipe's maximum. That depth is Blown's TIMBRE, and it is what
produces the 24.8 dB level swing and the airy, barely-speaking bottom of the
knob. And reed-pipe's tone filter
is *inside* the loop (`return_filter_` feeds `returning`, which feeds
`outgoing_`, which is written back to the bore), while Blown's is outside it —
so the two engines colour the sound at different places in the signal path, and
only reed-pipe's colour control changes what the pipe itself does.

Both copyright lines are carried in `LICENSE` and in each source file; declared
deviations are listed in the header comment of
`plaits/dsp/engine2/blown_engine.h`, and `tests/ab.json` holds the measured
comparison against the module.
