# Palette Lab Test Bench (VCV Rack 2)

Builds one Plaits Lab package into a VCV Rack 2 module with Plaits' controls
and jacks, so you can patch it without flashing hardware: sequence it, modulate
it, and hear it with TRIG patched. The browser audition (`dev`) is quicker for
turning knobs; the test bench is for patches.

Each build holds one engine package, with no banks, model switching or Palette
configurations.

## Prerequisites

- VCV Rack 2, and the [Rack SDK](https://vcvrack.com/manual/Building#Building-Rack-plugins)
  for your OS and CPU architecture. Rack Free is sufficient.
- Python 3, `make`, `jq`, `zstd`, and a C++11 compiler:
  - **macOS:** Xcode command-line tools, then `brew install jq zstd`.
  - **Linux:** your distribution's build tools, plus `jq` and `zstd`.
  - **Windows:** the MSYS2 MinGW 64-bit shell, set up as in VCV's plugin
    guide (including `make`, Python, `jq` and `zstd`). Use this shell for the
    commands below, and add `PYTHON=python` if Python is named `python` there.

## Build, test, install

From this directory:

```sh
make PACKAGE=../packages/community/<name> test                     # no Rack SDK needed
make RACK_DIR=<Rack-SDK> PACKAGE=../packages/community/<name> dist   # the plugin
make RACK_DIR=<Rack-SDK> PACKAGE=chords dist                          # a built-in, by catalog ID
make RACK_DIR=<Rack-SDK> clean                                        # remove build output
```

`PACKAGE` is a package directory or a built-in catalog ID. Quote a path that
contains spaces: `make PACKAGE="/path/to/my engines/example" test`.
`sources.py` asks the SDK for its sources, engine class and `postProcessing`.
The build tracks included headers and keeps compiled objects separate for each
package. Switching packages does not need `clean`. Keep the Rack SDK path free
of spaces because Rack's own makefiles require that.

`dist` produces `dist/PlaitsLabTestBench-<version>-<platform>.vcvplugin`. Put
it in the `plugins-<platform>` folder inside Rack's user folder (**Help → Open
user folder**) and restart Rack. The module is **Palette Lab Engine**, under the
brand **Palette Lab**; the panel shows your package's name. The original plugin
and module slugs (`PlaitsLabTestBench` / `PlaitsLabEngine`) are retained so
existing Rack patches still load.

The plugin is unsigned. If macOS refuses to load it, run
`xattr -dr com.apple.quarantine` on the extracted `PlaitsLabTestBench` folder
and restart Rack.

## First patch

1. Add **Palette Lab Engine** from the **Palette Lab** brand in Rack's module
   browser. Check the engine name at the top of the panel against the package
   you built.
2. Add Rack's **Audio** module and select your audio device. Connect OUT to an
   audio input, then listen to AUX separately too: the two outputs can have
   different roles. Begin at a low monitoring level.
3. Add **MIDI-CV**, select a MIDI source, and connect its V/OCT output to
   Palette Lab's V/OCT input. For triggered playing, connect its GATE output
   to TRIG. Compare that with TRIG unplugged; the engine receives different
   trigger states in each case.
4. Sweep HARMONICS, TIMBRE, MORPH and TWIST. Hover for the engine-specific
   control names. Add an LFO or other CV source to TIMBRE or MORPH and try
   the corresponding attenuverter on both sides of noon.

This is a source-development tool: your engine is compiled into the plugin.
After editing it, rerun the test and dist commands with the same `PACKAGE`
and `RACK_DIR` arguments, quit Rack, replace the installed plugin package, and
reopen your saved patch. Rack
loads the new code on restart. Rebuilding alone does not update a running Rack.
Each build replaces the same module; it does not add another engine to a menu.

## Listening checklist for contributors

- **Controls and outputs:** listen to OUT and AUX throughout the intended pitch
  range. Sweep every control through its ends and midpoint, including TWIST.
  Check for unintended silence, clicks or sudden level changes, while allowing
  for sounds the engine deliberately makes.
