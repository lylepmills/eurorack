# Plaits Lab Test Bench (VCV Rack 2)

Builds one Plaits Lab package into a VCV Rack 2 module with Plaits' controls
and jacks, so you can patch it without flashing hardware: sequence it, modulate
it, and hear it with TRIG patched. The browser audition (`dev`) is quicker for
turning knobs; the test bench is for patches.

It is a test bench, not a Palette. Each build holds one package, with no banks,
no model switching, and no Palette configurations.

## Prerequisites

- VCV Rack 2, and the [Rack SDK](https://vcvrack.com/manual/Building#Building-Rack-plugins)
  for your platform.
- Python 3, `make`, `jq`, and a C++11 compiler:
  - **macOS:** Xcode command-line tools, then `brew install jq`.
  - **Linux:** your distribution's build tools, plus `jq`.
  - **Windows:** the MSYS2 MinGW 64-bit shell, set up as in VCV's plugin
    guide, then `pacman -S jq`.

## Build, test, install

From this directory:

```sh
make RACK_DIR=<Rack-SDK> PACKAGE=../packages/community/<name> test   # Rack-free checks
make RACK_DIR=<Rack-SDK> PACKAGE=../packages/community/<name> dist   # the plugin
make RACK_DIR=<Rack-SDK> PACKAGE=chords dist                          # a built-in, by catalog ID
make RACK_DIR=<Rack-SDK> clean                                        # remove build output
```

`PACKAGE` is a package directory or a built-in catalog ID. `sources.py` asks
the SDK for its sources, engine class and `postProcessing`, so any package that
passes `check` builds.

`dist` produces `dist/PlaitsLabTestBench-<version>-<platform>.vcvplugin`. Put
it in the `plugins-<platform>` folder inside Rack's user folder (**Help → Open
user folder**) and restart Rack. The module is **Plaits Lab Engine**, under the
brand **Plaits Lab**; the panel shows your package's name.

The plugin is unsigned. If macOS refuses to load it, run
`xattr -dr com.apple.quarantine` on the extracted `PlaitsLabTestBench` folder
and restart Rack.

## What it models

`src/lab_voice.cc` mirrors the stock control path of `plaits/dsp/voice.cc`,
for one engine:

- FREQ is ±4 octaves around MIDI note 60; V/OCT adds 1 V per octave.
- The FM, TIMBRE and MORPH attenuverters scale their jacks. When TRIG is patched
  and a jack isn't, they apply the internal decay envelope instead.
- TRIG has a 1 ms delay, and reports unpatched, low, rising edge and high to
  the engine exactly as hardware does. Unpatched, the engine is always on.
- LEVEL sets accent, and drives the low-pass gate for engines that don't
  envelope themselves.
- The int16 output stage matches `ChannelPostProcessor`. OUT and AUX are ±5 V.
- Jack scaling follows VCV's own Plaits port (Audible Instruments). The engine
  renders 12-sample blocks at 48 kHz, resampled to Rack's rate.

The low-pass gate's response and decay are hidden settings, as on the
hardware. Right-click **Edit LPG response/decay**, and TIMBRE and MORPH become
the gate's knobs until you untick it.

For a package that declares `alreadyEnveloped`, right-click **With TRIG patched**
chooses **Engine's own envelope** (the default, as the hardware does it) or
**Plaits LPG**. In **Plaits LPG** mode the engine runs untriggered and the gate
strikes it. The setting is saved with the patch.

The panel and tooltips use the package's own names and descriptions for
HARMONICS, TIMBRE, MORPH and MACRO, from its `plaits-engine.json`.

## What it does not model

- Rubato's firmware options: gate and velocity TRIG responses, LEVEL to decay,
  hold-on-trigger, MODEL CV, linear TZFM, fast FM, stereo, Drift.
- Polyphony.
- Banks, model switching and Palette configurations.
- Hardware CPU and flash budgets. Use `check --full`, `qemu/estimate.py` and a
  `--cpu-probe` build for those.

## Tests

- `make test` runs the voice through `tests/test_lab_voice.cc`, without Rack.
  It checks output range, pitch tracking, patched TRIG and the gate's decay,
  and holds for any package.
- From the SDK directory, `python3 -m unittest tests.test_vcv_sources` checks
  `sources.py`.

## Licensing

The source here is MIT-licensed, like the SDK. A built plugin also links the
Rack SDK (GPL-3.0-or-later) and contains your package's engine under its own
license. Keep builds for local use; this is not a VCV Library plugin.
