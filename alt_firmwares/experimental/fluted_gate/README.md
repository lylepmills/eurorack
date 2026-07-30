# `fluted` gate measurement

This directory contains the measurement behind the decision to **drop the
`fluted` engine** from the Braids port.

Nothing here is engine code. `fluted_probe.cc` is a measurement instrument: it
renders either the proposed 48 kHz float port or Braids' own fixed-point FLUT,
and writes raw float32 for the Python side to analyse.

## Build

```sh
REPO=<repo root>
g++ -O2 -w -I$REPO -o fluted_probe fluted_probe.cc \
  $REPO/braids/{macro_oscillator,digital_oscillator,analog_oscillator,resources}.cc \
  $REPO/stmlib/utils/random.cc $REPO/stmlib/dsp/units.cc
```

A worktree does not inherit submodules — `git submodule update --init
--recursive` first or `stmlib` is missing.

## Run

`compare.py`'s numpy caveat applies: the system python3 has none. Use
`~/Desktop/claude/rubato-audio/plugins/just_play/.venv-bundler/bin/python3`.

**Run `validate.py` first.** Every other number here is worthless if the probe
does not reproduce Braids itself, so that check is the entry point, not an
afterthought.

| script | what it answers |
|---|---|
| `validate.py` | **Run first.** Does the probe reproduce Braids' own FLUT, and is the port a faithful transcription of it? Also derives what Braids' fixed body filter works out to in harmonics of the note. |
| `period.py` | Is the period genuinely wrong, or is f0 present with a loud upper partial? Prints per-harmonic levels. |
| `sweep.py` | 2,000 renders over note × MORPH × HARMONICS × MACRO × in-loop DC blocker. The headline gate result. |
| `jet.py` | Does the jet fraction (HARMONICS) set the pitch? Fine HARMONICS sweep. |
| `window.py` | Is the in-tune HARMONICS window stable across notes and across breath-noise seeds? |
| `rescue.py` | Best case available: DC blocker fixed beyond the spec AND HARMONICS pre-restricted. Does MORPH work then? |

`fluted_probe` takes four more knobs than the scripts above exercise, for
isolating one variable at a time: `--loop-dc` / `--out-dc` (the two DC-blocker
poles, in the loop and on the output), `--pitch-term` (a fixed group-delay
compensation in samples, overriding the computed ReedPipe form — pass `1.0` to
get Braids' own constant), and `--drive-mode` (`0` scales the jet table's
input, `1` its output). `--seed` varies the breath noise, which is how
`window.py` tests whether a mode choice is bistable.

## Two traps this hit, worth knowing before reusing it

**`DigitalOscillatorShape` is not the `fn_table_` index.** `fn_table_` is
indexed by `MacroOscillatorShape - MACRO_OSC_SHAPE_TRIPLE_RING_MOD`, and the
`DigitalOscillatorShape` enum in `digital_oscillator.h` is in a *different*
order. `set_shape(OSC_SHAPE_FLUTED)` silently renders **Snare** — a loud first
block, then a decay to DC, which reads convincingly as "this model does not
sustain". Always drive `MacroOscillator` with a `MACRO_OSC_SHAPE_*`.

**Measure the pitch ratio in normalised frequency, not Hz.** `NoteToFrequency`
is anchored to `kCorrectedSampleRate` (47872.34), so a take played back at
48000 sits 4.6 cents high. `note_to_hz()` folds that in, which keeps the port's
known uniform offset out of a mode-tracking result.
