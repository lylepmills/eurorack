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
permission: four Plaits controls, pitch, trigger, scope/spectrum, audio
playback, and A/B rendering against any built-in model. The local server
recompiles after a source change; source never leaves your machine. Pass
`--editor <page-url>` to drive the full hosted contributor site instead.

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

See `RFC.md` for the contract and trust-boundary decisions.
