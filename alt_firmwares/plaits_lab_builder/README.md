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

Firmware WAVs are already stored in R2. Manual generation in the queued build,
a documentation-specific artifact key, cache backfill for builds that already
have a WAV, `GET /v1/builds/:buildKey/manual`, job-status metadata, and the
production editor download control remain open. Keep the documentation digest
separate from the firmware build key so prose changes do not force a recompile.

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

Build the production-shaped image from the firmware repository root:

```sh
docker build --platform linux/amd64 \
  -t plaits-lab-builder:local \
  -f Dockerfile.plaits-builder .
```

Start it and submit either recipe fixture to `POST /build`. The mixed fixture is
the useful proof case because it combines all three built-in DX banks with all
four Rubato engines, which the former stock/experimental compile switch could
not express.

Verified mixed-recipe output at this checkpoint:

- ARM text: 199,952 bytes
- ARM data: 48 bytes
- BSS: 27,392 bytes
- Binary SHA-256: `564c23226f6bc501ce8d1f2d55bc9f8a505ec7d5cb8e0fae6b263a42a4db78cb`
- WAV SHA-256: `2e9a93cb88efe9b449a6a93cb8c8f07551ffe4afaec7a750af3f9e511dfd7312`

## Cloud deployment

The production Worker, queues, R2 bucket, Durable Objects, and compiler
Container are managed by `wrangler.jsonc`. Before each firmware-source rollout:

1. Compute and set a new immutable `PLAITS_SOURCE_REVISION`.
2. Build and push the matching container image tag.
3. Run the contract, generator, type, and dry-run deployment checks.
4. Deploy with `pnpm deploy` and wait for the Container image rollout.

Cloudflare's rate-limit binding allows five new compilation requests per source
IP per minute. Cache hits and repeated polls for an already queued build bypass
that limit. This is a lightweight abuse guard rather than an account or billing
system; IP addresses are not stored in Durable Objects or attached to firmware
artifacts.

The production compiler image is
`plaits-lab-build-service-firmwarebuilder:schema5-20260717`. After deploying a
new image, wait for `wrangler containers list` to report `ready` before smoke
testing; requests made while the application was still `provisioning` reached
the previous live instance during the schema-5 rollout.

The July 17 production smoke test completed build
`76e8c1c9dde6b238be377994dc27d62116acaa67f585547d6823afa1b40447cb`
and confirmed an immediate R2 cache hit on repeat submission. The complete
cross-repository loose-end list is maintained in
`../PLAITS_LAB_PROJECT.md`.
