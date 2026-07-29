# Kick

A port of Braids' KICK — a self-enveloping drum: two decaying excitation
pulses of opposite polarity drive a resonant bandpass tuned to the played
note, which jumps up 17 semitones for the first ~4 ms of every hit, then a
self-brightening ("punch") dynamic filter modulation, all smoothed through an
output one-pole.

The DSP is Emilie Gillet's `DigitalOscillator::RenderKick`.

## What it has that Analog Bass Drum does not

Plaits' own `analog-bass-drum` is the module's refined descendant for this
instrument family, and it gets its click a different way. Analog Bass Drum's
click is FM — an oscillator briefly modulates its own frequency. Kick's click
is filter-side: a Chamberlin state-variable bandpass whose centre frequency is
literally retuned up for the first few milliseconds of the hit, driven by two
fixed-decay pulses rather than an FM operator, and whose own coefficients
self-modulate from the filter's recent output level. A resonator being
retuned out from under a fixed exciter sounds different from an oscillator
retuning itself, and that's the reason to have both.

## The controls

TIMBRE sets the resonator's decay (`Svf::set_resonance`, driven by a cubic-ish
falloff off TIMBRE) and COLOR sets the coefficient of the *output* one-pole
the resonator's sum passes through before it reaches the buffer — so COLOR is
brightness of the final mix, not a property of the resonator itself. Kick maps
those to Decay and Tone, keeping the module's two axes intact; Balance and
Punch are the two new ones, both at their stock position (0.5) reproducing the
module exactly.

- **Balance** (MORPH) crossfades the two excitation pulses against each
  other. Centred, both sit at Braids' native level — the module exactly.
  Sweeping either way fades one pulse out, leaving just the sharp positive
  click or just the low negative thump. The module hardwires both pulses at
  unity; this balance is not reachable on it.
- **Punch** (MACRO) scales the self-brightening dynamic filter modulation
  around Braids' one hardcoded amount: 0.5 reproduces the module's fixed punch
  exactly, 0 turns it off (a plain, undriven resonant filter), 1 pushes it to
  4x. Braids never varies this at all — it's a single hardcoded call.

## Rate

RenderKick ends in `size -= 2`, so its excitation generator — both pulses'
countdowns and the pitch/resonance pick — runs at a 48 kHz-equivalent cadence,
and by the port's rate rule that timing transfers verbatim. The resonator
itself does not share that rate: `Svf::Process` is called twice per iteration
with the same excitation sample held across both calls, but the filter's own
recursion state genuinely advances each call, so it runs at Braids' native
96 kHz off a zero-order-held 48 kHz excitation — not a duplicated write the
"ends in size -= 2" shorthand would suggest on its own. The filter's
coefficient table is generated against a 96 kHz denominator (unlike
particle-burst's own resonator table, which was already calibrated for a
48 kHz port), so transplanting it verbatim would tune the resonator flat by
close to an octave (this port's rate is half Braids' native rate).

This port does not oversample to reach that native rate. It re-derives the
coefficient at Plaits' own rate from the same closed form the table came
from, evaluated through `NoteToFrequency` (carrying the usual pitch
correction) instead of Braids' 96 kHz denominator — chosen because the
reference this port is measured against is already the 96 kHz signal
decimated to 48 kHz, and the excitation driving the resonator is a decaying
pulse pair with negligible energy near the port's Nyquist for the zero-order
hold to matter. The output one-pole sits in the same twice-per-iteration
block the filter does, so it needs the same treatment: Braids' coefficient
`k` applied twice decays by `(1-k)^2` per iteration, so the port solves for
the single-step equivalent `k' = 1 - (1-k)^2` rather than transplanting `k`.
Skipping that (an earlier pass of this port did) measured -3.7 dB AC RMS at
COLOR=0, where `k` is small enough for the missed factor of ~2 to matter.

There is a third place, and it is the subtlest: PUNCH — the filter's
self-modulation — adds a term straight into that same coefficient `f`. An
addend to a rate-derived coefficient is rate-derived too, so it needs the
same factor of two. An earlier pass of this port left it out, which halved
the whole PUNCH gesture and left the resonator flat by about 35 cents at
note 36 and 90 cents at note 24. Fixing it took the full-band spectrum
difference from 0.42–0.87 dB to 0.10–0.20 dB across all five cases, and AC
RMS to within 0.22 dB. The general rule the two bugs share: re-deriving a
filter coefficient at a new sample rate is not finished until every term
summed into it has been re-derived too.

Pitch is not part of the measured comparison, but not because the model has
no pitch. PUNCH does bend the resonator upward while a hit is loud, so each
hit glides — at note 60 with a long decay, Braids' reference runs 271.3 Hz
just after the strike down to a settled 265.1 Hz, about 40 cents — and then
holds a constant centre frequency for the rest of the hit, which the port
tracks to about 10 cents. What makes the harness's own `cents` figure
unusable here is its fixed mid-file window: on a trigger-and-decay model it
lands in a tail 50–90 dB down and locks onto something that is not the
resonator, reporting the same number for both sides even when the port was
genuinely tens of cents off. `tests/ab.json` says so and omits the `cents`
tolerance accordingly; the pitch check is done out of band instead.

Full deviations, the exact truncated PUNCH constant, the line-by-line
parameter mapping, and the full measured comparison are in the header comment
of `plaits/dsp/engine2/kick_engine.h`; the numbers themselves are in
`tests/ab.json`.
