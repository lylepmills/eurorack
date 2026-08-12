# Plaits Palette firmware build service

This directory contains the approved-engine backend for Plaits Palette. It accepts
legacy recipes and manifests through schema 23 containing 24 or 32 versioned
engine references, firmware preferences and starting options, and bounded
chord-table/custom-FM/scale-bank/Speech-bank resources. Schema 15 can target either Mutable
Instruments Plaits or Plum Audio Ro'Ved and adds the color-blind bank display.
Schema 16 adds an ordered bank of up to 16 editable scales shared by Diatonic
Chord and Scale Stack, plus automatic LEVEL routing, which
uses LPG decay on ordinary oscillator models and preserves LEVEL as
velocity/accent on self-enveloped models. Schema 17 adds a selectable set of
the five shipped Speech banks plus up to eight total stock/custom LPC banks;
each custom bank has up to 32 selector words within the shared 1,024-frame
duration ceiling. Custom banks carry bounded 14-byte decoded frames and word boundaries rather
than source recordings or synthesis-model data. Schema 18 adds the Stock,
Drift, or Step unpatched-attenuverter mode to Starting Options. The selected
mode participates in the generated options profile. Schema 19 adds triggered
and gated one-knob envelopes as the fifth and sixth locked-FREQUENCY
assignments. Selecting either compiles both runtime choices into the firmware;
builds that do not select a contour omit their code to preserve flash. Schema 20
adds the opt-in `replaceableFmBanks` preference: each 6-Op FM bank's baked array
becomes the flash region a TIMBRE transfer erases and reprograms, so any or all
of a build's FM banks can be replaced independently, repeatedly, and without a
reflash. It is OPT-IN because page-aligning the banks costs ~816 bytes and the
stock 24-model preset has 768 bytes spare — defaulting it on would have pushed
the default build past the flash limit. An un-flagged recipe emits the
historical layout and keeps the module's single legacy user-data region, so
opting out is the behaviour Plaits has always had. Schema 21 adds experimental
`Sync In` as a fifth MODEL-input assignment. Rising edges reset oscillator phase
sample-accurately in engines with a native reset path; every other engine uses a
bounded first-edge-per-audio-block fallback so the feature cannot multiply an
expensive engine's render cost without limit. Fast sync and demanding model,
parameter, or stereo combinations can produce digital distortion or dropouts;
the generated field guide carries the same warning as the editor. Sync code is compiled only when asked for, and its flash cost varies with the
engines in the palette. Schema 22 moves that decision onto its own `syncInput`
preference. Before v22 it was derived from the STARTING value, which made a
compile-time capability depend on an initial runtime setting: Starting Options
are values the user can change on the module afterwards, so a build that did not
pre-select Sync In could never reach it — `ui.cc` sizes the MODEL-input menu as
`4 + PLAITS_BUILD_ENABLE_SYNC_INPUT`, so the mode is simply absent. The starting value no longer implies the
capability: a recipe that starts the module in Sync In without the preference is
REJECTED, because emitting it would compile a four-entry MODEL menu while
starting the module at index four — past the end of its own menu. Independently of the flag, a
transferred bank now records how many patches it holds, so a short bank sizes
the HARMONICS quantizer to its real count instead of repeating to fill 32. A
schema-23 candidate adds two independent Experimental options. `linearTzfm`
changes the FM law on 29 qualified models: counter-clockwise is linear
through-zero FM, clockwise remains regular exponential FM, and the center is
off. Models without a meaningful signed oscillator direction keep their stock
bipolar exponential FM law. `fastFm` dedicates the shared fast converter to a
continuous 50 kHz FM stream; that makes LEVEL CV unavailable throughout the
build, including while a non-qualified model is selected. The 34 models that
passed the hardware headroom and transport tests consume the fast stream;
every other model retains its normal control-rate exponential FM, sampled from
the same converter. Either option can be selected alone or together. The
catalog's `fmCapabilities` lists are the public product policy used to explain
those per-model differences in the editor and generated field guide. Guides
show the same active-only `TZ` and `50k` badges beside model references, with a
legend on the bank-map page. The failed periodic-LEVEL
experiment and borderline Brass result are not part of schema 23. This
candidate is not deployed.
one-shot marker in the application image is restored by every WAV or HEX flash, so first boot applies
all embedded Starting Options even when reinstalling the exact same build;
ordinary power cycles still preserve later runtime changes. It generates a compile-time
configuration, builds with the pinned Mutable
Instruments ARM toolchain, enforces the Plaits flash and RAM limits, and returns
either the default 48 kHz audio updater or, when explicitly requested, an
application-only Intel HEX file for a direct hardware programmer. The HEX starts
at the firmware's linked application address and deliberately excludes the
bootloader.

