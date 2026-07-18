# Plaits Lab project checkpoint

This document is the durable handoff for the Plaits alternate-firmware,
custom-firmware editor, and hosted build service. It describes the production
checkpoint landed on July 17, 2026.

## Repositories

- Firmware: this repository, branch `codex/plaits-experimental-engines`
- Production website/editor and contributor center: sibling repository
  `../rubato-audio`, branch `main`
- Live unlisted editor: <https://rubato.audio/plaits-lab>
- Unlisted contributor center: <https://rubato.audio/plaits-lab/contribute>
- Public build API: <https://plaits-api.rubato.audio>
- Legacy editor prototype: its disconnected history is preserved by the
  immutable `archive/plaits-editor` tag in the `rubato-audio` upstream. It is not a
  source of truth and must not receive its own upstream or deployment.
- Reference build environment: sibling checkout `../mutable-devcontainer`

The unrelated `../crowmancy-firmware` repository is not part of this project.

## Firmware state

The default build is the experimental layout. Its amber bank contains:

1. VA + VCF
2. Phase Distortion
3. Glisson
4. GENDY
5. Scanned
6. Pulsar
7. String Machine
8. Chiptune

`PLAITS_STOCK_ENGINE_LAYOUT` restores the stock amber bank, including all three
factory DX patch banks and Wave Terrain. Both layouts retain the existing
alternate-firmware options.

Implemented in this checkpoint:

- Glisson, GENDY, Scanned, and Pulsar experimental engines
- Eleven rounds 1 and 2 audition engines: Loopback, Lockstep, Tapfield, Phase
  Weave, Sideband Bank, Attractor, Undertow, Reed Pipe, Phase Flock, Rulefield,
  and Spectral Spiral
- A fourth macro for every stock synthesis implementation
- A locked-frequency menu option that routes the frequency knob to that macro
- Stock and experimental compile-time layouts
- Built-in factory DX data for stock-layout builds
- Host tests for finite output, control extremes, neutral stock midpoints,
  audible macro response, and distinct/responding DX banks
- An AMD64 Docker/devcontainer toolchain that works on Apple Silicon
- Flash-size recovery by size-optimizing non-audio UI/settings objects
- A Plaits Lab SDK v0 with blank/fork scaffolding for all 39 catalog models,
  sanitizer/audio validation, hot-reloading browser previews, deterministic
  submissions, and unreviewed local ARM builds
- One authoritative content-addressed package catalog generating the editor
  library and both firmware build allowlists
- Recipe-specific PDF field-guide generation with a 24-position bank map and
  detailed control, output, and trigger references for all selected models

Detailed engine controls are in `alt_firmwares/README.md`.

## Hardware-test status

- The audio firmware update path and containerized build system are proven on a
  physical Plaits.
- Glisson, GENDY, and Scanned were iterated on hardware and accepted as a good
  checkpoint.
- The fourth dimensions for all stock algorithms were iterated on hardware and
  accepted.
- Pulsar passes host and ARM builds but has not received the same focused
  hardware listening pass. Treat that as the first outstanding sound-design QA.
- The eleven rounds 1 and 2 engines pass combined host tests and a generated
  24-slot ARM build. They remain prototypes until the hardware audition in
  `alt_firmwares/PLAITS_LAB_AUDITION.md` is complete.
- The latest retained local listening artifacts are the ignored `v9` WAV files
  under `build/plaits/`. They are not source artifacts and can be regenerated.

## Canonical bank ordering

All user-facing products, manifests, and documentation use:

1. Green — tonal voices
2. Red — noise, physical modelling, and drums
3. Amber — digital and alternate models

The original firmware registry stores engines in amber, green, red order. That
is an implementation detail. The build service must explicitly map the web
manifest's green/red/amber slots to the registry order rather than passing its
array through unchanged.

## Build and validation

Initialize submodules after a fresh clone:

```sh
git submodule update --init --recursive
```

Build the container image:

```sh
docker build --platform linux/amd64 \
  -t mutable-eurorack-dev:local \
  -f .devcontainer/Dockerfile .
```

Run the complete host synthesis test:

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  bash -lc 'make -f plaits/test/makefile -j2 && ./plaits_test'
```

Build the default experimental audio updater:

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  bash -lc 'make -f plaits/makefile BUILD_ROOT=build/experimental/ -j2 wav'
```

Build the stock-layout audio updater:

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  bash -lc 'make -f plaits/makefile BUILD_ROOT=build/stock/ \
    PROJECT_CONFIGURATION=-DPLAITS_STOCK_ENGINE_LAYOUT -j2 wav'
