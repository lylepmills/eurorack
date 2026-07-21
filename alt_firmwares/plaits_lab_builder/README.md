# Plaits Lab firmware build service

This directory contains the approved-engine backend for Plaits Lab. It accepts
legacy recipes and version 5 manifests containing 24 immutable engine
references, a fixed navigation preference, seven starting options, and one to
six bounded chord-table documents. It generates a compile-time configuration,
builds with the pinned Mutable
Instruments ARM toolchain, enforces the Plaits flash and RAM limits, and returns
the 48 kHz audio updater.

The service is split across two isolation layers:

- A Cloudflare Worker validates and hashes recipes, stores job state in Durable
  Objects, schedules work through Queues, and caches successful WAV files in R2.
- A non-root Cloudflare Container has no runtime internet access. It owns the
  allowlisted C++ registry generator and the compiler. The request cannot
  provide source, paths, make targets, flags, or shell fragments.

## API

The browser-facing API is public at `https://plaits-api.rubato.audio`. It does
not require an account, email address, cookie, API token, or customer identity.

- `GET /v1/catalog` returns the approved engine and chord-table contract.
- `POST /v1/builds` accepts a Plaits Lab manifest and returns a deterministic
  build ID with `queued`, `building`, or `succeeded` status.
- `GET /v1/builds/:buildKey` returns durable job state.
- `GET /v1/builds/:buildKey/firmware` streams the cached WAV from R2.

## Personalized manual prototype

The authoritative catalog now includes complete control and trigger prose with
a documentation digest that is independent from firmware package identity.
Generate the current audition-layout field guide with:

```sh
python3 render_manual.py audition_recipe.json \
  ../../output/pdf/plaits-lab-audition-field-guide.pdf
```

The PDF uses the public green/red/amber order, shows all 24 positions, and then
deduplicates repeated engines in the detailed model reference. The renderer is
deterministic and runs with ReportLab inside the compiler image. It deliberately
omits internal layout IDs and per-model attributions. Generated PDFs are ignored
build products and should be reproduced from the recipe.

Manual generation is integrated into the service (2026-07-18): the queue
consumer renders the PDF through the container's synchronous `POST /manual`
endpoint after a successful compile (a manual failure never fails the
firmware build), stores it in R2 under `manuals/<manualKey>.pdf`, reports
`manual.status` / `manual.downloadUrl` in job status, serves
`GET /v1/builds/:buildKey/manual`, and backfills the PDF for already-cached
firmware via `manualOnly` queue messages. `computeManualKey` hashes the slot
layout + each engine's DOCUMENTATION digest + `PLAITS_MANUAL_CONTRACT` —
deliberately not the firmware source revision or toolchain — so prose-only
edits never invalidate firmware and firmware rollouts keep reusing cached
manuals. Bump `PLAITS_MANUAL_CONTRACT` when the renderer's layout changes.
This checkpoint is NOT yet deployed: it needs a new container image AND
Worker deploy. Because the firmware source also changed since the deployed
`schema5-20260717` image (the chord-table `ChordBank` rework moved the
`chords` engine digest), that rollout must bump `PLAITS_SOURCE_REVISION`,
use a new immutable image tag, and land together with the website catalog
re-sync (`rubato-audio/website/scripts/sync-plaits-catalog.mjs`).

The build key covers the normalized slots, preferences, starting options,
ordered chord-table data (without `createdAt`), source revision, toolchain
identity, and build-contract version. Identical recipes therefore share the
same immutable artifact.

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

Verified mixed-recipe output at source revision `303a9afad9f1`:

- ARM text: 199,152 bytes
- ARM data: 48 bytes
- BSS: 27,360 bytes
- Binary SHA-256: `1e56ba075dcb914e1b137b931d199f2164b8dae8f1c8493ae7446de73fdce521`
- WAV SHA-256: `b0a6949509a6a84951e7c29e7ef19136a890706b2737d803c1d6f0f751e18bbb`

These are revision-specific. The July 17 schema-5 figures (199,952 / 48 / 27,392,
binary `564c2322…`, WAV `2e9a93cb…`) held at revision
`a7f437964326+55b8da14febf` and no longer reproduce, so re-measure against the
revision under test rather than treating either set as an expected value.

## Cloud deployment

The production Worker, queues, R2 bucket, Durable Objects, and compiler
Container are managed by `wrangler.jsonc`. Before each firmware-source rollout:

1. Compute and set a new immutable `PLAITS_SOURCE_REVISION`.
2. Build and push the matching container image tag.
3. Regenerate the engine allowlist: `pnpm run catalog:regen` (rewrites
   `../plaits_lab_catalog/public_catalog.json` from the exporter at the
   checked-out commit), then commit any change. Each engine digest hashes its
   catalog metadata *and* source bytes, so even a display-name edit invalidates
   it — a stale `public_catalog.json` makes the Worker reject every recipe the
   website emits with "unavailable package version". `pnpm run deploy` runs
   `catalog:check` as a hard gate and refuses to ship a stale allowlist.
4. Re-sync the website catalog to the SAME commit
   (`website/scripts/sync-plaits-catalog.mjs`) so its per-engine digests match
   the builder's — otherwise engines whose source moved since the old pin start
   rejecting. The builder allowlist and the website snapshot must always be
   generated from one commit.
5. Run the contract, generator, type, and dry-run deployment checks.
6. Deploy with `pnpm run deploy` (not `pnpm deploy`, which is pnpm's built-in
   workspace-deploy command) and wait for the Container image rollout.

Cloudflare's rate-limit binding allows five new compilation requests per source
IP per minute. Cache hits and repeated polls for an already queued build bypass
that limit. This is a lightweight abuse guard rather than an account or billing
system; IP addresses are not stored in Durable Objects or attached to firmware
artifacts.

The production compiler image is
`plaits-lab-build-service-firmwarebuilder:rev-303a9afad9f1` (immutable
commit-derived tags replaced the date-based convention). After deploying a
new image, wait for `wrangler containers list` to report `ready` before smoke
testing; requests made while the application was still `provisioning` reached
the previous live instance during the schema-5 rollout.

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

Field guides survive a rollback: manual keys hash documentation identity and
`PLAITS_MANUAL_CONTRACT`, deliberately not the source revision.

The July 17 production smoke test completed build
`76e8c1c9dde6b238be377994dc27d62116acaa67f585547d6823afa1b40447cb`
and confirmed an immediate R2 cache hit on repeat submission. The complete
cross-repository loose-end list is maintained in
`../PLAITS_LAB_PROJECT.md`.