Schema-15/Ro'Ved support passed its hardware checklist on July 30, 2026.
Schema 22 is available in production with recipe-driven scale banks, automatic
LEVEL routing, selectable/custom LPC Speech banks of up to 32 words, text and
recording encoders, source/engine audio previews, unpatched-attenuverter modes,
and the triggered and gated FREQUENCY contours. It also includes schema 20's
replaceable FM banks and schema 21's experimental Sync In, now controlled by
schema 22's explicit opt-in preference. Schema 23 remains an undeployed
development candidate.

## Editable local source builds

`export_recipe_source.py` turns a saved browser configuration into the generated
headers, source map, pinned revision, and build script needed to reproduce that
Palette locally and modify its firmware. The short, user-facing instructions
live in the dedicated
[`plaits_palette_source` guide](../plaits_palette_source/README.md).

The generated `build.sh` calls `validate_local_build.py` after linking. That
applies the hosted builder's flash, RAM, and replaceable-FM page-layout gates,
but it cannot certify arbitrary DSP or control-code changes as safe.

An August 4 hardware regression exposed two firmware defects in the custom
Speech path: inconsistent `PLAITS_HAS_CUSTOM_SPEECH_BANKS` values changed the
`Voice` class layout between compilation units, and discrete LPC playback could
read one frame past a word boundary. A patched build passed both the stock-bank-
only control and the original two-custom-bank recipe on hardware. The recovery
completed at `rev-812937f27ada`, including custom-bank level matching and the
Six-op flash fix, before the split Speech rollout below.

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
byte for byte. The recovered path later passed its module test and returned to
production at `rev-812937f27ada`.

The August 5 Speech split shipped at `rev-174d93845372`: Original Speech remains
available, Speech Sounds travels continuously from Naive through SAM to LPC
phonemes, and LPC Words shares the selectable stock/custom word-bank resource.
Production canary build
`1df4bbb1c13c114b2c1ab802c5f31652b292c19f3fd1b64ccb13bdcf0404e140`
compiled all three models with two custom banks at 58,804 B text + 48 B data and
20,172 B BSS. Its public 48 kHz mono WAV is 4,803,692 bytes with SHA-256
`b2af16801f581279ba2107dc1a308ac389e12f483f0e6eb5a1adb0e723e6674f`;
the matching field guide also reached ready.

The Mutable-attribution and divided-icon follow-up shipped at
`rev-769417c8b8e3`. Production canary build
`dd9c1c3dc7367fac5eec17c171e40d868cda3c1aee63014e82d97100f06c7291`
again compiled all three models with two custom banks at 54,068 B text + 48 B
data and 20,316 B BSS. Its 4,492,268-byte WAV has SHA-256
`443bc8e7b887a6ed6b5df222a589c42da1836ecd7f1398b86a793d34988bc3aa`
and binary SHA-256
`772af22234dac433bbb3d6775e38185e0eff9f39f392c6fda3f082fae0e6e9c8`;
both match the exact local release candidate, confirming the follow-up changed
catalog provenance and artwork without changing compiled firmware bytes.

The August 7 community-engine rollout shipped Dylan Bolink's Clap and Freshets
Formant at `rev-1b8ecdaddb33`. Their one-model CPU-probe builds passed on a
physical Plaits at 4–5 LEDs for Clap and no more than 6 LEDs for Freshets, with
no audible faults. The exact staged release canary, build
`43bb706c50fe971029a4b6e565d754ff069827cd8bbd3a778587c51085c07253`,
then installed, booted, navigated and produced audio on the module; its WAV
SHA-256 is
`641b68925496d07dc1cbf9d8ed77038e8625c4c1a645fa3427fc872dfdb29619`.
Production canary build
`5ff65ceee30ad7c746266b7dcd5c45a45c648b7ee033a660fa37452b46687761`
compiled both new engines alongside Original Speech, Speech Sounds and LPC
Words at 59,044 B text + 48 B data and 20,636 B BSS. Its 4,803,692-byte WAV
has SHA-256
`a0e531633008a18d6dc90e545b1c55c566fc15c8578a9993816ecb3005783075`
and binary SHA-256
`9356cc152e3392368e86835418296f2422a94b989685d70374d0868117f016b9`;
the matching field guide also reached ready.

