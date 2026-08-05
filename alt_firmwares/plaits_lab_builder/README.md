# Plaits Lab firmware build service

This directory contains the approved-engine backend for Plaits Lab. It accepts
legacy recipes and manifests through schema 18 containing 24 or 32 versioned
engine references, firmware preferences and starting options, and bounded
chord-table/custom-FM/scale-bank/Speech-bank resources. Schema 15 can target either Mutable
Instruments Plaits or Plum Audio Ro'Ved and adds the color-blind bank display.
Schema 16 adds an ordered bank of up to 16 editable scales shared by Diatonic
Chord and Scale Stack, plus automatic LEVEL routing, which
uses LPG decay on ordinary oscillator models and preserves LEVEL as
velocity/accent on self-enveloped models. Schema 17 adds a selectable set of
the five shipped Speech banks plus up to eight total stock/custom LPC banks;
custom banks carry bounded 14-byte decoded frames and word boundaries rather
than source recordings or synthesis-model data. Schema 18 adds the Stock,
Drift, or Step unpatched-attenuverter mode to Starting Options. The selected
mode participates in the generated options profile. A one-shot marker in the
application image is restored by every WAV or HEX flash, so first boot applies
all embedded Starting Options even when reinstalling the exact same build;
ordinary power cycles still preserve later runtime changes. It generates a compile-time
configuration, builds with the pinned Mutable
Instruments ARM toolchain, enforces the Plaits flash and RAM limits, and returns
either the default 48 kHz audio updater or, when explicitly requested, an
application-only Intel HEX file for a direct hardware programmer. The HEX starts
at the firmware's linked application address and deliberately excludes the
bootloader.

Schema-15/Ro'Ved support passed its hardware checklist on July 30, 2026.
Schema 17 is available in production with recipe-driven scale banks, automatic
LEVEL routing, selectable/custom LPC Speech banks, text and recording encoders,
and source/engine audio previews.

An August 4 hardware regression exposed two firmware defects in the custom
Speech path: inconsistent `PLAITS_HAS_CUSTOM_SPEECH_BANKS` values changed the
`Voice` class layout between compilation units, and discrete LPC playback could
read one frame past a word boundary. A patched build passed both the stock-bank-
only control and the original two-custom-bank recipe on hardware. Keep the
public custom-bank editor disabled until the patched compiler image has also
passed a build through the deployed production service.

The first production canary at `rev-4749aec727af` failed safely before
compilation: the newer Worker normalized a pre-v18 recipe with the no-op
`attenuverterMode: stock` value, while the container rejected the field below
schema 18. Production was immediately rolled back to `rev-9f8fe4de4c67` while
the private container contract was amended to accept only that legacy-equivalent
value; Drift and Step remain schema-18-only.

The corrected production canary, build
`d0e1fe7acbec6c77e26198da2b5afb71e3d83dcbf473114c1614830747a12697`,
compiled the original two custom banks at `rev-93a95c419688`. Its downloaded
WAV (`8861a201…`) matched the same recipe built directly by that immutable image
byte for byte. Keep the public editor disabled until this production-served WAV
also passes the module test.

The service is split across two isolation layers:

- A Cloudflare Worker validates and hashes recipes, stores job state in Durable
  Objects, schedules work through Queues, and caches successful WAV/HEX files in R2.
- A non-root Cloudflare Container has no runtime internet access. It owns the
  allowlisted C++ registry generator, compiler, and pinned Kokoro/LPC encoder.
  Kokoro weights, voices, and language dictionaries are baked into the image.
  The request cannot
  provide source, paths, make targets, flags, or shell fragments.

## API

The browser-facing API is public at `https://plaits-api.rubato.audio`. It does
not require an account, email address, cookie, API token, or customer identity.
Its CORS allowlist includes both production site origins, `https://rubato.audio`
and `https://www.rubato.audio`; keep both in the Worker tests and live canary.

- `GET /v1/catalog` returns the approved engine, chord-table, and scale-bank limits.
- `POST /v1/builds` accepts a Plaits Lab manifest and returns a deterministic
  build ID with `queued`, `building`, or `succeeded` status.
