# Sub Osc

A port of Braids' two SUB models — SQUARE_SUB and SAW_SUB — merged into one
slot. They differ only in whether the main oscillator is a square or a
variable saw, so HARMONICS turns that into a continuous axis and two Braids
models fit where one used to. At HARMONICS 0 the main oscillator *is* Braids'
`RenderSquare`; at 1 it *is* its `RenderVariableSaw`; in between the two
waveforms crossfade off one shared phase.

The DSP is Emilie Gillet's `MacroOscillator::RenderSub`. MORPH is Braids'
COLOR verbatim, including its shape, which is worth knowing: the sub level is
a **V**. Fully down gives an equal blend with the sub two octaves below,
**noon gives no sub at all**, and fully up gives an equal blend one octave
below. The sub is loudest at both ends, and Braids never lets it exceed an
equal blend.

MACRO is new — Braids welds the sub to a plain square and this narrows its
pulse. The detent and everything above it are Braids (ApplyMacro's maximum
equals its stock value); turning MACRO down from noon narrows the sub toward
12% pulse width, which Braids cannot do.

AUX carries the sub on its own at full level rather than scaled by the blend,
which would make it silent at the centre of MORPH — the first place anyone
would look for it.

Both copyright lines are carried in `LICENSE` and in each source file.

## TIMBRE is two clamps, not a line

Both Braids shapes take their width from one 15-bit integer,
`parameter_ = int16(TIMBRE * 32767)`, and both clamp it — differently, and in
opposite directions:

| | clamp | width |
| --- | --- | --- |
| `RenderSquare` | `min(parameter_, 32000)` | low fraction `(32768 − parameter_)/65536`: 0.5 down to **0.011719** |
| `RenderVariableSaw` | `max(parameter_, 1024)` | ramp spacing `parameter_/65536`: **0.015625** up to 0.499985 |

So TIMBRE runs the square's pulse *closed* as it runs the saw's two ramps
*apart*, and each end of the travel is set by a clamp rather than by the
linear law. This port reproduces both clamps on the integer. Nothing here
clamps a width against frequency, because Braids does not.

The variable saw is a comb, not a shape morph: its naive sample is
`(phase >> 18) + ((phase − pw) >> 18)`, the sum of two unit ramps `pw` apart.
At TIMBRE 0 they sit 1/64 cycle apart and null harmonic 32; at TIMBRE 1 they
sit half a cycle apart, the waveform doubles to an octave-up saw and its
amplitude halves.

## What changed since the first release — and how your patches will sound

This engine shipped built on a shape-morphing oscillator, and three things
about that were wrong. All three are fixed, and the honest summary is that
**most of this engine's parameter space now sounds different.** The region
that is genuinely untouched is HARMONICS at 0 with TIMBRE below about 0.88 —
Braids' plain SQUARE_SUB, below the pulse-width knee. Everything else moved.