The August 8 replaceable-FM-banks rollout shipped at `rev-8d56c1936c5c`, a
revision merging the community engines already in production with schema 20. The
staged WAV passed on physical hardware. Production canary build
`080a0ce6212b257d1af4ee8be3f649dd1875fa41ab1261b1329365bfa7ba7500` compiled a
schema-20 recipe with `replaceableFmBanks` enabled and all three FM banks placed;
its 9,630,764-byte WAV has SHA-256
`4704cae93316315c7017c7b39b4c01e47beeb397dd20ee1125235478b992c8f1`.

The August 8 Sync In rollout shipped at `rev-fc594b275f0d` as schema 21 and
manual contract 16. MODEL can now detect audio-rate rising edges: eleven
oscillator engines use sample-accurate reset paths, while all other engines use
a bounded first-edge-per-block fallback. The ordinary stock firmware remains
inside flash at 228,516 / 229,376 bytes. A paired 24-model reference measured a
19,552-byte Sync delta; the editor charges a conservative shared-plus-native
cost and warns that fast sync and demanding model/stereo combinations can cause
digital distortion or dropouts. Staging canary build
`e523c4956410996bcd7b58b906dc5b480b166c4ea44fdcf06ea5ac0b81c92a6d`
compiled Sync In with Original Speech, Speech Sounds, LPC Words, and a custom
bank at 50,260 B text + 48 B data and 20,380 B BSS. Its WAV SHA-256 is
`fa6c4cf2310a023b203af2d26f5b455d1062b836e63b69062ec3d6668a010679`;
the downloaded field guide was extracted and verified to contain both the fifth
MODEL-input setting and the processing-headroom warning. Production then rebuilt
the same deterministic build id from its separate cache (`cacheHit: false`): the
4,180,844-byte WAV had the same
`fa6c4cf2310a023b203af2d26f5b455d1062b836e63b69062ec3d6668a010679`
SHA-256, and its 9,590-byte contract-16 field guide again contained the Sync In
setting and warning. The production container pool settled with both configured
instances healthy on `rev-fc594b275f0d`.

Sync In became an opt-in Advanced preference the same day, at `rev-c6684dea562b`
as schema 22. Its compile switch had been inferred from a STARTING value: a
recipe that began in Sync In got the code and silently paid its flash, and one
that did not could never reach the mode from the menu it advertised. The
capability is now its own preference, and the two must agree — starting in Sync
In without it would leave `model_cv_option` pointing past the end of the
compiled MODEL menu, so the Worker and `generate_engine_config.py` both refuse
that pair. The staged WAV passed on physical hardware. Production canary build
`10739dcb15f67551df83af5bf4dad8aefe581fa8ecba77419c3a7d989952eb58` compiled
Sync In with all three Speech engines and a custom bank; its 4,180,844-byte WAV
has SHA-256
`fa6c4cf2310a023b203af2d26f5b455d1062b836e63b69062ec3d6668a010679` — the same
bytes the hardware gate flashed, since the build is content-addressed and both
environments resolved the same id.

The August 10 Speech-bank capacity rollout shipped at `rev-44453427c187`.
Custom banks now accept up to 32 selector words while retaining the shared
1,024-frame duration ceiling. Staging canary build
`c7b17e7de3032579551e8ebb653f0d2d6a1a82a8049aa778d547f822c2107d51`
compiled a 32-word, 640-frame bank; its 4,803,692-byte WAV has SHA-256
`c25ad6b8ef216c659d6658bb6e4414d991f5a8e42f05a7216dc545f8bfe92a16`
and passed boot, navigation, and first/last-word playback on physical hardware.
After the gradual production rollout settled on both configured instances, a
fresh Speech encoder identity accepted an explicit 32-entry request at 608
frames. Production canary build
`da86571a3d2342eddfbef273f133427c08974e7b923aa769e1dbe915d9024ce2`
then compiled the full 32-word, 640-frame release gate; its 4,803,692-byte WAV
has SHA-256
`ee3f40eebe7b14e007509207356367dfe5b0890ba8c9b26e5f48e8f5fba7a730`.

Two rollout notes worth keeping:

- The Worker's preference set is CLOSED and it rebuilds normalized preferences
  field by field, so a new preference needs FOUR edits, not one: admitted by
  `hasExactKeys`, version-gated, added to the `schemaVersion` union, and
  re-emitted. Bumping `maxRecipeSchemaVersion` alone left staging rejecting the
  field with `invalid_preferences` before the container saw it — the same
  Worker/container split that failed the `rev-4749aec727af` canary, caught in
  staging this time. The re-emission is the edit that hides: schema 22 shipped
  to staging with `normalizeRecipe` still stamping 21 from the old sync-in
  starting value, so the Worker accepted a valid recipe, re-emitted it a version
  too low, and the container rejected the Worker's own output. A new version is
  not landed until something asserts the version it NORMALIZES to.
- **`wrangler containers info` reporting `healthy >= 1` does NOT mean the new
  image is serving.** Production rolls gradually, so during a rollout the healthy
  instance is the OLD one; the first production canary failed with the previous
  image's `schemaVersion must be 2 through 19`. Gate on
  `configuration.image` matching the new tag with `starting == 0`, not on the
  health count. A build that fails against a stale instance is not cached
  permanently — the same recipe recompiles and succeeds once the rollout lands.
- The named Speech encoder Container can outlive a gradual image rollout. Do
  not exercise a freshly bumped identity while `configuration.image` still
  names the old image: it can attach to that image and keep serving it after the
  pool advances. The 32-word production rollout caught this safely; the early
  `speech-encoder-v6` probe still enforced 16 words, so production rotated to
  `speech-encoder-v7` only after the new image had `starting == 0`.

The service is split across two isolation layers:

- A Cloudflare Worker validates and hashes recipes, stores job state in Durable
  Objects, persists normalized recipes in R2, schedules reference-only build IDs
  through Queues, and caches successful WAV/HEX files in R2. Keeping recipes out
  of queue messages lets large custom-FM palettes stay below the 128 KB message
  limit; consumers still accept embedded recipes while older jobs drain.
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

## Exporting an editable local build

User-facing setup and build instructions live in
[`alt_firmwares/plaits_palette_source/README.md`](../plaits_palette_source/README.md).
The exporter accepts the browser's **Save configuration** download directly and
refuses to overwrite a non-empty directory. Its implementation and tests stay
here beside the generator and hosted build path they reuse.

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
6. Deploy the candidate to the named staging environment with `pnpm run
   deploy:staging`. Staging uses a one-step Container rollout so the old and new
   image do not temporarily consume its two-instance test capacity. It uses the
   same immutable image tag and source
   revision as production, but separate Durable Object namespaces, queues,
   dead-letter queue, R2 artifact bucket, and rate-limit namespace. Its stable
   endpoint is `https://plaits-api-staging.rubato.audio`.
7. Deploy the noindex `rubato-audio-staging` Pages project, then run the one
   intentionally slow release gate:

   ```sh
   PLAITS_EXPECTED_SOURCE_REVISION=<revision> \
   PLAITS_STAGING_ARTIFACT_DIR=<hardware-gate-dir> \
   pnpm run smoke:staging
   ```

   It checks CORS and environment identity, source-voice and stock-bank
   previews, text encoding, saved-bank preview restoration, one real firmware
   compile containing Original Speech, Speech Sounds, and LPC Words with
   reduced stock banks, both downloads, and writes the exact WAV/recipe used by
   the gate. A repeat against the same revision reuses the staging caches. The
   smoke tolerates only the bounded
   503/522 window while a newly created Container application provisions; all
   other failures stop immediately.
8. Flash and play the exact staged WAV. Only after that pass, deploy production
   with `pnpm run deploy` (not `pnpm deploy`, which is pnpm's built-in
   workspace-deploy command). This promotes the already-pushed image; do not
   rebuild it between staging and production. Wait for the Container rollout,
   then exercise the production canary before enabling a new public UI flag.

Website-only releases keep their ordinary build path. The real compiler smoke
is a builder/firmware release gate, not a tax on every copy or CSS deploy.

The first full staging gate passed on physical Plaits hardware on August 5,
2026, using the exact `rev-812937f27ada` staged WAV. The module booted and
navigated normally, produced audio, and played the custom "staging speech
hardware check" bank.

The split-Speech gate then passed on physical hardware at
`rev-174d93845372`, build
`80ffa4164cebc2b73f94a347b646a8d6fdc8274c39dc61e5fe354655b64bba41`.
Its exact staged WAV contained Original Speech and LPC Words, booted and
navigated normally, produced audio from both models, and played the shared
custom bank. The later production canary covered all three Speech engines. Its
SHA-256 is
`0845885d1cc89d2b66bddef6ea583dabc3f9b43865ec420023ec26069633b1c9`.

