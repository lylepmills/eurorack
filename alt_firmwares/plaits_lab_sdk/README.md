# Plaits Lab SDK v0

The SDK is a constrained C++ source contract, a dependency-free Python CLI,
and a local preview bridge for the Plaits Lab contributor center. Every stock,
Rubato, and audition model is represented in the same authoritative package
catalog; community packages use the same controls, outputs, scenarios, and
content-addressed version model.

Run commands from the eurorack repository root:

```sh
SDK="python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py"
# contributor packages live here, namespaced like their id (community/<slug>) —
# next to the reference packages, out of the firmware source tree
PKG="alt_firmwares/plaits_lab_sdk/packages/community"

$SDK catalog
$SDK init $PKG/my-engine --author "Your name"
$SDK init $PKG/pulsar-fork --from pulsar --author "Your name"
$SDK check ./$PKG/my-engine --full
$SDK render ./$PKG/my-engine --scenario hero --output /tmp/my-engine.wav
```

```powershell
# Windows (PowerShell) — `python`, not `python3`: a stock python.org install
# provides `python` and `py`, while `python3.exe` is only a Microsoft Store
# alias stub that errors out. Every other argument is identical.
$SDK = "python alt_firmwares/plaits_lab_sdk/plaits_lab.py"
$PKG = "alt_firmwares/plaits_lab_sdk/packages/community"

iex "$SDK catalog"
iex "$SDK init $PKG/my-engine --author 'Your name'"
```

`init --from` accepts any catalog ID printed by `catalog`. A fork copies and
renames the primary implementation into a self-contained community package and
pins the source package's immutable digest as provenance.

## Licensing

`init` writes a complete `LICENSE` naming you, and stamps a matching
`// Copyright <year> <you>.` / `// SPDX-License-Identifier: <license>` header
into every source file it generates. You do not have to find or paste license
text — but do read what you're agreeing to, because submitting a package is how
you license it to Rubato Audio and to everyone who flashes the firmware.

A package must use **MIT** (the default), **BSD-2-Clause**, **BSD-3-Clause**, or
**ISC** — choose with `--license`:

```sh
$SDK init $PKG/my-engine --author "Your name" --license ISC
```

The allowlist exists because every engine is statically compiled into one
firmware image beside Mutable Instruments' MIT-licensed Plaits code and shipped
as a single audio-installable WAV. A package's license therefore has to be
*notice-only*: dischargeable by carrying a copyright line in the firmware's
attribution list, with no copyleft reaching the rest of the image and no
source-disclosure duty riding on the distributed binary. Those four are exactly
that set.

**Apache-2.0 is deliberately excluded** even though it is permissive: §4(d)
makes the NOTICE file travel with every derivative and the §3 patent grant
carries a termination condition — per-package obligations a flashed firmware
blob has no way to honor. GPL, LGPL, and MPL are excluded outright.

A **fork keeps its upstream license** and carries both copyright notices — the
original author's and yours — in the `LICENSE` and at the top of each vendored
source file. That's what makes it an honest derivative work rather than a
re-attribution. To pick your own license, start from `--from blank` instead.

`check` verifies all of this: that the `LICENSE` text is really the license the
manifest declares, that it names a rights holder, and that every source file's
SPDX tag agrees with the manifest.

## Browser audition

`dev` serves its own audition page — nothing else to run:

```sh
$SDK dev ./$PKG/my-engine
```

Open the `http://127.0.0.1:4179/` link it prints. Page and API are the same
origin, so there is no connecting, no CORS/CSP, and no browser local-network
permission. The local server recompiles after a source change; source never
leaves your machine.

**Live audition (recommended).** With the Emscripten toolchain (`emcc`) on your
PATH, `dev` compiles your engine to WebAssembly and the page runs it in a
browser AudioWorklet — it plays continuously and every control, pitch, envelope,
and strike change is heard **instantly**, with a live scope/spectrum. This is the
primary way to audition; no render step. Install the toolchain once:

```sh
# macOS / Linux
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh          # do this in each shell before `plaits-lab dev`
```

```powershell
# Windows (PowerShell)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk; .\emsdk install latest; .\emsdk activate latest
.\emsdk_env.ps1                # do this in each shell before `plaits-lab dev`
```

