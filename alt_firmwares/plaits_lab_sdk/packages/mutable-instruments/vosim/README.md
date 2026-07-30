# VOSIM

A port of Braids' VOSM — two sine formants summed onto a pedestal and
multiplied by a bell window that the played note restarts, with both formant
phases hard-reset at every restart. Classic VOSIM.

The DSP is Emilie Gillet's `DigitalOscillator::RenderVosim`
(`braids/digital_oscillator.cc:410-438`).

## The controls

Both of Braids' knobs are formant **pitches**, and both are **absolute** —
neither tracks the played note. `digital_oscillator.cc:415` sets
`formant_increment[i] = ComputePhaseIncrement(parameter_[i] >> 1)` for `i` in
0, 1, and `braids.cc:217-226` fills `parameter_[0]` from TIMBRE and
`parameter_[1]` from COLOR. So TIMBRE is formant 1's pitch and COLOR is formant
2's, each sweeping MIDI 0 to 127.9922 in 1/128-semitone steps — `>> 1` of a
15-bit knob tops out at 16383, one count under `ComputePhaseIncrement`'s own
`kPitchTableStart` clamp.

Nothing else in the model is a knob. The amplitudes are fixed at 2:1
(`>> 1` and `>> 2` at `:424` and `:427`), the pedestal is fixed at 0.75 of full
scale (`:422`), and the window is a fixed table read (`:429`). Two of those are
what the port's spare macros are spent on:

| Macro | Label | What it does |
|---|---|---|
| TIMBRE | Formant 1 | Braids' TIMBRE. Absolute pitch, MIDI 0–127.99. |
| HARMONICS | Formant 2 | Braids' COLOR. Absolute pitch, MIDI 0–127.99, set independently of formant 1. |
| MORPH | Window | The bell's attack fraction. Noon is Braids' 1/16 exactly; the ends run to 1/256 and to a symmetric half-and-half. |
| MACRO | Balance | The formant amplitude ratio. Noon is Braids' 2:1 exactly; the ends are either formant alone. |

The pedestal is exactly the sum of the two formant amplitudes, which is what
keeps the windowed grain unipolar — it never crosses below zero, so the window
can open and close without a step. Balance therefore moves both amplitudes at
once and keeps their sum fixed; a balance control that did not would put a
discontinuity at every grain edge.

## The drop, and what the measurement says about it

The initial port evaluation dropped this model: the topology already ships as
`granular-formant`, whose two grainlets share one f0-locked window and carry one
sine formant each, so what Braids has and Granular Formant does not looked like
just (a) an absolute rather than ratio second formant and (b) the 2:1 balance.
The reinstatement condition was an A/B with TIMBRE at the same absolute formant
pitch, HARMONICS at the f2/f1 ratio, MORPH in the shape-branch-1 region near
breakpoint 0.0625, and MACRO at full carrier bleed. That comparison was run. It
does not all go one way.

Solving those four settings exactly — breakpoint 0.0625 needs
`carrier_shape` 0.500783, which at note 40 is MORPH 0.5081 — and rendering
against Braids VOSM at TIMBRE 0.60 / COLOR 0.72 (formants at 690 and 1676 Hz,
15.4 semitones apart, comfortably inside the ±24-semitone ratio), Granular
Formant lands **1.88 dB** away where this port is at **0.06 dB**. But the
prescribed point is not Granular Formant's best. A level-matched coordinate
descent over all four of its macros gets it to **0.63 dB**. Under a metric where
the port reads 0.06, that is close — so for the formant pairs Granular Formant
can set, the original "re-macro of Granular Formant" reasoning is basically
right.

Where it stops being right is the ratio limit. Granular Formant's HARMONICS
spans −24 to +24 semitones — a hard 4:1 cap on f2/f1 in either direction — and
its TIMBRE spans MIDI 24 to 108. VOSM's two knobs are independent MIDI 0–128
pitches and reach ratios up to about 1600:1. Integrating both constraints over
the knob plane, Granular Formant can set **24.6 %** of the formant pairs VOSM
can. Same best-fit search at pairs outside that window:

| Braids setting (note 40) | Δ | formants | port | Granular Formant, best fit |
|---|---:|---|---:|---:|
| TIMBRE 0.60, COLOR 0.72 | +15.4 st | 690 / 1676 Hz | 0.061 dB | 0.629 dB |
| TIMBRE 0.45, COLOR 0.85 | +51.2 st | 228 / 4381 Hz | 0.039 dB | 1.701 dB |
| TIMBRE 0.85, COLOR 0.45 | −51.2 st | 4381 / 228 Hz | 0.036 dB | 1.528 dB |
| TIMBRE 0.10, COLOR 0.30 | +25.6 st | 16.6 / 97 Hz | 0.137 dB | 0.144 dB |

So the verdict is a split one, and worth recording as such. Inside the ratio
window the engine earns its slot on the Balance knob and on being exact, not on
reach. Outside it — three quarters of the knob plane — Granular Formant cannot
get closer than 1.5–1.7 dB and no setting of its macros will, because the pair
is not addressable at all.

