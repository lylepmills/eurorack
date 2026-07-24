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

`init --from` accepts any catalog ID printed by `catalog`. A fork copies and
renames the primary implementation into a self-contained community package and
pins the source package's immutable digest as provenance.

## Browser audition

`dev` serves its own audition page — nothing else to run:

```sh
$SDK dev ./$PKG/my-engine
```

Open the `http://127.0.0.1:4179/` link it prints. Page and API are the same
origin, so there is no connecting, no CORS/CSP, and no browser local-network
permission. The local server recompiles after a source change; source never
leaves your machine. Pass `--editor <page-url>` to drive the full hosted
contributor site instead.

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
$SDK submit ./$PKG/my-engine --output ./my-engine.plaits-package.zip
```

Full checks enforce the manifest, license, source boundary and allowlist,
compile with address/undefined-behavior sanitizers, execute every declared
scenario, and reject invalid duration, silent output, or excessive DC. The
bundle contains the exact source, deterministic preview WAVs, content digest,
and per-scenario peak/RMS/DC/silence/realtime metrics.

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
runs `plaits-lab-builder:local` through Docker. It reports linked ARM size.
Hosted firmware builds never compile draft source; they accept only published
package version/digest references from the catalog.

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