1. **SAW_SUB was not ported, and it bled into the whole HARMONICS axis.** The
   old oscillator's naive sample worked out to
   `HARMONICS × ramp + (1 − HARMONICS) × pulse` — a crossfade between the
   pulse and a **plain sawtooth**. At HARMONICS 1.0 that left a bare sawtooth
   with a dead TIMBRE knob (its TIMBRE 0.0 and 1.0 renders differed by a mean
   absolute sample value of 5.9e-6, while Braids' differ by +6.01 dB and a
   full octave of apparent pitch). But the error was never confined to the top
   of the knob: *every* setting above HARMONICS 0 was crossfading in that
   plain ramp where Braids has a twin-ramp comb whose level and notch
   positions both track TIMBRE.

   **You will hear** a level drop over the whole interior. Both engines — the
   one that shipped and this one — were rendered at note 45 with MORPH at noon
   so the sub is silenced, and this is the change in OUT's AC RMS, in dB:

   | | H 0.00 | H 0.25 | H 0.50 | H 0.75 | H 1.00 |
   | --- | --- | --- | --- | --- | --- |
   | **TIMBRE 0.00** | +0.01 | −0.02 | −0.07 | −0.13 | −0.18 |
   | **TIMBRE 0.25** | −0.01 | −0.42 | −0.92 | −1.45 | −1.71 |
   | **TIMBRE 0.50** | −0.11 | −1.42 | −3.20 | −4.69 | −3.58 |
   | **TIMBRE 0.88** | −1.11 | −2.03 | −3.55 | −5.12 | −5.85 |
   | **TIMBRE 1.00** | −2.72 | −3.81 | −5.21 | −5.87 | −6.03 |

   The bottom row is safe at any HARMONICS (a twin ramp 1/64 cycle apart is
   nearly a single ramp); the left column is safe up to the pulse-width knee;
   the interior between them is 1 to 6 dB quieter. It is a **timbre** change as
   much as a level one — at TIMBRE 0.5 / HARMONICS 1.0 harmonics 2, 6 and 10
   are now annihilated by the comb, where the old plain ramp had them at −6.0,
   −15.6 and −20.0 dB; at TIMBRE 0.5 / HARMONICS 0.5 the fundamental drops
   3.2 dB and harmonics 5 and 9 drop 4.0 and 4.6 dB. Any patch above
   HARMONICS 0 wants its level and its filter re-checked, and near the top of
   both knobs it is a different sound rather than a trimmed one.
2. **The pulse-width law was wrong at the top of TIMBRE.** The old law reached
   0.02 where Braids reaches 0.011719, and diverged multiplicatively as the
   pulse narrowed (+1.11 dB at TIMBRE 0.88, +4.44 dB at 0.977, +2.56 dB at
   1.0). **You will hear:** above about TIMBRE 0.88 the pulse is now genuinely
   thinner — brighter, more nasal, and a few dB quieter into the LPG at the
   same knob position.
3. **The floor moved with pitch, and Braids' does not.** The old oscillator
   re-clamped pulse width to `[2f, 1−2f]`; above roughly MIDI 61 that, not
   TIMBRE, set the width, and above MIDI 71 TIMBRE was inert at the top of its
   travel (note 84 at TIMBRE 1.0 measured +8.35 dB AC RMS / 5.04 dB spectrum).
   **You will hear:** high notes with TIMBRE up now thin out the way low notes
   always did, instead of stalling on a fat pulse.

Three quieter changes come with them, and they reach patches that never touch
HARMONICS.

4. **The DC blocker is gone from both outputs.** DC is subtracted analytically
   now. The old 7.6 Hz corner sat *on* the two-octave sub rather than below it,
   so low subs used to lose level and phase and now keep both — note 24 with
   MORPH fully down went from −1.11 dB AC RMS against Braids to −0.01 dB. AUX
   gets the same correction, and gets **most** of it with MACRO at its detent
   or above, not with MACRO turned down: a DC blocker only ever took the
   fundamental, and a 50% square puts more of its energy there than a 12% one
   does, so narrowing the sub *shrinks* the correction. Measured on AUX with
   MORPH fully down, new against old, in dB:

   | | MACRO 0.0 | MACRO 0.25 | MACRO ≥ 0.5 |
   | --- | --- | --- | --- |
   | **note 21** | +1.24 | +2.38 | +2.72 |
   | **note 24** | +0.96 | +1.85 | +2.18 |
   | **note 28** | +0.68 | +1.32 | +1.51 |
   | **note 33** | +0.42 | +0.82 | +0.94 |
   | **note 45** | +0.11 | +0.23 | +0.26 |
   | **note 60** | −0.00 | +0.04 | +0.04 |
5. **A trigger no longer resets anything.** `Render()` used to call `Reset()`
   on every rising edge, which re-initialised both oscillators and zeroed the
   DC blocker. Matching Braids means the attack of every *triggered* patch
   changes three ways: no 0.54 full-scale DC step decaying over ~20 ms into the
   LPG, no one-block pitch glide up from the oscillator's default, and **no
   phase reset** — notes now start wherever the free-running phase is, so a
   short percussive envelope no longer produces the identical click every time.
   Inaudible on a sustained patch; on a plucky one it *is* the attack.