```

The WAV files are written to `build/experimental/plaits/plaits.wav` and
`build/stock/plaits/plaits.wav` respectively.

At this checkpoint, clean ARM builds report:

- Experimental layout: 202,208 bytes text, 48 bytes data, 24,032 bytes BSS
- Stock layout: 226,464 bytes text, 48 bytes data, 27,744 bytes BSS
- Rounds 1 and 2 audition layout: 148,000 bytes text, 48 bytes data, 22,400
  bytes BSS

## Web editor state

The production Rubato editor provides a complete engine catalog, three
draggable banks, search/filtering, device-local autosave, recipe import/export,
stock and experimental presets, Mutable Instruments artwork for stock models,
and a versioned manifest. It also exposes a fixed model-navigation preference
and seven apply-once starting options. Its firmware build and direct WAV
download are enabled without an account, login, email address, cookie, or
customer identity.

Manifest schema version 5 stores immutable package ID/version/digest references
in green/red/amber order plus firmware preferences, starting options, and one
to six portable chord-table documents. Published tables are immutable catalog
snapshots; a user can fork one into a bounded device-local draft, edit its four
cent offsets per position, and order loaded tables into the module's solid and
blinking green/red/yellow selector states. The editor migrates versions 1
through 4 when they are opened.

The builder validates table and chord counts, metadata length, integer pitch
bounds, arpeggio length, published catalog identity, and local-draft provenance.
It emits only numeric cent arrays. `ChordBank` now shares those flash-resident
arrays, converts only the selected chord to ratios, and no longer allocates a
complete ratio copy for every chord-aware engine instance.

The chord-table checkpoint passed the editor build/lint/render/API tests, seven
builder contract tests, eleven generator tests, the host synthesis suite, a
generated-config host compile, and a full pinned ARM link. The default
three-table recipe linked at 202,208 bytes text, 48 bytes data, and 24,032 bytes
BSS.

Rubato's unlisted `/plaits-lab/contribute` route exposes the complete generated
catalog, fork commands, the local SDK audition bridge (controls, MIDI,
scope/spectrum, and A/B), private bundle upload, and
draft/review/hardware-beta/publication state. Pages Functions live beside the
website; D1 holds submission state and private R2 buckets hold source bundles.
Anonymous draft ownership uses a random device-local bearer token and stores
only its SHA-256 hash—no account or email is required. Source intake is closed
by default until server-side source revalidation, reviewer audit history,
moderation, and signing are implemented.

The implemented backend accepts only approved engine IDs and bounded chord
tables, translates bank ordering, compiles in an isolated container, rejects
over-budget builds, caches successful WAVs, and returns audio update files.
The authoritative implementation and deployment notes are in
`alt_firmwares/plaits_lab_builder/README.md`; the legacy editor's
`docs/build-service.md` is historical context only.

## Production build-service checkpoint

Production was deployed to Cloudflare on July 17, 2026:

- Worker/custom domain: `plaits-lab-build-service` at
  `https://plaits-api.rubato.audio`
- Compiler image:
  `plaits-lab-build-service-firmwarebuilder:schema5-20260717`
- Queue: `plaits-lab-builds`; dead-letter queue:
  `plaits-lab-builds-dead-letter`
- R2 bucket: `plaits-lab-firmwares`
- Durable Objects: `FirmwareBuilder` and content-addressed `BuildJob`
- Admission policy: five new cache-miss compilations per source IP per minute;
  cache hits, status polling, and downloads bypass the limiter
- CORS origin: `https://rubato.audio`

That deployment still identifies source revision
`a7f437964326+55b8da14febf` and image tag `schema5-20260717`. Publishing this
Git branch does not rebuild or redeploy either artifact, and the deployed image
must not be described as byte-identical to the landed checkpoint. A new source
identity, immutable image, rollout, and local/remote hash comparison remain
required.

The former email/HMAC customer identity, internal bearer token, account route,
per-customer quotas, and `CustomerGate` Durable Object were removed. Build IDs
are the deterministic artifact keys, so identical normalized recipes share one
immutable WAV.

The live smoke test compiled schema 5 build
`76e8c1c9dde6b238be377994dc27d62116acaa67f585547d6823afa1b40447cb`,
then returned the repeat request as an R2 cache hit. The artifact was 15,703,532
bytes with 202,208 bytes text, 48 bytes data, 24,032 bytes BSS, binary SHA-256
`d9dd225d27925feb417454aced5106bc946ab934b0046aac2b1fbea6c84bb062`,
and WAV SHA-256
`cf654a346d052e77c5195d1b19ff1694c637dfaade63c2469b36d47076ea4c16`.

## Loose ends and next milestones

1. Audition the eleven rounds 1 and 2 prototypes on hardware, rank the keepers,
   and tune or discard them; include Pulsar in the same focused pass.
2. Add a production CPU-budget test plus ARM allocator/stack reports. The
   current boundary enforces recipe shape, catalog identity, flash, RAM, output
   size, and checksums, but it does not yet measure worst-case DSP CPU.