- `GET /v1/builds/:buildKey` returns durable job state.
- `GET /v1/builds/:buildKey/firmware` streams the recipe's cached WAV or HEX
  artifact from R2 with the matching content type and filename.
- `POST /v1/speech/encode` synthesizes a bounded word list and returns listening
  previews plus the compact LPC resource stored in a schema-17 recipe.
- `POST /v1/speech/encode-recording` splits a paused microphone take and
  converts it without retaining the uploaded recording. Generated previews and
  frame data return in the response; unlike deterministic text requests, the
  recording request is not cached in R2.
- `POST /v1/speech/segment`, `GET /v1/speech/voice-preview/…`, and
  `GET /v1/speech/stock/…` support language-aware splitting and listening.
  Deterministic text, voice, and stock results are content-addressed in R2.

Engine references are resolved to the current approved catalog at request
normalization. An older digest or compatible semantic version for the same
engine and package is upgraded to the source in the current compiler image;
unknown/renamed packages, breaking versions, malformed digests, and references
from a newer catalog still fail closed. Recipes therefore preserve the chosen
model across compatible implementation updates without allowing callers to
provide source. The current source revision remains part of the build key.

## Personalized manual prototype

The authoritative catalog now includes complete control and trigger prose with
a documentation digest that is independent from firmware package identity.
Generate the current audition-layout field guide with:

```sh
python3 render_manual.py audition_recipe.json \
  ../../output/pdf/plaits-palette-audition-field-guide.pdf
```

The PDF uses the public green/red/amber order, shows all 24 positions, and then
deduplicates repeated engines in the detailed model reference. The renderer is
deterministic and runs with ReportLab inside the compiler image. It deliberately
omits internal layout IDs and per-model attributions. Generated PDFs are ignored
build products and should be reproduced from the recipe.

A slot whose built-in six-op FM bank the recipe replaced is credited in its model
reference: the bank's name, author, origin, and description, plus the patch count
HARMONICS spans (which a short v13 bank changes). Because a v12/v13 bank belongs
to a SLOT, two placements of one FM engine carrying different banks get separate
model entries instead of deduplicating into one — a v6 bank overrides a factory
bank, so its credit lands on every slot playing that bank. The guide credits a
bank rather than listing its patches; the patch names live in the editor.

Manual generation is integrated into the service (2026-07-18): the queue
consumer renders the PDF through the container's synchronous `POST /manual`
endpoint after a successful compile (a manual failure never fails the
firmware build), stores it in R2 under `manuals/<manualKey>.pdf`, reports
`manual.status` / `manual.downloadUrl` in job status, serves
`GET /v1/builds/:buildKey/manual`, and backfills the PDF for already-cached
firmware via `manualOnly` queue messages. `computeManualKey` hashes everything
the PDF PRINTS — the slot layout, each engine's DOCUMENTATION digest, the chord
tables the options-menu page lists, the scale order when a scale-aware engine is
present, each custom FM bank's credit, and
`PLAITS_MANUAL_CONTRACT` — deliberately not the firmware source revision or
toolchain, so prose-only edits never invalidate firmware and firmware rollouts
keep reusing cached manuals. It is likewise NOT the packed patch bytes: the
guide credits a bank instead of listing its patches, so two banks with equal
credits do render the same PDF. Anything the renderer starts printing must be
added to the key. Bump `PLAITS_MANUAL_CONTRACT` when the renderer's layout
changes — or when the key's inputs change, so cached PDFs re-render (contract 5
covers the FM-bank credits, and the chord-table fold that fixed guides served
from cache with another recipe's LIGHT 1 row; contract 6 renames a customized
six-op slot "Custom 6-Op FM Bank" in the bank map and the model reference,
subtitled with the bank's own name; contract 10 adds the recipe's scale order).
Contract 13 clarifies LIGHT 8's Default/CCW/CW behavior. It shipped with the
six-voice wavetable optimization image, so cached guides receive that
prose-only correction from the matching renderer. Contract 14 standardizes the
fourth synthesis control's user-facing name as MACRO and adds the precision
fine-tuning range to the at-module reference. Its renderer and tests are landed,
but the Worker remains on contract 13 until the next builder-image rollout: that
rollout must bump `PLAITS_MANUAL_CONTRACT` to 14 together with the immutable
source revision and image tag. Never deploy the contract bump by itself, because
the current production container cannot render the new guide.