Schema 23 shipped on August 12 at `rev-de96c9d9f22b`, adding independent,
opt-in Linear TZFM and Fast FM preferences plus the FM Heaven preset. Catalog
qualification enables Linear TZFM for 29 models and Fast FM for 34; unsupported
models retain their ordinary control-rate exponential FM. Fast FM dedicates the
shared converter to a continuous 50 kHz FM stream, so LEVEL CV is unavailable
throughout a Fast-FM build. The final FM Heaven firmware passed the physical
module audition before rollout. Staging and production both rebuilt exact recipe
`9fb002c6e38b62823ceffbe1250f76ce56584a505d51248c9c5db4b8d7548634`;
its 15,859,244-byte WAV has SHA-256
`f28605521b8a5528f8db205ff517d394cda5358975efad57ef6fe7daf65138af`,
and its application binary has SHA-256
`98a1f63de43caf6351fcc990592d0c13199020e63f6c8061ef16178966a5aa46`.
The linked firmware uses 204,612 bytes of text, 48 bytes of data, and 23,116
bytes of BSS. Production Worker version
`8bcd8530-7184-49b9-8975-0965900d75f6` passed the full compiler canary before
the public editor was enabled; `rev-44453427c187` remains the immediate
rollback image.

Cloudflare's rate-limit binding allows five new compilation requests per source
IP per minute. Cache hits and repeated polls for an already queued build bypass
that limit. This is a lightweight abuse guard rather than an account or billing
system; IP addresses are not stored in Durable Objects or attached to firmware
artifacts.

The production compiler image is
`plaits-lab-build-service-firmwarebuilder:rev-de96c9d9f22b` (immutable
commit-derived tags replaced the date-based convention; the table below is the
full history — keep this line in step with its last row). After deploying a new
image, use `wrangler containers info <application-id>` and wait until
`configuration.image` matches the intended immutable tag and `starting == 0`;
`healthy >= 1` alone can still mean the previous image is serving during a
gradual rollout. A first-time staging application can temporarily return "no
Container instance available" while its image is starting; the bounded staging
smoke retries that response.

Schema 23 is live, with independent experimental Linear TZFM and Fast FM
preferences, Sync In as an opt-in preference, and custom Speech banks accepting
up to 32 words inside the 1,024-frame ceiling. It inherits schema 22's explicit
Sync capability, schema 21's experimental MODEL-input assignment, schema 20's
replaceable FM-bank preference, schema 19's
triggered and gated FREQUENCY contours, and schema 18's Stock, Drift,
and Step unpatched-attenuverter modes; schema 17's selectable stock LPC banks,
custom text/recording-derived Speech banks, source/engine previews; and the
earlier recipe-driven scale banks and automatic LEVEL routing. The generalized
schema-inheritance hardening from `5b2b077` is also live: current production
source `de96c9d9f22b` descends from that commit, so future supported schemas
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
| August 5, 2026 (recover default-palette flash headroom after Six-op CPU work) | `77cff28e2e70` | `rev-77cff28e2e70` |
| August 5, 2026 (match custom Speech bank levels to stock) | `812937f27ada` | `rev-812937f27ada` |
| August 5, 2026 (split Speech engines with custom banks shared by Original Speech and LPC Words) | `174d93845372` | `rev-174d93845372` |
| August 5, 2026 (schema 19 triggered and gated FREQUENCY contours, manual contract 15) | `d554b7f46dc0` | `rev-d554b7f46dc0` |
| August 5, 2026 (credit split Speech engines to Mutable Instruments; divided Speech icons) | `769417c8b8e3` | `rev-769417c8b8e3` |
| August 7, 2026 (Clap and Freshets Formant community engines) | `1b8ecdaddb33` | `rev-1b8ecdaddb33` |
| August 8, 2026 (schema 20 replaceable FM banks) | `8d56c1936c5c` | `rev-8d56c1936c5c` |
| August 8, 2026 (schema 21 experimental Sync In, manual contract 16) | `fc594b275f0d` | `rev-fc594b275f0d` |
| August 8, 2026 (schema 22 Sync In as an opt-in Advanced preference) | `c6684dea562b` | `rev-c6684dea562b` |
| August 10, 2026 (32-word custom Speech banks) | `44453427c187` | `rev-44453427c187` |
| August 12, 2026 (schema 23 Linear TZFM + Fast FM; FM Heaven) | `de96c9d9f22b` | `rev-de96c9d9f22b` |

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