If `emcc` is not found, `dev` falls back to a render-and-listen page (Render
preview, scope/spectrum, and A/B against any built-in model) that needs only a
host C++ compiler — but live audition is the better experience, so installing
Emscripten once is worth it. The **Envelope** control switches between a
continuous drone (**Sustained**) and a struck note (**Plucked**, where **Strike**
opens a low-pass-gate decay) — the same low-pass-gate behavior Plaits applies to a
patched trigger, so sustained engines respond to Strike too.

## Validation and submission

```sh
$SDK check ./$PKG/my-engine --full
$SDK submit ./$PKG/my-engine
```

Full checks enforce the manifest, licensing (see above — LICENSE text, rights
holder, and per-file SPDX tags must all agree), source boundary and allowlist,
compile with address/undefined-behavior sanitizers, execute every declared
scenario, measure CPU cost against a stock engine (see below), and reject
invalid duration, silent output, or excessive DC. The
bundle contains the exact source, deterministic preview WAVs, content digest,
and per-scenario peak/RMS/DC/silence/realtime metrics.

`submit` runs those same checks, then shows you exactly what is about to
leave your machine — package, license, digest, bundle size, and the ownership
affirmation — and uploads only after you type `submit` to confirm. It is the
only way to submit: the contributor center follows submissions, it does not
accept them. Add `--bundle-only` to build the zip without sending it (to look
inside, or for CI), or `--yes --author "…"` to submit non-interactively. Your identity is
a token minted on first submit and kept in a per-user config file (`$SDK
whoami` shows which, `--show` prints it); paste it into the contributor center
to follow your submissions in the browser, or `$SDK login` to adopt one that
page already made.

Both of those commands need a compiler that can link the sanitizers. MinGW-w64
— the usual Windows toolchain — ships no sanitizer runtime, so there the SDK
runs these two steps inside the builder Docker image instead, and says so. That
needs Docker plus a one-time `docker build` of the image (the same one the
hardware-build step uses); everything else still runs natively. `--arm` is
carried into the container along with it, so `check --full --arm` still does
the ARM compile there. Nothing changes on macOS or Linux, where the host
compiler sanitizes directly.

Scenarios run with `ASAN_OPTIONS=detect_leaks=0` on every platform. LeakSanitizer
is part of ASan on Linux but does not exist on macOS, so leaving it on would let
the same package pass `check --full` on one host and fail on another — and it can
say nothing about an engine regardless, since the source policy rejects
`malloc`/`calloc`/`new`/`delete` before a package ever compiles. ASan and UBSan,
which are what this gate is for, stay fully on.

`check` compiles with your **host** compiler, which is more permissive than the
pinned hardware toolchain (e.g. it has `std::log2`/`std::exp2`; the ARM newlib
does not). Add `--arm` to also compile your engine against the real ARM 4.8.3
toolchain — via a local toolchain, or the builder Docker image if present — and
catch hardware-only errors before the full hardware build:

```sh
$SDK check ./$PKG/my-engine --full --arm
```

### Will it run in real time?

> The short version lives here; the full guide — meter semantics, the
> measured cost table, and the optimization playbook with real numbers — is
> [PERFORMANCE.md](PERFORMANCE.md).

The audio callback gets roughly **1500 CPU cycles per sample** (72 MHz ÷ 48 kHz),
and that covers everything — the low-pass gate, the output stage, the UI and the
ADCs, not just your engine. Overrun it and the callback cannot finish a block in
time: the output glitches and the starved UI stops refreshing the LEDs.

Three tools, in increasing order of authority.

**1. `check --full` — a smoke test, and only that.** It times your engine against
a stock one on *this machine*. A development machine's memory system and pipeline
differ in kind from a 72 MHz Cortex-M4, so this catches only pathologically
expensive engines. It once reported "0.6× a stock engine" for an engine that ran
at **281%** of the hardware budget. Treat a clean result here as meaning nothing
much.

**2. `qemu/estimate.py <package> --sweep` — a calibrated estimate.** Runs your
engine on an emulated Cortex-M4, counts what it actually executes, and converts
that to cycles with a model fitted to sixteen real shipping engines measured on
real hardware:

```
worst case: harm-high  (312.4 instructions/sample)
  cost varies 1.42x across the parameter space

OK: approximately 61% of the CPU budget (likely 52-77%) -- expected to fit
```

