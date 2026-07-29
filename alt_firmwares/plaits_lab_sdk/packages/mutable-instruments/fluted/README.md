# Fluted

A jet-flute engine derived from Braids' FLUT — a bore delay line and a jet
delay line closed into one loop, driven by an asymmetric cubic jet
characteristic and a breath pressure the noise multiplies.

The DSP is Emilie Gillet's `DigitalOscillator::RenderFluted`
(`braids/digital_oscillator.cc:1367-1461`).

**Derived from, not a port of.** Wave 1 of this port measured FLUT at 37 cents
of pitch error and dropped it, with the written conclusion that the model
cannot be reproduced at 48 kHz. That conclusion was right about the arithmetic.
What it missed is that the engine does not have to run at 48 kHz. Nothing here
claims bit-fidelity; the deviations are listed at the end of
`plaits/dsp/engine2/fluted_engine.h` and the measured A/B is in
`tests/ab.json`.

## Why it was dropped, and what fixed it

`RenderFluted`'s loop is `while (size--)` with no `size -= 2`, so it is a
96 kHz algorithm and its rate constants do not survive a move to 48 kHz. Three
of them break, and all three sit inside the feedback path where no make-up gain
can reach them:

- `(delay_ << 1) - (2 << 16)` subtracts two **samples** of loop delay, not a
  fraction of a period.
- the reflection one-pole's group delay is `(1 - k) / k` **internal samples** —
  at MIDI 45 the table index is 45, `k = 409/4096`, so 9.01 of them: 0.094 ms
  at 96 kHz and 0.188 ms at 48 kHz.
- the in-loop DC blocker's pole of 0.98999 is a corner of 152.9 Hz at 96 kHz
  and 76.5 Hz at 48 kHz, and its phase at the resonance is a lead: +54.4° at
  110 Hz against +34.9°, a 19.5° swing that is 0.054 of a period of loop delay
  appearing out of nowhere.

The last two are filters. You cannot re-derive a filter's phase into a delay
constant, so the only honest fix is to run the waveguide at the rate it was
written for. This engine takes two internal steps per 48 kHz output sample —
the same thing the landed `blown` engine does — and decimates through a 31-tap
Kaiser halfband. Every constant above then transfers verbatim.

Measured against `braids/test/render_braids_model --rate 48000`, held at the
module's deterministic corner (TIMBRE 1.0, COLOR 0.5), reading f0 as the
strongest spectral peak:

| note | module | engine | raw | less R6's 4.61 c |
|-----:|-------:|-------:|----:|-----------------:|
| 33 | 275.303 Hz | 275.935 Hz | +3.97 c | −0.64 c |
| 45 | 331.232 Hz | 332.314 Hz | +5.65 c | +1.04 c |
| 57 | 659.904 Hz | 661.762 Hz | +4.87 c | +0.26 c |
| 69 | 447.709 Hz | 448.582 Hz | +3.37 c | −1.24 c |
| 81 | 887.704 Hz | 890.210 Hz | +4.88 c | +0.27 c |

The residual after the flat 4.61-cent R6 subtraction is not error either. R6
shortens the delay **line** by 0.266%, but two fixed samples and the phase of
two filters are also in this loop and do not scale, so the pitch moves by less
than the line does and by a register-dependent amount. Rendering the engine at
`note − 0.0461` semitones, so its lines match the module's exactly, the
residual is **−0.093 to +0.018 cents** across MIDI 33.5 to 81.5. The loop is
right; the spread in the table is R6 propagating through a partly rate-fixed
loop.

One thing the table is not: the module does not play the written note. FLUT
overblows — MIDI 45 speaks at 331 Hz, MIDI 33 at 275 Hz — and the engine
reproduces that, register jumps and all.

## A bug in the Braids source, fixed and declared