The contract is the Worker's alone, and in source (`a0c0791`) it rides in the
`POST /manual` body as `manualContract` for the container to echo on
`X-Plaits-Manual-Contract`, so that header describes the render that actually
happened; the container's `PLAITS_MANUAL_CONTRACT` env var is only the fallback
for a caller that sends none. It rode the `rev-94e84165ea2a` rollout, having
waited out one rollout cycle on purpose: the fix is in the container, and
shipping a container means a new image, a new `PLAITS_SOURCE_REVISION`, and a
re-key of every cached artifact — too much churn for a header nothing consumes
(the Worker records `env.PLAITS_MANUAL_CONTRACT` on the R2 object itself).
Before that, production reported the image default `1` while the Worker was
on 5.
That checkpoint shipped in the July 19 rollout. It needed a new container image
AND a Worker deploy, and because the firmware source had also changed since the
deployed `schema5-20260717` image (the chord-table `ChordBank` rework moved the
`chords` engine digest), it bumped `PLAITS_SOURCE_REVISION`, used a new
immutable image tag, and landed with the website catalog re-sync
(`rubato-audio/website/scripts/sync-plaits-catalog.mjs`).

The custom-FM-bank credits shipped the same way on July 27 (`rev-af5eaeb0f5b0`):
the renderer in the container image, the key change in the Worker. Both halves
are needed — deploy the Worker alone and every recipe re-keys to a manual the old
image renders without credits; ship the image alone and cached PDFs keep their
old (credit-free, and for chord tables possibly wrong) contents, since the key
never moved. The field-guide change itself touches no firmware source; the
revision moved because unrelated `plaits/` commits (the host-build runtime chord
tables, the CPU probe) had landed on master since `rev-0152d502f2f3`. The
catalog was unaffected: `catalog:check` and the website `--check` both confirmed
byte-identical snapshots at the new revision, so no engine digest moved.

The build key covers the normalized slots, preferences, starting options,
ordered chord-table, scale-bank, and Speech-bank data (without `createdAt`), requested output
format, source revision, toolchain identity, and build-contract version. Identical recipes
therefore share the same immutable artifact; WAV and HEX requests remain
separate cache entries while sharing their field-guide PDF.

## Local validation

Run the contract suites:

```sh
python3 -m unittest discover -s . -p 'test_*.py' -v
pnpm test
pnpm check
```

Build the production-shaped image from the firmware repository root. The image
copies the working tree, so the `stmlib` and `stm_audio_bootloader` submodules
must be checked out first; an uninitialized submodule copies in as an empty
directory, and without the Dockerfile's guard the image would build cleanly and
then fail every compile at request time on a missing
`stm_audio_bootloader/fsk/packet_decoder.h`:

```sh
git submodule update --init stmlib stm_audio_bootloader
docker build --platform linux/amd64 \
  -t plaits-lab-builder:local \
  -f Dockerfile.plaits-builder .
```

Start it and submit either recipe fixture to `POST /build`. The mixed fixture is
the useful proof case because it combines all three built-in DX banks with all
four Rubato engines, which the former stock/experimental compile switch could
not express.

Verified mixed-recipe output at source revision `199eeaf14147`:

- ARM text: 201,744 bytes
- ARM data: 48 bytes
- BSS: 27,840 bytes
- Binary SHA-256: `da9b46309dd179e3f41ea70df3216b88d91959e199cfe91e7bba3a640f5a71c0`
- WAV SHA-256: `fa632d5369695478cff74b37ba228d2b32fa262b40992a567bbdb5825bf7b929`

Measured by submitting `mixed_build_request.json` to the image under test. The
same request gave 201,344 / 48 / 27,824 at `720d1406e87b` (binary `695eb862…`,
WAV `ab63035b…`) and 201,296 text bytes at `rev-0e6e6f202307` — so the aux
output / suboscillator split cost 48 bytes, and the work since (variable-length
FM banks, the host-build runtime chord tables, the CPU probe) another 256, with
no BSS movement. The 223,760 figure once recorded for `rev-0e6e6f202307` does not
reproduce from this request and was measured some other way; treat these as
reproducible only via the procedure above, per the note below.

