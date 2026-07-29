# Struck Drum

A port of Braids' DRUM — six fixed partials struck once per trigger, two of
them ring-modulated by noise rather than played straight.

The DSP is Emilie Gillet's `DigitalOscillator::RenderStruckDrum`.

## What it has that modal-resonator does not

modal-resonator excites a bank of state-variable resonators with noise or a
click; the noise IS the excitation, filtered by the resonators themselves.
DRUM's noise never passes through anything resonant: two of its six partials
are plain sine oscillators that get MULTIPLIED by a noise signal filtered on
its own separate three-stage lowpass chain, so the noise's colour and the
partials' pitch are two independent knobs on the same product. There is no
way to route noise around a resonator bank and multiply it back in
afterward, so modal-resonator cannot make this sound this way — and
conversely DRUM's fixed six-partial table cannot reach modal-resonator's
continuous mode count, stretch or per-mode Q.

## The controls

TIMBRE sets the decay (identical in structure to struck-bell's own TIMBRE:
a quadratic blend between two fixed decay tables, drone above ~98%). COLOR
drives three things Braids couples on one knob: the noise's lowpass cutoff,
the six-partial harmonics level, and the crossfade between two differently
ring-modulated noise voices. Unlike bell, COLOR never touches pitch here —
DRUM's six partial ratios are completely fixed, which is what MORPH's Spread
spends its axis on instead: scaling all six together, unison at 0, the
module's own voicing at noon, double at 1. MACRO's Decay spread is copied
directly from struck-bell's identical control — TIMBRE can only slide every
partial's decay together, this reshapes how far they diverge from each
other, from uniform to 4x the module's own spread.

## A detail worth recording: two similar constants that are not the same

`digital_oscillator.cc:1068-1069` mixes the two noise-ring-modulated
partials using `12288 - noise_mode_gain` and `noise_mode_gain`, where
`noise_mode_gain` is itself computed at `digital_oscillator.cc:1035` as
`(parameter_[1] - 16384) * 12888 >> 14`. `12288` and `12888` are one digit
apart and are NOT a typo for each other: `noise_mode_gain` maxes out at
12887, so at the very top of COLOR's travel `12288 - noise_mode_gain` goes
slightly negative (-599). Both constants are carried exactly as written.

## A second detail worth recording: the noise filter's state is integer

Each stage of the noise lowpass is `state += (input - state) * f >> 15`
(`digital_oscillator.cc:1050-1052`). That shift floors, on a signed
zero-mean operand, so each stage settles roughly `16384 / f` raw units
*below* its input's mean rather than at it. At COLOR 0 the coefficient is
only ~45/32768, and the third stage sits at about -1095 of ±32768 full
scale — at that setting the "filtered noise" is mostly a DC term, and
ring-modulating partial 1 (which shares the fundamental) by it partly
cancels the drum's fundamental. Running the chain in continuous float loses
that: it read every A/B case hot by up to +0.86 dB, and the COLOR-0 case's
excess grew with window length (+1.61 dB over two hits, +2.32 dB over
eight) in a way that looks like RNG divergence but is entirely
deterministic. The port carries the chain in Braids' raw integer scale and
floors it, which brings the suite to +0.01…+0.04 dB.

## Rate

`RenderStruckDrum`'s sample loop ends in Braids' `size -= 2` pattern
(confirmed independently by `partial_phase_increment[i] =
ComputePhaseIncrement(partial_pitch) << 1`, the same doubling struck-bell's
port explains), so the additive recursion and the noise-lowpass chain both
run at 48 kHz — the port runs both at Plaits' native 48 kHz directly. The
per-call decay/cutoff/gain values are calibrated to Braids' fixed 250 us
block (kBlockSize=24 @ 96 kHz), which is the SAME wall-clock duration as
Plaits' own kBlockSize=12 @ 48 kHz, so no further rate conversion is needed
there either; an atypical call size generalises via
`powf(decay, size / 12.0f)`, copied from struck-bell's identical mechanism.

Both copyright lines are carried in `LICENSE` and in each source file;
declared deviations are listed in the header comment of
`plaits/dsp/engine2/struck_drum_engine.h`, and `tests/ab.json` holds the
measured comparison against the module.