6. **MORPH smooths when it moves.** The V's level law is unchanged (the old
   maximum blend was 0.5, Braids' is 32766/65536 = 0.49997), so a held MORPH
   is the same sub level it always was. The blend used to be computed once per
   block and now interpolates Braids' raw COLOR integer per sample and re-folds
   the V, so a swept or modulated MORPH no longer stair-steps at the block rate.
7. **AUX drops an octave at MORPH's exact centre.** The octave now comes off
   Braids' integer — `int16(MORPH × 32767) < 16384` picks the two-octave sub —
   and MORPH at exactly 0.5 quantises to 16383, so it lands on the *low* side.
   The old test put exactly 0.5 on the high side. They disagree only over
   MORPH in `[0.5, 0.500016)`, and **OUT cannot show it** — the blend is
   exactly zero at noon, which is the point of the V. But AUX carries the sub
   at full level whatever the blend, so **AUX at a centred MORPH now sounds
   two octaves down where it used to sound one** (at note 84 its pitch goes
   524.7 Hz → 262.3 Hz). Noon is a detent, and it is exactly where you reach
   for AUX, because it is the one MORPH setting where the sub is audible on
   AUX alone. This one matches Braids, so it is a fix — but if you patch AUX
   with MORPH centred you will hear it.

The one region that sounds exactly as it did: HARMONICS 0, TIMBRE below about
0.88, note below about 61, MORPH held still and off its exact centre (item 7 —
AUX only), and no trigger.

## Declared deviations

Measured by `tests/ab.json` / `ab_engine.py` — reproducible, not asserted.
All 21 cases pass. Eighteen use the 1.0 dB / 1.0 dB / 2.0 cent baseline; three
proven measurement exceptions are encoded explicitly below. Each is paired
with a **committed control case** that isolates the cause, so the exception
cannot hide a waveform regression.