These are revision-specific. The July 17 schema-5 figures (199,952 / 48 / 27,392,
binary `564c2322…`, WAV `2e9a93cb…`) held at revision
`a7f437964326+55b8da14febf` and no longer reproduce, so re-measure against the
revision under test rather than treating either set as an expected value.

## Cloud deployment

The production Worker, queues, R2 bucket, Durable Objects, and compiler
Container are managed by `wrangler.jsonc`. Before each firmware-source rollout:

1. Compute and set a new immutable `PLAITS_SOURCE_REVISION`.
2. Build and push the matching container image tag. **Pass the revision as a
   build arg**, not just as a Worker var — the container stamps its own
   `PLAITS_SOURCE_REVISION` into every artifact's `X-Plaits-Source-Revision`,
   which the Worker prefers over its own var when recording build metadata. An
   image built without the flag reports the Dockerfile's `ARG` default —
   `development` since 2026-07-26, deliberately not a real-looking commit. It
   used to default to a fixed old commit, and `rev-0e6e6f202307` was built
   without the flag and stamped `303a9afad9f1` on everything it compiled, which
   is exactly the failure the sentinel makes visible instead:

   ```sh
   docker build --platform linux/amd64 \
     --build-arg PLAITS_SOURCE_REVISION=<revision> \
     -t registry.cloudflare.com/<account-id>/plaits-lab-build-service-firmwarebuilder:rev-<revision> \
     -f Dockerfile.plaits-builder .
   <account-wrangler> containers push \
     registry.cloudflare.com/<account-id>/plaits-lab-build-service-firmwarebuilder:rev-<revision>
   ```

   Confirm with `curl -sD - -o /dev/null localhost:<port>/build/<key> | grep
   Source-Revision` against the image before pushing; `development` there means
   the build arg was missed. (Local `plaits-lab-builder:local` images are built
   without the flag on purpose and report `development` — that is only a problem
   for an image you are about to deploy.)
3. Regenerate the engine allowlist: `pnpm run catalog:regen` (rewrites
   `../plaits_lab_catalog/public_catalog.json` from the exporter at the
   checked-out commit), then commit any change. Each engine digest hashes its
   catalog metadata *and* source bytes, so even a display-name edit invalidates
   it. Compatible stale references now resolve to the current package, but the
   catalog must still match the compiler image for new/removed packages,
   documentation identity, and trustworthy provenance. `pnpm run deploy` runs
   `catalog:check` as a hard gate and refuses to ship a stale allowlist.
4. Re-sync the website catalog to the SAME commit
   (`website/scripts/sync-plaits-catalog.mjs`) so its per-engine digests match
   the builder's. Server-side compatibility is a stale-client safety net, not a
   substitute for deploying the builder allowlist and website snapshot from one
   commit.
5. Run the contract, generator, type, and dry-run deployment checks.
6. Deploy with `pnpm run deploy` (not `pnpm deploy`, which is pnpm's built-in
   workspace-deploy command) and wait for the Container image rollout.

Cloudflare's rate-limit binding allows five new compilation requests per source
IP per minute. Cache hits and repeated polls for an already queued build bypass
that limit. This is a lightweight abuse guard rather than an account or billing
system; IP addresses are not stored in Durable Objects or attached to firmware
artifacts.

The production compiler image is
`plaits-lab-build-service-firmwarebuilder:rev-c8b7d7736d40` (immutable
commit-derived tags replaced the date-based convention; the table below is the
full history — keep this line in step with its last row). After deploying a
new image, wait for `wrangler containers list` to report `ready` before smoke
testing; requests made while the application was still `provisioning` reached
the previous live instance during the schema-5 rollout.

Schema 17, including selectable stock LPC banks, custom text/recording-derived
Speech banks, source/engine previews, and the earlier recipe-driven scale banks
and automatic LEVEL routing, is live. The generalized
schema-inheritance hardening from `5b2b077` is also live: current production
source `9f8fe4de4c67` descends from that commit, so future supported schemas
inherit older feature shapes without another version-list edit.