- **Triggers and level:** compare unplugged TRIG, short pulses and held gates.
  Patch LEVEL and vary it. Check the engine's attack, tail and retrigger behavior.
- **Modulation:** patch HARMONICS, TIMBRE, MORPH and FM individually. For the
  three attenuverters, check zero, positive and negative amounts. Unplug them
  again to hear any internal-envelope modulation with TRIG patched.
- **LPG:** use **Edit LPG response/decay** to try both extremes. For an engine
  with its own envelope, compare **Engine's own envelope** and **Plaits LPG**
  when TRIG is patched.

In your package's existing `README.md`, note the OS/Rack version, engine
version, patch connections, useful settings and any limitations you found.
This gives a reviewer a listening recipe; it is not a required pass/fail form.
Add reproducible cases to `tests/scenarios.json` where supported. Continue with
the SDK's `check --full` and hardware CPU probe before publication; a good Rack
patch does not establish the hardware's timing or flash budget.

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

The 12 HP panel follows the hardware's knob and jack positions, with white
Rogan knobs, three attenuverters and two rows of jacks. The MODEL CV position
is empty because each build contains one engine. OUT and AUX share the bottom
row with TRIG, LEVEL and V/OCT. Existing patches retain their parameter and
port IDs, so their settings and cable connections survive the layout change.

TWIST is a small, dedicated centre knob for the SDK's fourth synthesis control
(`macro` in engine code);
its midpoint is neutral for stock models. Tooltips show each control's
engine-specific name and description from `plaits-engine.json`. For example,
Chords' TWIST tooltip describes voice-group balance. Long engine names are
fitted to the panel header.

Patches save the engine's package ID. Rebuilding the same module for another
engine shows **ENGINE CHANGED** on an existing patch; its controls and cables
remain in place. After checking the new sound, right-click **Use <engine> for
this patch** to acknowledge it. Saving alone keeps the warning. Older patches
without an engine ID load as before. This warns about a different engine, not
edits to the same engine while developing it.

## What it does not model

- Rubato's firmware options: gate and velocity TRIG responses, LEVEL to decay,
  hold-on-trigger, MODEL CV, linear TZFM, fast FM, hard sync, stereo, Drift.
- Polyphony.
- Banks, model switching and Palette configurations.
- Hardware CPU and flash budgets. Use `check --full`, `qemu/estimate.py` and a
  `--cpu-probe` build for those.

## Tests

- `make test` needs Python and a C++ compiler, with no Rack SDK or `jq`.
  `tests/test_lab_voice.cc` checks CV scaling, TRIG states, pitch tracking and
  LPG decay using a predictable probe oscillator. These assertions do not
  assume that every engine's output is a pitched oscillator.
- `tests/test_package.cc` separately compiles your selected package and checks
  both outputs' bounds across control sweeps, unpatched/patched TRIG, LEVEL
  and LPG modes. It reports RMS for inspection; it makes no universal
  assumptions about tuning, silence or decay. Run the SDK's `check --full`
  for the package's own scenarios and sanitizer checks.
- From the SDK directory, `python3 -m unittest tests.test_vcv_sources` checks
  the source resolver, quoted labels/names, a real build under a path containing
  spaces, header-only edits, package switching and failed package resolution.
- With the Rack SDK installed, `make RACK_DIR=<Rack-SDK> PACKAGE=chords test-rack`
  tests the actual module's saved-engine identity, legacy patches and warning
  acknowledgement without opening a Rack window.

## Licensing

The source here is MIT-licensed, like the SDK. A built plugin also links the
Rack SDK (GPL-3.0-or-later) and contains your package's engine under its own
license. Keep builds for local use; this is not a VCV Library plugin.

The panel is original artwork, also under MIT. The physical arrangement was
checked against [Audible Instruments' Macro Oscillator 2](https://github.com/VCVRack/AudibleInstruments/blob/v2/src/Plaits.cpp).
Its [panel artwork is distributed by permission](https://github.com/VCVRack/AudibleInstruments/blob/v2/LICENSE.md),
so its SVG, model icons and decorative graphics are not included here.
The knobs and jacks use Rack's built-in component library.
