# Brass lip-model research harness

The measurement rig that produced `plaits/dsp/engine2/brass_engine.{h,cc}`.
Host-only, no toolchain beyond `g++`, no dependency on the Plaits tree — it
carries its own copy of the model in `model.h` so an experiment can be changed
without touching the shipped engine.

Kept because the *engine* records the conclusions but not the method, and the
next person to tune a self-oscillating model (a lip, a reed, a bowed string,
anything with a valve and a waveguide) will want the method more.

```sh
cd alt_firmwares/research/brass_lip
g++ -O2 -w <experiment>.cc -o /tmp/x -lm && /tmp/x
```

Every experiment prints a table. None takes more than a few minutes.

| file | what it answers |
|---|---|
| `passive_loop.cc` | Does the waveguide itself ring where it should? Fixed reflection, impulse in, measure the ring. **Run this first if a model goes quiet** — it isolates the loop from the valve. It is what proved inverting rings at fs/2L, non-inverting at fs/L, and non-inverting *without* a DC blocker rings at 0 Hz. |
| `probe.cc` | Threshold mouth pressure vs lip tuning, by bisection. The physical sanity check: a real valve has an oscillation threshold. |
| `partials.cc` | Grid search over the valve constants, scored on whether lip tension actually *selects* a partial. This is what found that a hard nonlinearity pins the model to the fundamental. |
| `zones.cc` | **The important one.** Sweeps lip frequency finely and prints the Arnold-tongue structure — where each partial captures, how wide the zone is, how the pitch bends inside it, and how large the silent gaps are. Every control-mapping decision in the engine came from this table. |
| `edges.cc` | Zone edges as a multiple of the partial number, across notes and mouth pressures. Shows the zones widening with pressure, so the softest playing sets the safe placement. |
| `tradeoff.cc` | Pitch accuracy vs partial selectivity over `reflect` × `zeta` × `a0` × `k` × `zc`. |
| `signed.cc` | *Signed* cents per partial, which is what revealed the pull fits `-4.8 + 67.2/n`, plus the re-measurement after nulling it. |
| `robust.cc` | Lip placement inside the zone, scored on wrong-partial misses rather than tuning, over notes × knob × breath. |
| `final.cc` | End-to-end: the staircase, every control's level, and grid-wide tuning and peak safety. |
| `auxcheck.cc` | Output-tap comparison. **Note the DC blockers in `model.h`** — without them the taps are mostly bias and every level number is wrong, which is exactly the mistake this file exists to have caught. |

## Two things about measurement, both of which produced false conclusions first

**Autocorrelation F0 needs parabolic interpolation and a minimum lag.** Integer
lags quantise badly up high — lag 45 vs 46 is 33 cents apart at 1 kHz — which
reads as model error when it is measurement error. And without a minimum lag the
first peak can land at lag ~14, reporting a spurious 3.4 kHz. Both bit here.

**Measure the AC, not the signal.** A blown model carries a large steady flow.
An early "flat to 1.6 dB across the keyboard" result was largely measuring that
DC; with it blocked the same tap is 8 dB down at the bottom and a different tap
is the flat one.

## What the harness does not cover

CPU on hardware, flash, and anything about how it sounds. `plaits_lab.py check
--full` covers audio health per scenario; the ear covers the rest.
