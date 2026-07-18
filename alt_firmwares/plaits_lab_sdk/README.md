# Plaits Lab SDK v0

The SDK is a constrained C++ source contract, a dependency-free Python CLI,
and a local preview bridge for the Plaits Lab contributor center. Every stock,
Rubato, and audition model is represented in the same authoritative package
catalog; community packages use the same controls, outputs, scenarios, and
content-addressed version model.

Run commands from the eurorack repository root:

```sh
SDK="python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py"

$SDK catalog
$SDK init my-engine --author "Your name"
$SDK init pulsar-fork --from pulsar --author "Your name"
$SDK check ./my-engine --full
$SDK render ./my-engine --scenario hero --output /tmp/my-engine.wav
```

`init --from` accepts any catalog ID printed by `catalog`. A fork copies and
renames the primary implementation into a self-contained community package and
pins the source package's immutable digest as provenance.

## Browser audition

Start the editor separately, then connect the exact local C++ package:

```sh
$SDK dev ./my-engine --editor http://localhost:3000
```

The command prints a contributor-center URL. The page provides the four Plaits
controls, pitch, trigger, Web MIDI, scope/spectrum views, audio playback, and
A/B rendering against any built-in model. The local server recompiles after a
source change; source never has to be uploaded just to listen.

## Validation and submission

```sh
$SDK check ./my-engine --full
$SDK submit ./my-engine --output ./my-engine.plaits-package.zip
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
$SDK build ./my-engine --hardware --output /tmp/my-engine-firmware.wav
```

This produces an **UNREVIEWED** audio updater for hardware the contributor
controls. The CLI uses a local ARM 4.8.3 toolchain when present and otherwise
runs `plaits-lab-builder:local` through Docker. It reports linked ARM size.
Hosted firmware builds never compile draft source; they accept only published
package version/digest references from the catalog.

See `RFC.md` for the contract and trust-boundary decisions.