Line 1404 is `lut_flute_body_filter[pitch_ >> 7]`. That table has 128 entries,
and `Render()` clamps `pitch_` only to `kHighestNote = 140 * 128`, so any note
above MIDI 127 reads up to 13 entries past the end. `RenderBlown` reads the same
table off a `pitch_ >> 7` of its own — a different index, shifted down 64
semitones and moved by COLOR, but the same arithmetic — and clamps *that* to
0..127 first, which is what proves the intent. This engine clamps.

The fix cannot change the sound anywhere the module is defined: the table
saturates at its ceiling from entry 79 onward, so every in-range note at or
above MIDI 79 already reads 2867 — and so would every out-of-range one, if the
table continued.

## How it differs from Reed Pipe and from Blown

Plaits' reed-pipe and Braids' BLOWN are both one delay line closed by an
inverting reflection and driven by a **reed**: a clipped straight line whose
output multiplies the pressure difference, odd-symmetric about its operating
point. Fluted is a different loop and a different excitation.

- **Two delay lines in series**, with COLOR moving the split. The loop has an
  internal boundary the reed models do not have.
- **A jet, not a reed.** The characteristic is a fixed `min(d³ − d, 1)` — it
  dips to −0.385 at d = 0.577, crosses zero at d = 1 and saturates flat above
  d = 1.33 — read *directly*, not used as a multiplier on a pressure
  difference. It is strongly asymmetric and non-monotonic; a clipped reed line
  is neither.
- **Multiplicative breath noise on an envelope.** The noise scales the breath
  rather than adding to it, so turbulence vanishes as the breath does, and the
  breath itself has a 1.67 ms attack and a 3.33 ms decay to 80% before it
  holds. BLOWN's breath is a constant with additive noise and no envelope,
  which is why BLOWN sustains identically forever and this does not.
- **8-bit delay lines.** Braids declares FLUT's bore and jet as `int8` where
  BLOWN's bore is `int16`, so the loop carries about 48 dB of quantisation
  noise as part of its sound. This engine keeps them.
- **An in-loop DC blocker at 152.9 Hz**, high enough to be a tone control
  rather than hygiene, which no reed model here has.

Measured against the landed `blown` engine, held at MIDI 45 with all four
macros at noon: 12.75 dB of energy-weighted spectral difference, a spectral
centroid of 9719 Hz against Blown's 2717 Hz, and they do not speak in the same
register — Fluted's strongest partial is 333 Hz where Blown's is 110 Hz.

### One thing that is *not* the difference

The clamp at `digital_oscillator.cc:1438-1440` reads like a half-wave
rectifier, and it is tempting to say that is what makes a flute sound like a
flute. It is not, and this engine's first draft spent a macro on making the
clamp continuous before finding that out — swept end to end at MIDI 45 the AC
RMS, the spectral centroid and the strongest partial came out identical to
three decimal places at every position. At the module's own operating point the
jet flow is DC-biased well clear of zero and the clamp almost never fires:
instrumented over 3 s at MIDI
45, it catches **0.000%** of samples at TIMBRE 0.9, 0.040% at TIMBRE 0.5,
0.325% at TIMBRE 0.0, and 7.9% only at the extreme corner. When it does fire it
is mostly because the int8 jet line has *wrapped*, not because the flow
reversed. What shapes this model's spectrum is the asymmetry of the cubic over
a positive-biased range.

## The controls

| macro | label | what it is |
|---|---|---|
| HARMONICS | Embouchure | Braids' COLOR: the jet/bore split of one fixed loop length, 18.75% to 30.86% jet. Moves the embouchure without moving the pitch. |
| TIMBRE | Air | Braids' TIMBRE: breath turbulence, 51.3% depth down to 1.3%. Runs backwards, as the module's does. |
| MORPH | Blow | Breath pressure, welded at twice the excitation envelope in Braids. |
| MACRO | Body | An offset on the note index Braids uses to pick the reflection filter's corner, which Braids gives no knob at all. |