### Rolling back

Immutable `rev-<commit>` tags make a rollback a configuration change rather
than a rebuild: set `PLAITS_SOURCE_REVISION` and the container `image` in
`wrangler.jsonc` back to a previously deployed pair, `pnpm run deploy`, and wait
for `wrangler containers list` to report `ready` before smoke testing. Never
delete a previously deployed image from the registry — it is the rollback
target.

| Deployed | Source revision | Image tag |
| --- | --- | --- |
| July 17, 2026 (schema 5) | `a7f437964326+55b8da14febf` | `schema5-20260717` |
| July 19, 2026 (schema 6) | `303a9afad9f1` | `rev-303a9afad9f1` |
| July 21, 2026 (schema 6) | `8cf101fe28af` | `rev-8cf101fe28af` |
| July 21, 2026 (schema 7) | `7b62cbd851d4` | `rev-7b62cbd851d4` |
| July 21, 2026 (schema 7) | `dd1db33a7fa3` | `rev-dd1db33a7fa3` |
| July 22, 2026 (schema 8) | `f3474e7470b1` | `rev-f3474e7470b1` |
| July 23, 2026 (schema 9) | `c323e0d31f90` | `rev-c323e0d31f90` |
| July 23, 2026 (schema 9, stereo gated) | `b107b1c4f041` | `rev-b107b1c4f041` |
| July 23, 2026 (schema 10, per-engine stereo) | `8b3e1cb6fe3b` | `rev-8b3e1cb6fe3b` |
| July 24, 2026 (schema 10, empty-slot fix) | `effbb4573178` | `rev-effbb4573178` |
| July 24, 2026 (schema 11, sparse banks) | `c961b1d86063` | `rev-c961b1d86063` |
| July 24, 2026 (schema 12, per-slot FM banks) | `6cd7d2cf841c` | `rev-6cd7d2cf841c` |
| July 25, 2026 (options menu reorder) | `0e6e6f202307` | `rev-0e6e6f202307` |
| July 25, 2026 (aux output / subosc split) | `720d1406e87b` | `rev-720d1406e87b` |
| July 26, 2026 (factory FM bank strip) | `83a78fad3ee8` | `rev-83a78fad3ee8` |
| July 26, 2026 (schema 13, variable-length FM banks) | `0152d502f2f3` | `rev-0152d502f2f3` |
| July 27, 2026 (field-guide FM-bank credits, manual contract 5) | `af5eaeb0f5b0` | `rev-af5eaeb0f5b0` |
| July 27, 2026 (custom-bank naming in the bank map, manual contract 6) | `94e84165ea2a` | `rev-94e84165ea2a` |
| July 28, 2026 (Helix — first community engine) | `b9e1671a09f4` | `rev-b9e1671a09f4` |
| July 28, 2026 (Helix 0.2.0 — metadata + contributor colour) | `d43bb37fa88d` | `rev-d43bb37fa88d` |
| July 28, 2026 (AUX rework allowlist regen) | `2782e70be79c` | `rev-2782e70be79c` |
| July 29, 2026 (schema 14, optional calibration procedure, manual contract 7) | `9e04ca2bf0d5` | `rev-9e04ca2bf0d5` |
| July 29, 2026 (complete Braids inventory, corrected A/B and hardware validation) | `0c0c0f3ef835` | `rev-0c0c0f3ef835` |
| July 29, 2026 (Wave Paraphonic chord-table compatibility) | `199eeaf14147` | `rev-199eeaf14147` |
| July 30, 2026 (custom FM-bank request-size fix) | `28f77ed4f416` | `rev-28f77ed4f416` |
| July 30, 2026 (schema 13/14 per-engine stereo validator fix) | `c5ec5ef1d4f1` | `rev-c5ec5ef1d4f1` |
| July 30, 2026 (schema 15, Plum Audio Ro'Ved target) | `0032af8067d9` | `rev-0032af8067d9` |
| July 30, 2026 (schema 15, color-blind brightness display + Intel HEX, manual contract 9) | `1d1c2ff82b34` | `rev-1d1c2ff82b34` |
| July 30, 2026 (quarantine Undertow and Phase Flock) | `b7708ec67487` | `rev-b7708ec67487` |
| July 30, 2026 (restore optimized Undertow and Phase Flock; sync scale-bank digests) | `21eea00866b0` | `rev-21eea00866b0` |
| July 30, 2026 (schema 16 Auto LEVEL + locked-octave shortcut, manual contract 11) | `07c108dd71b7` | `rev-07c108dd71b7` |
| July 30, 2026 (repair overloaded stereo paths and DX7 Bank A audition) | `8c015354f67a` | `rev-8c015354f67a` |
| July 30, 2026 (sound-exact CPU sweep, with flash-safe Modal path) | `7db000d5bfd3` | `rev-7db000d5bfd3` |
| July 31, 2026 (Renaissance WTCH and WTx6 standalone engines) | `792f18cfbe5a` | `rev-792f18cfbe5a` |
| July 31, 2026 (unpatched attenuverter Drift + Step modes, manual contract 12) | `075543932021` | `rev-075543932021` |
| July 31, 2026 (control-path flash recovery for full stock palettes) | `e90a1c3500c4` | `rev-e90a1c3500c4` |
| August 1, 2026 (six-voice wavetable optimization, manual contract 13) | `bced0e9c156e` | `rev-bced0e9c156e` |
| August 1, 2026 (navigation, fine-tune LED, and Ro'ved LED fixes) | `220eeca73af8` | `rev-220eeca73af8` |
| August 1, 2026 (Virtual Analog Dual and Crossfade variants) | `22a9af18c30b` | `rev-22a9af18c30b` |
| August 2, 2026 (stock-model control and output metadata audit) | `407cbf6eefca` | `rev-407cbf6eefca` |
| August 4, 2026 (precision fine tuning with automatic pitch retention) | `8ab5c8c018a2` | `rev-8ab5c8c018a2` |
| August 4, 2026 (schema 17 custom Speech banks, manual contract 14) | `017f95a1f59f` | `rev-017f95a1f59f` |
| August 4, 2026 (gate custom Speech code out of legacy builds; recover default-palette flash) | `2e963f7402b9` | `rev-2e963f7402b9` |
| August 4, 2026 (bake the English speech model for network-isolated previews) | `9f8fe4de4c67` | `rev-9f8fe4de4c67` |
| August 4, 2026 (repair custom Speech class layout and LPC frame bounds) | `4749aec727af` | `rev-4749aec727af` |
| August 4, 2026 (rollback after schema-18 compatibility canary) | `9f8fe4de4c67` | `rev-9f8fe4de4c67` |
| August 4, 2026 (bridge normalized legacy recipes for the Speech recovery rollout) | `93a95c419688` | `rev-93a95c419688` |
| August 4, 2026 (restore saved custom-bank previews from their LPC frames) | `c8b7d7736d40` | `rev-c8b7d7736d40` |

Three consequences a rollback has that a forward deploy does not:

- **Queued builds fail fast, by design.** The build key hashes the source
  revision, so messages queued against the rolled-back-from revision no longer
  match and `processBuild` ends them with `stale_build`, asking the client to
  resubmit. That is correct behavior, not a second incident.
- **Cached artifacts are orphaned, not lost.** R2 objects stay under the build
  key they were compiled for; a rollback simply stops producing that key, and
  redeploying the newer revision makes them cache hits again.
- **The website catalog pin has to move with it.** `rubato-audio` pins
  `sourceRef` in `plaits-pins.json`. Rolling back across a schema change
  without re-syncing leaves the editor advertising recipes the builder will
  reject — going from schema 6 to schema 5, for instance, makes the 32-slot
  fourth bank unbuildable while the UI still offers it.

Field guides survive a rollback: manual keys hash documentation identity, the
recipe content the guide prints, and `PLAITS_MANUAL_CONTRACT` — deliberately not
the source revision.

The July 17 production smoke test completed build
`76e8c1c9dde6b238be377994dc27d62116acaa67f585547d6823afa1b40447cb`
and confirmed an immediate R2 cache hit on repeat submission.