The last row is the counter-example worth keeping. That pair is out of range on
paper twice over (the ratio *and* TIMBRE's MIDI 24 floor), but both formants sit
so far below the played note that the grain is almost the bare window, and
Granular Formant matches it as well as this port does. Parameter-space
unreachability is not the same claim as audible unreachability, and only the
second one counts.

## Rate

`RenderVosim` does not end in `size -= 2` — the loop at `:417-437` writes one
sample per iteration — so it is a **96 kHz** algorithm and its rate constants do
not carry to 48 kHz as written. The one that matters is `:430`:
`if (phase_ < phase_increment_)` is a slice of the carrier cycle **one 96 kHz
sample wide**, and it is where both formant phases restart. Run at 48 kHz it
would be twice as long a slice, holding both formants at zero for twice as much
of every period.

So the port runs its inner loop 2x oversampled, at Braids' own 96 kHz, and
decimates through a 15-tap halfband. Every constant then transfers verbatim —
which is the point of doing the check rather than a licence to skip it.

The halfband's measured response is +0.02 dB at 12 kHz, −1.42 dB at 20 kHz,
−6.01 dB at 24 kHz, and never above −19.62 dB anywhere from 28.8 kHz to 48 kHz.
The reference renderer decimates Braids with a 127-tap sinc instead, so content
between 24 and 29 kHz is removed there and folds into the top octave here. The
A/B's three bands above 5.12 kHz run +0.33 to −2.50 dB, on at most 1.3 % of the
energy; that transition band is the expected part of it.

## Tables

`lut_bell` is embedded verbatim and reproduces at **0 LSB** across all 257
entries from its generator in `braids/resources/lookup_tables.py:239-247`, under
truncation toward zero rather than rounding — rounding leaves 124 entries a
count high. The truncated integers are what is embedded, because the truncated
integers are what Braids reads.

It is embedded rather than evaluated for a reason beyond cost. The attack is
sixteen entries out of 256, read with linear interpolation, so the window Braids
applies departs from its own Hann segment by up to 167 LSB — 0.255 % of the
window's height, at index 0.5, right at the leading edge of the grain. That
polygon is the window, not an approximation of it.

## Measured

`tests/ab.json` holds eight cases against the module — each Braids axis at both
ends, the formant pair used for the granular-formant comparison, a pair 51
semitones apart, and a high note.
Worst figures across the set: AC RMS **−0.03 dB**, spectrum **0.09 dB**, pitch
**+0.5 cents**. Every band carrying 2 % or more of the energy is within 0.20 dB
in all eight cases. What is left sits in two places, neither load-bearing: the
three bands above 5.12 kHz (the decimator, at most 1.3 % of the energy), and —
on `high-note` alone — the four bands below the played note, together 0.2 % of
the energy, where neither side has anything but its own noise floor.

The AC RMS figures are squashed: `outGain` is negative, so both renders pass
through `stmlib::Limiter` and the level comparison is not linear. The spectral
figures are the ones to read.

Two things got the port from "plausible" to that, and both were caught by
measuring rather than by reading the code:

- **`wav_sine` is not a unit sine.** Fitted over all 257 entries it is
  `127 − 32639·cos(2πi/256)` to within 1.68 LSB peak — a +127 offset and an
  amplitude 0.4 % short of full scale. Reproducing both is free (they fold into
  the pedestal and the two gains once per block) and worth about a third of the
  remaining error: across the eight cases a unit sine reads 0.06 / 0.09 / 0.12 /
  0.11 / 0.07 / 0.09 / 0.06 / 0.05 dB where the table's numbers read 0.04 / 0.06
  / 0.08 / 0.09 / 0.04 / 0.07 / 0.04 / 0.04 dB.
- **The DC blocker has to be seeded, and the seed is not a constant.** Braids
  leaves a large negative offset in and the port removes it, and a one-pole at
  0.001 takes ~100 ms to walk to it from zero. Seeding is therefore not
  optional. But the obvious seed — the pedestal times the window's mean, a flat
  −0.3750 — is *not* the model's offset: both formants restart with the carrier,
  so every grain multiplies the window by the same stretch of each sine and the
  sines do not average out. Measured at note 45 with MORPH at noon, the true
  offset is −0.3542 at TIMBRE 0.5 / COLOR 0.5, −0.4832 at COLOR 0.0, and
  **−0.6123** at TIMBRE 0.0 — so the constant seed left the blocker 0.237 FS
  from its target and the engine opened with a 100 ms DC ramp. In the A/B that
  showed up as **+11.63 dB** in `formant1-low`'s 20–40 Hz band, which fell to
  −0.41 dB when the first 250 ms was discarded. The port now integrates one
  grain on the first block after a `Reset()` — once, before the first sample —
  which lands within 0.0015 FS of the measured offset on every case. That band
  reads **+0.17 dB**, and the engine's first 100 ms carries the same mean as its
  hundredth.

Two cases — the ones with a formant at the top of its range — declare no cents
tolerance. A 13.29 kHz formant carrying 49 % and 29 % of the total energy
respectively puts a 3.6-sample comb into the autocorrelation, and the estimator picks a
different tooth of it on the two sides, reading +43.2 and +14.5 cents where
there is no pitch difference at all. Measured directly instead, with a
fine-grained DFT peak on the fundamental over 0.5 s from the middle of each
render, both sides agree to **+0.05 cents** on both cases — the same figure as
on `stock-mid`, where the autocorrelation is reliable and reads +0.5.

Both copyright lines are carried in `LICENSE` and in each source file; the
declared deviations are listed in the header comment of
`plaits/dsp/engine2/vosim_engine.h`.