Blow and Body are the two Braids does not have, and both are stock at noon.
Neither is what the Plaits neighbour spends its knobs on: reed-pipe puts breath
pressure on TIMBRE, reed stiffness on MORPH and the reflection coefficient on
MACRO, and has no jet and no note-tracking body filter. Note that BLOWN's body
filter is an *output* filter, never fed back — the same table does a different
job here, inside the loop.

Both ranges are narrower than they might be, and both are measured rather than
chosen. This is a self-oscillator held at one operating point, and pushing it
far off that point stops the oscillation instead of changing it: Blow is alive
from 0.90 to 1.30 and drops to −38 dBFS at 0.80 and −48 dBFS at 1.40, while
Body speaks down to −12 semitones (−7.25 dBFS at MIDI 33) and goes quiet at
−18 (−31.9 dBFS). Both ranges
stay inside what was measured. Across them the tone really moves — at MIDI 45
Blow runs a 6007 Hz spectral centroid at its floor through 6734 Hz at noon to
7203 Hz at its ceiling, and the register jumps with it, which is the flute
gesture the module cannot make.

## Outputs

**OUT** is the bore pressure at the far end, which is what the module emits.

**AUX** is a second tap halfway along the same bore — mono and stereo alike, so
in stereo the pair is one standing wave heard at two points. It is level-matched
to OUT (within 0.01 dB at every note measured) and its peak tracks OUT's (worst
measured 0.9318 against 0.9315, over notes 0 to 110 crossed with every corner of
all four macros), but it is not the same signal: the two correlate between −0.11
and +0.12 across MIDI 33, 45, 57, 69 and 81.

The midpoint was picked by measurement. A fixed fraction of the bore sets a
phase that scales with the mode number, so most fractions collapse toward mono
or antiphase at some note: 0.35 of the bore gives +0.78 to +1.00, 0.42 gives
−0.67 to +0.68, 0.55 gives −0.75 to +0.96, and 0.68 gives −0.83 to −0.98. Only
the midpoint stays decorrelated everywhere.

The obvious alternative for this model is the **jet line** — the flow at the
embouchure rather than at the end of the bore — and it is a genuinely different
sound: 6.2 to 7.7 dB quieter than OUT with a spectral centroid 870 to 2200 Hz
higher. It was measured and rejected on peaks. The jet line carries a large DC
bias and the decimator's worst-case gain is 1.5, so over the same macro sweep
its peak reached 2.03 against OUT's 0.93, and no fixed trim both bounds that
and leaves AUX usable.

## What the A/B reads

Ten cases, all inside tolerance: AC RMS −0.26 to +0.87 dB, spectrum 0.09 to
1.29 dB, pitch −0.8 to +0.7 cents. Eight of the ten sit at or under 0.40 dB of
spectrum.

The one outlier is COLOR at its ceiling, and it gets an explanation rather than
a wider tolerance everywhere else. The identical case at MIDI 45.5 instead of
45 reads 0.16 to 0.31 dB against a module-against-itself spread of 0.22 dB —
so nothing systematic is wrong — and the **module compared against itself two
cents away** at MIDI 45 reads 1.24 dB of spectrum and 0.76 dB of AC RMS. At
that COLOR the jet takes 30.86% of the loop and the model sits near a mode
boundary where a couple of cents of bore length moves the balance that much.
R6 moves the bore by 4.61 cents whatever the port does.

The cents tolerance is 1.5 rather than 1.0, for two measured reasons: the f0
estimator's own reading spans 0.2 to 1.6 cents across eight renders of the
*module* alone, and the flat 4.61-cent R6 subtraction is only approximate for a
loop that is partly rate-fixed. The delay-matched figure above — under a tenth
of a cent — is the real number. The maximum-turbulence case declares no cents
tolerance at all, because at TIMBRE 0 the estimator finds no usable f0 on the
module either.

`tests/ab.json` carries every number and the reasoning behind each tolerance.