It reports a **band**, not a number, and it sweeps parameter positions and
reports the **worst** — because an engine can pass at one knob position and
glitch at another.

Accuracy, measured by predicting each calibration engine from a fit that excludes
it (`python3 qemu/cost_model.py`): **mean error 14%, 14 of 15 within 30%.**

And the part worth reading twice: for a week, one engine appeared to cost
**4× its estimate** — until the apparent outlier turned out to be the
*measurement channel* breaking on builds that overrun the audio deadline. The
LED-meter re-measurement agreed with the model to within a few percent, and all
16 calibration engines now validate leave-one-out. The durable lesson: near
100% of budget is exactly where measurement gets hard, so a clean estimate is
still not a substitute for the hardware probe — and the probe's LED meter is
the readout to trust when a build overruns.

**3. `build --hardware --cpu-probe` — the measurement.** The Cortex-M4's own
cycle counter wrapped around the real render call, in the real audio interrupt.
The eight LEDs become a meter (one per eighth of budget, amber near the limit,
blinking red once over), and AUX carries a square wave whose *frequency* is the
answer — 1000 Hz means the whole budget, 600 Hz means 60%. Frequency rather than
a voltage so the reading survives any gain or coupling between the module and
whatever measures it. MAIN keeps your audio, so you can listen while measuring.

**Hardware is the authority.** Publication requires a probe measurement; the
estimate is a pre-flight check, not a substitute.

### Making an engine cheaper

Measured on this core, the things that cost far more than they look:

- **Float comparisons** — `VCMP` plus `VMRS` to move flags into the core stalls
  the pipeline. A `while (x > 0.001f)` loop is dearer than it looks.
- **Dependent arithmetic chains** — a serial `sum += ...` across many voices
  makes every add wait on the previous one. Split it into several partial
  accumulators and the scheduler can interleave them.
- **Per-sample work that could be per-block** — envelopes, filter coefficients,
  per-voice gains and frequencies, and every `exp`/`log`/`pow`.

And one that does *not*: **table lookups are cheap here** — the cheapest thing
measured, around 1 cycle per instruction. Replacing a LUT with polynomial
arithmetic to "avoid memory" made an engine slower, not faster.

The contributor center uploads that bundle as a private draft. Publication is
an explicit sequence: `draft → in-review → checks-passed → hardware-beta →
published`. Maintainers can reject at any review gate. Package IDs and semantic
versions are unique, and published versions are immutable.

## Local hardware beta

```sh
$SDK build ./$PKG/my-engine --hardware --output /tmp/my-engine-firmware.wav
```

This produces an **UNREVIEWED** audio updater for hardware the contributor
controls. The CLI uses a local ARM 4.8.3 toolchain when present and otherwise
runs `plaits-lab-builder:local` through Docker. Hosted firmware builds never
compile draft source; they accept only published package version/digest
references from the catalog.

The firmware this produces is a **one-model Plaits — just yours.** The module
boots into your single model (the first slot, which the LEDs show amber/yellow —
bank 0's colour; the green and red banks are empty),
so there's nothing else to scroll past while you test it. This isn't only for
tidiness: the full 24-model palette already fills the 224 KB flash, which would
leave a heavy engine no room, so registering your engine alone lets the linker
drop every stock engine and hand you nearly all of flash — a big model (Speech is
~23 KB) fits fine. The build prints how much flash the firmware uses and how much
is free; `check --arm` prints your engine's own size, so you always know how heavy
it is. (Arranging a full multi-model palette is the hosted builder's job.)

Unlike audition and `check` (which need only `stmlib`), the ARM firmware build
also needs the `stm_audio_bootloader` submodule, and — without a local ARM 4.8.3
toolchain — **Docker** plus a one-time builder image. Install
[Docker](https://docs.docker.com/get-docker/) if you don't have it, then set both
up once:

```sh
git submodule update --init stmlib stm_audio_bootloader
docker build --platform linux/amd64 -t plaits-lab-builder:local -f Dockerfile.plaits-builder .
```

(The image build downloads the pinned ARM 4.8.3 toolchain and warms a compile
cache, so it takes a while — but only the first time.)

See `RFC.md` for the contract and trust-boundary decisions.