3. Add operational alerts, queue/dead-letter monitoring, container cost
   dashboards, and a documented incident/rollback procedure.
4. Treat the IP limiter only as a lightweight abuse guard. Cloudflare's
   per-location rate-limit binding is intentionally permissive, and NAT users
   share an IP. Do not use it for billing, strict quotas, or identity.
5. Automate catalog/schema synchronization from this repository into
   `rubato-audio`. The production page currently contains a reviewed generated
   snapshot copied from the legacy editor rather than a CI-enforced generation
   step.
6. Keep contributor source intake closed while adding queued server-side source
   revalidation, reviewer audit history, moderation controls, catalog signing,
   and an explicit reviewer secret. Then run a small hardware-beta cohort
   before publishing the first community package.
7. Add the personalized field guide to the artifact API/R2 path and expose its
   download in the production editor if that feature is still desired.
8. Decide whether preview deployments should call production builds. CORS and
   the website CSP currently allow only `https://rubato.audio`, deliberately
   excluding Pages preview origins and localhost.
9. Replace the date-based compiler image tag with an immutable commit/digest
   convention on the next firmware rollout, bump `PLAITS_SOURCE_REVISION`, and
   verify the remote artifact against a local build before deployment.
10. Treat `rubato-audio/main` as the only website/contributor source of truth.
    The `archive/plaits-editor` tag is history only; do not revive its Sites
    shell, HMAC/account proxy, or framework wrapper.
11. Reconcile the upstream CS70-style octave/fifth switch and internal octave
    slew with Plaits Lab's apply-once option profiles before porting that
    feature. Both currently claim bytes in the fixed 16-byte persisted `State`
    layout, and locked-frequency option 3 has different meanings. The July 17
    merge keeps Plaits Lab's fourth-macro/profile behavior rather than silently
    changing saved-state semantics.
12. Flash and audition a four-to-six-table build on physical hardware. Verify
    all six solid/blinking selector states, persisted table selection, knob
    traversal, and arpeggiator behavior; current proof is host plus ARM builds.
13. Expand chord-table authoring beyond the first local-fork slice. The editor
    edits four cent offsets and arpeggio length, but cannot rename tables or
    positions, add/remove/reorder positions, or import/export one table alone.
14. Design the chord-table contribution path. The three published tables are a
    curated static catalog; discovery, submission, license review, immutable
    version publication, moderation, and catalog signing are not implemented.
15. Add website CI coverage for manifest migration, chord-table validation, and
    published digest parity with the live builder. Firmware and builder tests
    cover the server/compiler boundary, but the Astro frontend does not yet
    exercise these contracts in its test gate.

## Historical build-service checkpoint

`alt_firmwares/plaits_lab_builder` contains the generated-registry contract,
isolated compiler HTTP server, Cloudflare Queue/Container/R2/Durable Object
Worker, fixtures, and contract tests. The old editor Worker still contains a
closed-by-default same-origin proxy at `/api/firmware/*`; it is superseded by
the direct public API used from `rubato.audio`.

The mixed fixture proves the build path can combine all three factory DX banks
with Glisson, GENDY, Scanned, and Pulsar in one custom firmware. Its verified
ARM size is 199,952 bytes text, 48 bytes data, and 27,392 bytes BSS.

## Personalized manual prototype

The authoritative package catalog now includes manual prose for all 39 models.
It receives a documentation digest independent of the executable package
digest, so editing user-facing text does not invalidate saved firmware recipes.

`alt_firmwares/plaits_lab_builder/render_manual.py` deterministically renders a
Letter-sized PDF in public green/red/amber order. Its first page is a complete
bank map; subsequent pages deduplicate repeated engines and document the four
controls, MAIN/AUX behavior, trigger behavior, and every occupied position.
Layout IDs and per-model attributions are deliberately omitted because they do
not help firmware users. The audition fixture produces a visually verified
nine-page sample at `output/pdf/plaits-lab-audition-field-guide.pdf`; generated
PDFs remain ignored and should be reproduced from their recipe rather than
committed.

The renderer and compiler image are ready. The production integration still
needs to:

1. Render the PDF in the Queue/Container path and store it in R2 alongside the
   WAV, using a documentation-specific key/digest so prose-only edits do not
   invalidate firmware artifacts.
2. Add a `GET /v1/builds/:buildKey/manual` route and report manual readiness and
   download URLs in job status without changing the existing firmware route.
3. Handle existing cached firmware whose manual is absent by generating only
   the missing documentation artifact rather than recompiling the firmware.
4. Add production `rubato.audio` controls for downloading the manual and define
   whether firmware plus guide should also be offered as a ZIP.
5. Give all 39 model descriptions a final editorial/listening review and
   re-check the rendered Letter pages after any catalog wording change.
