# Plaits Lab contributor v0 handoff

Updated July 17, 2026. This is the authoritative continuation note for the
community-model SDK, catalog, contributor center, and submission lifecycle.
`PLAITS_LAB_PROJECT.md` remains the broader firmware/product checkpoint.

## What is implemented

- `plaits_lab_catalog/catalog.json` is the source of truth for 39 stock,
  Rubato, and audition models. Validation derives an immutable digest from
  normalized metadata plus implementation source. The generated public catalog
  is consumed by both the editor and firmware recipe builder.
- Recipe references pin engine ID, package ID, semantic version, and digest.
  The editor migrates older recipes; the compiler independently rejects any
  reference outside its generated allowlist.
- `plaits_lab_sdk/plaits_lab.py` implements `catalog`, blank and built-in
  `init --from`, `check --full`, `render`, hot-reloading `dev`, deterministic
  `submit`, and explicitly unreviewed local `build --hardware` workflows.
- Full local validation enforces package/license/source policy, compiles and
  runs address/undefined-behavior sanitizers, executes every declared scenario,
  and records duration, peak, RMS, DC offset, silence fraction, and host
  realtime cost.
- The local browser bridge supports the four model controls, pitch, trigger,
  MIDI, scope/spectrum, and A/B rendering against every built-in model. It binds
  only to localhost by default and restricts browser origins to `--editor`.
- A local community package can be linked into a complete ARM audio updater.
  The command uses the pinned local toolchain or the AMD64 Docker image and
  labels the result unreviewed.
- The legacy editor's `/contribute` route provides fork discovery, local audition,
  deterministic bundle upload, device-local draft recovery, and lifecycle
  status. R2 stores private ZIPs and D1 stores metadata.
- The API enforces unique package/version pairs and the ordered lifecycle
  `draft -> in-review -> checks-passed -> hardware-beta -> published`.
  Maintainer transitions require a server-side reviewer token.
- The public recipe editor and firmware download flow now live at the unlisted
  <https://rubato.audio/plaits-lab> page. The contributor center has not moved
  there; its Sites deployment was a development prototype and is not the
  production Plaits Lab home.

## Proven verification

- Catalog validation and generated-file byte comparison: 39 packages.
- SDK unit/integration suite: blank package creation, policy checks, sanitized
  previews, deterministic bundle contents, and built-in provenance.
- Python and TypeScript builder contract suites through schema 5, including
  digest rejection, firmware-profile policy, chord edits, and
  green/red/amber to amber/green/red registry translation.
- Editor TypeScript, ESLint, production build, server-rendered contributor page,
  firmware proxy tests, and complete submission lifecycle tests.
- Native host synthesis regression: `Synthesis engine tests passed.`
- Local SDK HTTP package and preview endpoints returned valid 48 kHz stereo WAV.
- A fork of Pulsar linked into a real ARM updater at 201,200 bytes text,
  48 bytes data, and 23,952 bytes BSS.

## Loose ends before public intake

These are deliberate production gates, not hidden completed work.

1. **Production contributor integration.** The production editor and firmware
   API live in `lylepmills/rubato-audio`; they do not yet include the legacy
   `/contribute` UI, submission API, D1 schema, or R2 bundle storage. Migrate
   those pieces deliberately into Rubato (or choose a separate maintained
   service), then re-run the full browser/API/security test matrix. Do not
   deploy `plaits-editor` as the production Plaits Lab site.
2. **Server-side revalidation.** Upload currently validates metadata, size, and
   ZIP signature, but trusts the contributor-reported content digest and audio
   report. Extract bundles with path/size limits and rerun the exact SDK checks
   in a networkless, resource-limited queue/container.
3. **ARM budgets.** Add worst-case cycle measurement, allocator high-water
   mark, stack measurement, and enforceable per-model flash/RAM budgets. Host
   realtime ratio is diagnostic only.
4. **Catalog promotion.** A D1 transition to `published` only exposes public
   metadata. It does not copy C++ into this repository or widen the hosted
   firmware allowlist. Maintainer promotion must verify provenance, merge the
   exact digest's source, add it to `catalog.json`, regenerate both public
   catalogs, review the diff, and redeploy the builder/editor. Automate this
   only after catalog signing and audit history exist.
5. **Reviewer operations.** Provision `PLAITS_REVIEW_TOKEN` as a secret, add an
   authenticated maintainer queue/checklist, preserve every transition in an
   append-only audit log, and distinguish automated evidence from human signoff.
6. **Contributor identity and recovery.** The public firmware builder requires
   no identity. The separate contributor prototype gives draft ownership to a
   random device-local bearer token; there is no account recovery, cross-device
   continuation, contributor profile, ownership transfer, or
   list-all-my-submissions endpoint. Choose an identity design only if those
   contributor features need it; do not add identity to ordinary builds.
7. **Moderation and abuse.** Before opening source submissions, add upload
   quotas, malware scanning, license/provenance evidence, takedown/reporting,
   delete/export/retention controls, and operational alerts. The production
   firmware endpoint already has a permissive five-new-builds-per-IP-per-minute
   guard; it is not a substitute for submission moderation.
8. **Hardware beta.** Pulsar and the eleven audition engines still need focused
   hardware listening. Define beta cohort size, test matrix, issue template,
   rollback/deprecation policy, and the threshold for publication.
9. **SDK distribution.** The v0 CLI is invoked from this checkout. Package a
   versioned installer, pin supported compilers/OSes, publish upgrade policy,
   and add Windows/Linux/macOS CI before recruiting non-repository contributors.
10. **Preview limits.** Built-in A/B models compile on first use. Models that
   depend on user-data banks need explicit preview fixtures. Add deterministic
   reset/repeatability, denormal, stuck-output, fuzzed-control, and long-run
   tests beyond the two default scenarios.
11. **Database migrations.** The Worker creates its v0 D1 schema idempotently at
    request time. Replace this with checked, versioned migrations before schema
    evolution or multi-region public use.

## Publishing and repository state

- Editor contributor v0 is commit `3ec7e13` in `plaits-editor`; subsequent
  firmware-proxy/profile commits are descendants. That checkout has no Git
  remote. Production-relevant recipe-editor and schema-5/chord-table work is
  preserved in `lylepmills/rubato-audio` on `main`; the remaining contributor
  center exists only in the local legacy checkout until it is migrated or a
  standalone upstream is deliberately chosen.
- Firmware work belongs on `codex/plaits-experimental-engines` in
  `lylepmills/eurorack`. This checkpoint intentionally combines the engine,
  catalog, schema-5 chord-table, firmware-profile, SDK, personalized-manual,
  and hosted-builder sources required to reproduce the deployed product.
- Generated audio, PDFs, `.wrangler` state, `node_modules`, and compiler build
  products are not source artifacts and must remain untracked.

## Continuation sequence

1. Confirm the firmware and `rubato-audio` worktrees, then read this file and
   `PLAITS_LAB_PROJECT.md`. Consult `docs/contributors.md` in the legacy editor
   only when working on the contributor flow.
2. Regenerate the public catalog into a temporary file and byte-compare it with
   both checked-in consumers before changing package metadata or source.
3. Keep the public firmware builder closed to arbitrary source. Test community
   packages locally or in the separate review sandbox only.
4. Implement server-side revalidation and resource evidence before making
   source submission public; ordinary approved-recipe firmware builds are
   already public without login at the unlisted Rubato page.
5. Promote the first package manually through source review, hardware beta,
   catalog regeneration, and approved-builder deployment; use that exercise to
   define the automated maintainer workflow.