- **DC is removed analytically, and Braids leaves it in.** Each waveform's
  exact naive mean is subtracted per sample (`1 − 2·pw` for a square; the
  variable saw's mean is exactly zero). Braids' own offset reaches 0.977 of
  full scale at the top of TIMBRE, which fails the SDK's audio-health gate
  (`|DC| ≤ 0.2`) and is the LPG thump the removal exists to prevent. This is
  what gives `square-timbre-sweep` its case-specific 1.9 dB spectral limit
  (**−0.51 dB AC RMS / 1.70 dB spectrum**),
  and 1.44 dB of that 1.70 is the DC arriving in a metric the engine cannot
  answer. The two lowest analysis bands are **DC meters by construction**: a
  2048-point frame has 23.44 Hz bins, bin 1 falls inside *both* the 20–40 Hz
  and the 40–80 Hz band, and a Hann window puts a frame's DC offset into
  bin ±1 at a quarter of its bin-0 amplitude. Braids' offset ramps 0.017 →
  0.949 across this render, so those two bands hold 5.4% of the reference's
  "energy" at a note whose lowest partial is 110 Hz and whose sub is silenced,
  and this engine has nothing there: −34.36 dB and −11.78 dB.

  Two controls, both reproducible from committed tools. Strip the slow DC from
  the reference and the port measures **0.44 dB / −0.16 dB** against it, every
  band from 20 Hz to 10 kHz inside 0.4 dB — the waveform is right. Compare the
  reference against **its own** DC-detrend and the metric still reads
  **1.44 dB**, with the same −33.44 / −11.15 dB in those two bands. That is
  the floor for *any* port that removes DC, and it is above the ordinary
  1.0 dB baseline on its own. The **committed control is `saw-timbre-sweep`** — the
  identical sweep at the identical note and MORPH, on the one waveform whose
  naive mean is exactly zero at every width, so neither side has any DC to
  remove. It passes at **0.39 dB**. Filtering instead of subtracting does not
  help either: a ramp this slow (0.32 per second) leaves only 0.007 through
  even a 7.6 Hz corner. The remaining 0.26 dB is the top-octave deficit below,
  not the DC.
- **Float polyBLEP, not Braids' int16 BLEP (R8), at 48 kHz rather than 96 kHz.**
  The two *kernels* are the same function — Braids' `t * t >> 18` on a 16-bit
  `t` is stmlib's `0.5f · t²` in fixed point — so this whole deviation is the
  **rate**, and it shows in two places. Everywhere, as a top-octave deficit of
  roughly 1–2 dB above 10 kHz (a 2-sample polyBLEP spans 41.7 µs at 48 kHz and
  20.8 µs at 96 kHz), present in the passing cases too. And once the pulse is
  narrower than one 48 kHz sample — TIMBRE at maximum above roughly MIDI 61.
  `square-timbre-max-high-note` (note 84, a 0.537-sample notch) reads
  **−0.92 dB AC RMS / 1.35 dB spectrum**. It carries a case-specific 1.6 dB
  spectral limit and omits autocorrelation pitch.

  **The spectrum figure there is the reference renderer, not the engine.**
  `render_braids_model` decimates 96 → 48 kHz with a 127-tap halfband and then
  clamps to int16, and a full-scale Braids square rings *past* full scale
  through that filter — the clamp fires on 13.0% of samples even at
  `square-stock` (5.5% at the ceiling, 7.5% at the floor), where it costs the
  reference 0.17 dB, and on **47.4%** here. Render the same setting with
  `--rate 96000`, decimate in float with the same taps, and the port measures
  **0.80 dB spectrum / −1.19 dB AC RMS** against it: inside the case's own
  baseline, every band from 640 Hz to 24 kHz within 1.3 dB. The raw
  autocorrelation **cents** figure comes from the 48 kHz grid rather than the clamp (the unclamped
  reference reports the identical −2398.2): the port's period is 45.745
  samples, so a 0.537-sample notch repeats its sub-sample alignment every 4
  cycles and autocorrelation prefers lag 183 (0.9843) to lag 46 (0.9116).
  Braids' reference peaks at lag 46 (0.9703) because at 96 kHz that notch is
  1.07 samples wide and the same polyBLEP resolves it. Resolved at the
  fundamental the port reads **−1.7 cents**, inside the ±2.0 baseline;
  autocorrelation is omitted because it identifies the wrong octave.

  **The estimator is the common factor, not the notch** — and the committed
  control for that is **`saw-timbre-max-high-note`**: the *saw* at the same
  note and TIMBRE, where the width is 0.49998 and the waveform is two
  half-cycle ramps rather than a thin pulse. No notch, no clamp, spectrum
  **0.38 dB** and AC RMS **−0.22 dB** — the waveform is demonstrably right —
  while autocorrelation still reads **−2399.0**. At note 84 four periods span 182.98
  samples, so the 48 kHz grid repeats its sub-sample alignment every 4 cycles
  for any waveform with real energy near Nyquist. Both note-84 cases omit the
  misleading pitch assertion; their level and spectrum checks remain active.
- **A trigger does nothing**, matching `MacroOscillator::Strike()`, which
  touches only the digital oscillator. `square-timbre-max-retrigger` renders
  both sides at 2 Hz and measures the same numbers as the untriggered case.
- **HARMONICS and MACRO are the port's own axes.** Braids switches models with
  a discrete menu, so only HARMONICS 0.0 and 1.0 are A/B-able; MACRO's stock
  plateau (detent and above) is Braids and the rest is new. **AUX** did not
  exist in Braids' SUB models either — it is manufactured as the bare sub.
- **Braids' shape-change re-Init is not reproduced.** `AnalogOscillator::Render`
  re-`Init()`s when the shape changes, which puts `pitch_` back to MIDI 60 for
  the module's first 24-sample block. It is common-mode here — both
  oscillators change shape on the same first render — so the pair stays in
  relative lock.
