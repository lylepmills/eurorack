# Measuring and improving engine performance

Plaits gives your engine roughly **1,500 CPU cycles per sample** (72 MHz ÷
48 kHz) — and that budget also covers the low-pass gate, output stage, UI and
ADCs. In practice an engine is healthy below **~75%** and at risk above
**~90%**: the measurement brackets only the synthesis path, and the rest of
the interrupt's work lands on top.

## The three measurement tiers

| tier | command | what it tells you |
|---|---|---|
| smoke test | `check --full` | catches only pathologically expensive engines; a host timing cannot predict hardware |
| calibrated estimate | `qemu/estimate.py <pkg> --sweep` | emulated instruction count × a model fitted to 16 real engines: a **band**, e.g. "79% (68–101%)", worst case across knob positions |
| hardware truth | `build --hardware --cpu-probe` | the chip's own cycle counter around the real render, in the real interrupt |

The estimate's accuracy, validated by predicting each calibration engine from
a fit that excludes it: mean error 14%, all 16 within ~30%. Trust it for
direction; confirm on hardware before shipping.

## Reading the hardware probe

**LEDs (no patching needed):** a bottom-up bar, one LED per eighth of budget,
with the next LED's *brightness* showing the fraction (~1% resolution).
Green below 62.5%, amber above, **all-red blinking above 90%** — red means
the audio deadline is at risk in practice, calibrated against builds that
audibly crunch. Aim for green with at most one amber.

**AUX tone (precise numbers):** each report is a burst count (which value),
a fixed reference tone, and a value tone. Two encodings ride together: the
tones' frequency (2500 Hz + value), and the value-to-reference **duration
ratio** — the ratio survives overload exactly, because everything stretches
by the same factor. When the two disagree, trust the ratio; when in doubt,
trust the LEDs.

## What costs what (measured on this chip)

- **Cheap:** table lookups (~1 cycle-per-instruction code), straight-line
  arithmetic, memory traffic in general.
- **Expensive:** float compares in loops (`VCMP`+`VMRS` stall the core),
  float divides, long dependent accumulation chains, and above all
  **per-sample work that only needs doing per block**.

## The playbook, with real numbers

The Helix engine went from 145% to 61% of budget in three moves, sound
byte-identical at each step:

1. **Hoist per-parameter work out of the sample loop** — envelopes, filter
   coefficients, per-voice gains and frequencies, every `exp`/`log`/`pow`
   computed once per block. (Helix: 3.7× cheaper.)
2. **Split long accumulator chains** — four partial sums instead of one
   serial `sum +=` lets the FPU pipeline overlap independent work.
3. **Refresh coefficients at half rate** — parameters move slowly; alternate
   blocks can reuse the previous coefficients. Refreshing at 2 kHz instead of
   4 kHz is inaudible and returned 22% of the whole budget.

And one measured non-lesson: replacing table lookups with polynomial
arithmetic to "avoid memory" made things *slower*. Measure before believing
any cost intuition, including these.

## Validity notes

- The probe brackets the synthesis call only; UI/ADC overhead is why the red
  line sits at 90%, not 100%.
- On a build that overruns, the tone *frequency* channel reads high; the
  duration-ratio channel and the LEDs stay honest.
- Every probe build reports a boot-time checksum of its own flash image
  (beacons 14–16), so a bad flash can't masquerade as a performance mystery.
