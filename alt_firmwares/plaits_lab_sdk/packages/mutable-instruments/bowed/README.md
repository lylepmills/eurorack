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
upward at HARMONICS 0, and that is exactly where the port's one-sample tap
clamp takes over. `low-fold` is misnamed and does not reach the octave
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

**All fifteen declared cases agree.** Level runs −2.88 to +4.89 dB against
Braids, against the declared 1.6× (+4.08 dB) make-up; octave-band spectra sit
0.05 to 2.01 dB apart; pitch stays inside ±3 cents. The sixteenth,
`high-bridge-clamp`, still declares no tolerance and still counts as neither,
because both sides really are dead there — Braids −61.0 dBFS, the port −59.0 —
but it now reads 0.62 dB of spectrum rather than 10.7.

## The wave 1 defect, and what it changes

Wave 1 shipped this engine interpolating its delay lines in float. Braids does
not: its fractional read is `Mix(a, b, frac) << 8` against int8 line storage,
and `stmlib::Mix` is `(a * (65535 − balance) + b * balance) >> 16` returning an
int16 — so the interpolation between the two taps is **floored to whole int8
counts**, 1/128 of full scale, before the shift. Inside a stick-slip feedback
loop that truncation is a loss term, and it is what lets the bow **slip**.
Without it the model has no slip regime at all. The port now reproduces the
integer expression exactly.

Braids is *bistable* along TIMBRE: at note 45, HARMONICS 0.5 it collapses to
−44 dBFS at TIMBRE 0–0.075 and again at 0.175–0.225 while bowing normally
between, and it collapses at mid bow force too given the right note and bow
position — at note 24, TIMBRE 0.5, HARMONICS 0.0 it falls to −49 dBFS. The
shipped port was flat across all of it, −14 to −7 dBFS, missing every collapse
by up to 37 dB. It now collapses where the module collapses: −41.4 dBFS at the
first of those points and −46.4 at the second.

**So an existing patch will sound different**, and not by a rounding margin. If
it sits in one of the collapse bands it goes from a sustained full-level bowed
tone to a thin near-silent whistle, roughly 30 dB quieter — which will read as
the note dropping out. That is the module's own behaviour, and the reason the
control copy says "from a thin whistle to a hard scrape", but a patch parked in
a band will need TIMBRE moved off it.

**And it is not only the bands.** The truncation is a loss term everywhere in
the loop, so ordinary bowing settings move too — by up to about 5.5 dB, in both
directions. Measured port level before → after, at points where Braids bows
normally and nothing collapses:

| setting | Braids | port before | port after | move |
| --- | --- | --- | --- | --- |
| note 45, TIMBRE 0.75, HARMONICS 0.5 (`firm-pressure`) | −16.4 | −8.5 | −14.0 | 5.5 dB quieter |
| note 60, TIMBRE 0.4, HARMONICS 1.0 (`mid-neck`) | −17.5 | −9.2 | −13.9 | 4.8 dB quieter |
| note 60, TIMBRE 0.5, HARMONICS 1.0 | −11.9 | −12.9 | −8.7 | 4.3 dB **louder** |
| note 24, TIMBRE 0.4, HARMONICS 0.0 | −19.5 | −13.0 | −16.3 | 3.3 dB quieter |

Every one of those moved *toward* Braids — that is the point of the fix — but a
patch at firm bow pressure changing by half its perceived loudness is not a
rounding margin. The settings nearest stock move least: of the sixteen A/B
cases, seven move under 0.7 dB, `retrigger` 1.0 and `octave-fold` 1.6,
`mid-neck` 4.8 and `firm-pressure` 5.5, and the five collapse or dead corners
19 to 33 dB.

Two smaller quantisation corrections landed with it, each under 0.4 dB on every
case *except* `high-bridge-clamp`, which the clamp change carries from +7.96 dB
level / 12.69 dB spectrum to +3.45 dB / 0.62 dB — two residuals converging in a
corner that is dead on both sides, not a fidelity gain worth quoting. The
friction curve is now floored to the integer grid `lut_bowing_friction`
actually holds (floor matches all 257 entries; round matches 137; the closed
form now reproduces 255 of the 256 reachable indices exactly, index 27 alone
still one LSB high from float32 rounding). And the
bridge-tap clamp dropped from two samples to one, and moved ahead of the neck
split so it shifts the tap instead of lengthening the loop — which matters
between MIDI 72.9 and 84.5 at HARMONICS 0, where the engine used to run flat by
up to 36 cents and now tracks. A patch tuned by ear against that flatness will
find itself in tune — but keep that in proportion. The clamp only ever engaged
with the bow hard against the bridge at the top of the keyboard (HARMONICS has
to be at 0 for the MIDI 72.9 figure; by HARMONICS 0.1 the old clamp did not
engage below MIDI 85), and Braids renders −44 to −61 dBFS across that whole
corner. A patch parked there is one the Mix fix also drops by 20–30 dB: at note
82, TIMBRE 1.0, HARMONICS 0.0 the port goes −22.6 → −49.6 dBFS while its pitch
error goes −33.5 → +3.8 cents. The note nearly vanishing is what a user will
notice there; the tuning is the footnote.

Both copyright lines are carried in `LICENSE` and in each source file.
