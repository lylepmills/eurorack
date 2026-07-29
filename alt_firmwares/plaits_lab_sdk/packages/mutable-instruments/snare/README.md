# Snare

A port of Braids' SNAR — a self-enveloping drum: two decaying excitation
pulses drive two resonant bandpasses at fixed intervals from the note (the
"tone"), and a decaying noise burst drives a third, higher, fixed bandpass
(the "snap"). All three sum, and the whole mix is the note's loudness
envelope — there is no separate VCA stage.

Braids writes those intervals as `+12`, `+24` and `+60` semitones, but its
SVF coefficient table is generated at 96 kHz while `RenderSnare` steps each
filter once per *two* 96 kHz samples, so every resonator lands an octave
below its written offset. What you hear is the note itself, an octave above
it, and four octaves above it — measured on the reference render at note 45
(A2, 110 Hz): peaks at 109 Hz and 221 Hz. That octave shift is Braids' own,
and this port reproduces it by transferring the same table constant at the
same call cadence.

The DSP is Emilie Gillet's `DigitalOscillator::RenderSnare`.

## What it has that Analog Snare does not

Plaits' own `analog-snare` is the module's refined descendant for this
instrument family, and it works differently. Analog Snare's `AnalogSnareDrum`
runs a bank of partials continuously spread by a `mode_spread` macro, with
an independent, continuously live `decay` knob — decay and tone are
decoupled from the played note entirely. SNAR instead hardwires exactly two
tonal partials at fixed intervals, and its decay is not an independent
control: it is derived once per strike from the played pitch and the top
half of COLOR, then held fixed for the rest of the hit. A higher note
strikes shorter, by construction, with nothing to dial that back out.

## The controls

TIMBRE sets the live balance between the two tonal resonators (the note
itself and an octave above it) — this one keeps tracking the knob live
through a decaying hit. COLOR sets the snap burst's peak level, and — only
over its top half — additionally stretches both the snap's own ring and the
tonal resonators' resonance, coupled to the same pitch-derived decay. Snare
maps those straight across to Tone and Snap, keeping the module's two axes
intact; Snap balance and Spread are the two new ones, both at their stock
position (0.5) reproducing the module exactly.

- **Snap balance** (MORPH) crossfades the tonal sum against the snap
  resonator, kick_engine.h's own crossfade idiom. Centred, both sit at
  Braids' native unweighted sum — the module exactly. 0 isolates a pure
  two-partial tone the module cannot make on its own; 1 isolates the snap
  (AUX already exposes this alone, so this reaches the same material
  through OUT without patching AUX).
- **Spread** (MACRO) scales the two tonal resonators' fixed source offsets
  (`+12`/`+24`) around Braids' own hardwired 1x: 0.5 reproduces the module's
  own voicing exactly, 0 collapses both into unison an octave below the
  played note, 1 doubles the offsets so the two sit two octaves apart. The
  noise resonator is untouched. Braids never varies this at all — it is two
  hardcoded constants.

## Rate

RenderSnare ends in `size -= 2`, writing the same computed sample twice — a
literal zero-order-hold duplication (particle-burst's own pattern), not a
linear interpolation and not Kick's "duplicated write but the filter still
advances every sample" case. Every stateful call in the loop body — all
four excitation pulses and all three resonators — runs exactly once per
iteration, so the whole algorithm already runs at Braids' 48 kHz-equivalent
cadence, which is Plaits' own native render rate. Unlike Kick, whose
resonator genuinely runs at Braids' native 96 kHz and needs its coefficient
re-derived at Plaits' own (lower) rate as an accepted approximation, SNAR's
three resonators never leave 48 kHz-equivalent on either side, so their
coefficient tables transfer at their own generating constant with no
re-derivation and no oversampling — reproduced to under 1 LSB against the
real generated tables (details in the header comment).

## Trigger behaviour

Braids fires SNAR once, unprompted, the instant the model is selected
(`DigitalOscillator::Init()` sets its strike flag). This port follows
`struck_drum_engine.cc`'s established idiom: an unpatched TRIGGER input
fires exactly one hit on load, then stays silent until a real trigger
arrives — it does not free-run.

## Pitch and decay are coupled, and both are snapshotted

COLOR's decay contribution and the tonal resonators' resonance are read
once, at the instant of the strike, from whatever pitch and COLOR are
current *then* — not smoothed, and not re-read as the hit rings down. A
COLOR turn mid-decay changes the *next* hit, not the one currently ringing.
This is genuine Braids behaviour (`digital_oscillator.cc:2399-2418`) and is
reproduced as a hard snapshot, not smoothed into a fade.

## Measured

`tests/ab.json`, six cases, all against Braids' own reference decimated to
48 kHz, all inside the guide's standard targets with no widened tolerance:
AC RMS within 0.01-0.82 dB, spectrum within 0.19-0.67 dB. On the five
note-45 cases every band below 10 kHz sits within 0.3 dB.

What is left is one residual with a closed form. Braids' reference writes
the same sample twice at 96 kHz and is then decimated, so it carries the
duplicate-write's own response `|cos(pi*f/96000)|` where this port computes
each sample directly — predicting the port reads high by 1.09 dB at 15 kHz
and 3.0 dB at 22 kHz. Measured: +1.03 to +1.12 dB in the 10.24-20.48 kHz
band and +3.04 to +3.10 dB above that, across every case. Nothing else is
hiding under it. This is the same declared residual
`particle_burst_engine.h` documents.

Getting there needed one quantisation Braids does and an earlier draft of
this port did not: `state_ = state_ * decay_ >> 12` in `braids/excitation.h`
floors the excitation state every sample, and because the snap envelope's
coefficient is near unity that floor terminates it about 29 dB above where a
pure float multiply would. Adding it moved every case at once — spectrum
1.12→0.39, 0.55→0.19, 1.72→0.42, 1.10→0.39, 1.15→0.40, 0.94→0.67 dB — and
is what brought the measured top-octave tilt into line with the `|cos|`
prediction above.

The loosest case is `high-note-shorter-decay` (note 78) at +0.82 dB AC RMS,
growing smoothly with note from +0.07 dB at note 45. That one is the
per-step `>> 15` truncation inside Braids' own SVF, which this port does not
reproduce; it is not a spectral-shape error (0.67 dB spectrum on that case).

CPU: `qemu/estimate.py --sweep` first measured this engine at 87-128% of the
module's budget — the three `sinf` calls needed to retune each resonator,
one per sample, dominated the cost even though Braids itself only retunes
them once per `RenderSnare()` call. Reading Spread (MACRO) at block rate
instead of interpolating it per sample — matching Braids' own once-per-call
retuning, and costing nothing audible since a hand-turned macro stepping at
Plaits' sub-millisecond block rate is inaudible — cut that to 42-62%. The
excitation truncation above adds 16 instructions/sample back, for a current
estimate of 45-66%. Not yet measured on hardware
(`build --hardware --cpu-probe`).

Full deviations, the exact line-by-line parameter mapping, and the table
verification (under 1 LSB against the real generated `lut_svf_cutoff` /
`lut_svf_damp`) are in the header comment of
`plaits/dsp/engine2/snare_engine.h`.
