# `fluted` gate measurement

The measurement `BRAIDS_PORT_SPEC.md` §3.11 demanded before any `fluted` code
was written, and the evidence behind the decision to **drop the engine**. The
verdict and the numbers are written up in `BRAIDS_PORT_PROGRESS.md` §3.16.

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

| script | what it answers |
|---|---|
| `period.py` | Is the period genuinely wrong, or is f0 present with a loud upper partial? Prints per-harmonic levels. |
| `sweep.py` | 2,000 renders over note × MORPH × HARMONICS × MACRO × in-loop DC blocker. The headline gate result. |
| `jet.py` | Does the jet fraction (HARMONICS) set the pitch? Fine HARMONICS sweep. |
| `window.py` | Is the in-tune HARMONICS window stable across notes and across breath-noise seeds? |
| `rescue.py` | Best case available: DC blocker fixed beyond the spec AND HARMONICS pre-restricted. Does MORPH work then? |

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
