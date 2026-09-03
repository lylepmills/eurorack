# Plaits Palette firmware build service

This directory contains the approved-engine backend for Plaits Palette. It accepts
legacy recipes and manifests through schema 28 containing 24 or 32 versioned
engine references, firmware preferences and starting options, and bounded
chord-table/custom-FM/scale-bank/Speech-bank/custom-model resources. Schema 15 can target either Mutable
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
schema 23 adds two independent Experimental options. `linearTzfm`
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
experiment and borderline Brass result are not part of schema 23. Schema 24
adds custom Wave Terrain and Wavetable resources. A `customModelData`
assignment can attach one sampled, Mutable-compatible 4 KB block to a Wavetable
slot. The browser stores its source equation for editing, but the trusted build
boundary accepts only bounded sampled bytes. Each assignment is an opt-in,
independently rewritable flash region: the recipe data seeds it, and a later
TIMBRE audio transfer while that slot is active erases and replaces those same
pages. Even identical seeds remain separate because their slots must be able to
diverge. Wave Terrain instead uses one ordered terrain bank shared by every Wave
Terrain slot. The eight Mutable factory terrains are
first-class entries that may be reordered or omitted; the generator compiles
only the retained factory equations. Each custom entry carries one bounded
sampled 4 KB terrain and owns its own independently rewritable flash region.
HARMONICS sweeps and interpolates across exactly the ordered bank, and also
selects which custom entry a TIMBRE audio transfer overwrites. A selected
factory entry refuses the transfer. The default eight-factory bank remains
implicit, so an untouched recipe stays on its older schema and stock layout. A
custom terrain can request the compiled `native` representation.
The Worker preserves the bounded 4 KB grid for browser preview and backwards
compatibility, while the trusted generator independently parses the equation,
accepts only the reviewed variable/function/operator vocabulary, reruns the
calibrated CPU gate, verifies finite output over a dense grid, and emits calls
to the same errno-free fast-math primitives used by the hardware benchmark.
Each native equation is normalized into the sampled terrain's output range and
linked as a function pointer in the ordered bank. It therefore costs only its
linker-pruned code and owns no erase-safe TIMBRE region; complex equations,
mesh imports, and sampled blends remain independently rewritable 4 KB entries.
The one-shot marker in the application image is restored by every WAV or HEX flash, so first boot applies
all embedded Starting Options even when reinstalling the exact same build;
ordinary power cycles still preserve later runtime changes. It generates a compile-time
configuration, builds with the pinned Mutable
Instruments ARM toolchain, enforces the Plaits flash and RAM limits, and returns
either the default 48 kHz audio updater or, when explicitly requested, an
application-only Intel HEX file for a direct hardware programmer. The HEX starts
at the firmware's linked application address and deliberately excludes the
bootloader.

Schema-15/Ro'Ved support passed its hardware checklist on July 30, 2026.
Schema 24 adds `simplifiedPitchRanges`, an Advanced option that reduces the
FREQUENCY range selector from twelve positions to its three most clockwise ones:
octave switching, fine tuning, and the full-range coarse sweep. It drops the
eight +/-7-semitone ranges and the sub-audio LFO range, which are redundant for a
coarse-then-fine-then-lock workflow -- one +/-7-semitone range already spans 14
semitones, wider than an octave, and octave switching covers the same 12..108
ground while being saved and quantised. Each surviving mode then gets a third of
the selector travel rather than a twelfth. It is compile-time only, costs no
flash (both layouts assemble to the same size), and changes no stored state, so a
module keeps its tuned root and locked octave when moved between the two layouts
in either direction. Leave it off for LFO use or one-gesture octave jumps.
Note the option is NOT experimental and is not in the Experimental section: it
changes which selector positions exist, not what any of them do.

Schema 26 introduced recipe-driven scale banks, automatic
LEVEL routing, selectable/custom LPC Speech banks of up to 32 words, text and
recording encoders, source/engine audio previews, unpatched-attenuverter modes,
and the triggered and gated FREQUENCY contours. It also includes schema 20's
replaceable FM banks and schema 21's experimental Sync In, now controlled by
schema 22's explicit opt-in preference, plus schema 23's independent Linear
TZFM and Fast FM options and schema 24's simplified FREQUENCY range selector.

## Editable local source builds

`export_recipe_source.py` turns a saved browser configuration into the generated
headers, source map, pinned revision, and build script needed to reproduce that
Palette locally and modify its firmware. The short, user-facing instructions
live in the dedicated
[`plaits_palette_source` guide](../plaits_palette_source/README.md).

The generated `build.sh` calls `validate_local_build.py` after linking. That
applies the hosted builder's flash, RAM, rewritable-resource page-layout, and
audio scratch-buffer alignment gates, but it cannot certify arbitrary DSP or
control-code changes as safe. The 16 KB `shared_buffer` must be 8-byte aligned
and entirely in SRAM: `BufferAllocator` does not align the typed pointers it
returns. A terrain canary linked this byte array at `0x20000c99`; the real
Virtual Analog renderer reproduced an alignment fault on an emulated Cortex-M4
with a one-byte-offset arena, while an aligned control rendered successfully.
The post-link check now rejects that layout regardless of the recipe's engines
or custom resources. The aligned, release-shaped v6 terrain canary then passed
on physical Plaits hardware: saved Wave Terrain selection and the fourth-control
MACRO assignment survived repeated power cycles; all 16 factory, native, and
prebaked terrains swept without frozen/repeated audio buffers; TIMBRE, MORPH,
HARMONICS, and MACRO remained responsive; and navigation into and out of Wave
Terrain continued to produce audio. Near-silent positions during the MACRO
sweep occurred symmetrically with the terrain geometry and recovered normally,
not as CPU stalls.

The exact schema-24 release gate used staging build
`b20070a737e3aeb915bb7fd91c9222af6bd8d9fa9985671951a2db34cf597119`,
compiler-stamped at source `e81f62517fc4`. Its 11,187,884-byte updater WAV
has SHA-256
`767aadf1ce1cac2cd2ac7307da4575deff1b7d14a183008e250c10c29cbc6a11`.
After the complete two-instance staging pool was healthy on that immutable
image, the WAV passed the physical checks above. The direct Core Audio gate
captured 86.773 seconds from MAIN through ES-8 input 2; it contained no clipped
samples, no 10 ms window below -50 dBFS, no held run longer than two samples,
and no consecutive identical render blocks at 16, 24, 32, 48, 64, or 128
samples. The capture's SHA-256 is
`2c51443da5c5e85998c0f3c925e94265c2767f9f45b591b70fc48876bef59e2c`.
After both production instances settled healthy on the same immutable image,
production build
`2b0d01f852c5acf0ac4acdd3bf561a004aeac558d47dc55adbcc17463c6ca195`
compiled fresh and stamped `e81f62517fc4`. Its 11,187,884-byte updater WAV
has SHA-256
`4f887102b566b9cbdbd10dd52796e47485deee7e291e58582f23d2d2a69c8529`;
the matching contract-19 field guide also reached ready.

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
  image is serving — and `healthy` does not mean what it looks like.** For a
  DO-attached application (all Containers apps are), Cloudflare's own API model
  defines `healthy` as the number of *prepared* container instances, not
  serving ones. `active` is the count of running containers. So the gate this
  file used to specify — image, `healthy`, `scheduling == 0`, `starting == 0` —
  **can pass while an old container is still attached and serving**, which is
  exactly what happened on 2026-08-31: the app reported the new image with two
  healthy instances and zero failures for over 25 minutes while requests kept
  hitting the previous image. Gate on `configuration.image` matching the new
  tag, `active_rollout_id == null`, `scheduling == 0`, `starting == 0`,
  `failed == 0`, **and `active == 0`** (no old container still attached).
  Then confirm with `GET /v1/health` rather than inferring. A build that fails
  against a stale instance is not cached permanently — the same recipe
  recompiles and succeeds once the rollout lands.
- **`GET /v1/health` answers "has the rollout landed?" in ~2 s.** It probes two
  containers and reports both against the Worker's own revision: a FRESH
  uniquely-named DO (no warm container, so it starts on the configured image —
  tells you whether the POOL rolled) and the speech singleton under its real
  name (fixed name kept warm by traffic — tells you whether the endpoint users
  hit is current). `poolMatches` false means keep waiting; `poolMatches` true
  with `speechEncoderMatches` false means rotate `SPEECH_ENCODER_CONTAINER`.
  Before this existed the only revision signal was the stamp on a finished
  firmware, which costs a queued multi-minute compile, so nobody asked during a
  rollout.
- **`wrangler containers instances <app-id>` names the singleton directly.** It
  is the only view that shows `speech-encoder-vN` as a row with its own state
  and age, which is what distinguishes "a warm old instance is still attached"
  from "the pool has not rolled".
- **`GET /v1/health` COSTS A PRODUCTION CONTAINER SLOT — never poll it in a
  loop.** The fresh uniquely-named DO that makes the probe trustworthy is a
  real container start, and production runs `max_instances: 2` with
  `speech-encoder-vN` holding one of them permanently. So health probes
  compete with user builds for the single remaining slot. Polling it during
  the 2026-09-02 Acid rollout exhausted capacity and put real builds into
  `compiler_retry` with "Maximum number of running container instances
  exceeded" — which then reads exactly like a broken rollout and nearly
  triggered an unnecessary image rollback. Probe ONCE, wait minutes between
  probes, and gate a rollout on `containers info` (free) plus a single
  confirming probe. This is why staging carries four instances (`effd546`);
  production has no such headroom. Builds retry rather than fail, so the
  damage is delay rather than data loss, but it IS user-visible.
- **`containers info`'s `active` count lags `containers instances`.** After the
  Acid rollout the former reported `active: 1` while the latter showed every
  instance inactive. Trust the instance list.
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
present, each custom FM bank's credit, the shared Wave Terrain order and each
custom terrain's compiled/prebaked storage behavior, and
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

Contract 19 adds the Wave Terrain bank page. It lists the exact HARMONICS order,
distinguishes stock, compiled-equation, and prebaked entries, and explains that
TIMBRE audio transfers replace only selected prebaked custom entries. The
terrain representation is part of `computeManualKey`, so changing a custom
terrain between native and prebaked cannot reuse a stale guide.

Contract 21 corrects the Ro'Ved guide against Plum Audio's manual and the
firmware that now matches it. Outside the options menu FREQ/TIMBRE step models
and MORPH/HARM change bank — the guide had the two pairs the other way round —
and model clicks stay inside the current bank. It also prints the LOCKED OCTAVES
section for Ro'Ved, which was suppressed entirely on the belief that the panel
had no octave gesture; pushing and turning FREQUENCY is that gesture, free
because a locked FREQUENCY is no longer setting pitch. Like contract 14, the
renderer landed while the Worker waited a cycle on 20; it shipped with the
schema-27 image `8f97241069cc`, whose rollout carried the Worker to 22 (21's
Ro'Ved prose plus 22's LIGHT 4 LED order). Never deploy a contract bump by
itself — until that image was live, the production container still rendered the
old Ro'Ved prose.

Since contract 21 this is enforced rather than remembered. `render_manual.py`
declares `MANUAL_CONTRACT`, the lowest contract that describes what it prints;
raise it in the same commit that changes the guide's layout or prose.
`check_manual_contract.py` — wired into `pnpm run deploy` and
`deploy:staging` next to `catalog:check` — then refuses any environment whose
`PLAITS_MANUAL_CONTRACT` is below what the renderer requires **at the commit
`PLAITS_SOURCE_REVISION` names**, which is the renderer actually inside the
image being deployed. That is deliberately not an equality check: a landed but
unshipped renderer constrains nothing, so the waiting state described above
still deploys. It also catches an image tag naming a commit other than
`PLAITS_SOURCE_REVISION`, and fails closed when the revision cannot be resolved
locally — an unfetchable commit means nobody can say what the running container
renders. A revision predating the constant requires nothing, so older rollouts
do not fail retroactively. Tests: `python3 -m unittest test_check_manual_contract`.

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

> **The schema-27 percussion release image `37c608fa2a69` carries
> `PLAITS_MANUAL_CONTRACT=22` in both environment blocks.** Preserve that value
> when promoting it to production: the Ro'Ved Field Guide's navigation and
> locked-octave prose changed with this firmware and its renderer ships in the
> same immutable image. Contract 22 also invalidates the schema-27 staging PDF
> cached before LIGHT 4's special LED order was documented. The combined image
> also carries the Acid community engine and was promoted only after the
> schema-27 physical-module validation. See the contract notes above.
>
> You do not have to remember the contract value: `pnpm run deploy` runs
> `contract:check`, which refuses any deploy whose `PLAITS_MANUAL_CONTRACT` is
> below the `MANUAL_CONTRACT` that `render_manual.py` declares **at the commit
> named by `PLAITS_SOURCE_REVISION`**, and names the value to set. Run
> `pnpm run contract:check` by hand at any point to see where a deployment
> stands.

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
   image do not temporarily consume its four-instance release-gate capacity. It uses the
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

Later on August 12, Worker commit `414d473529a7` moved normalized recipes into
R2 and reduced queue messages to reference-only build IDs, avoiding
Cloudflare Queues' 128 KB per-message limit for large custom-FM palettes.
Commit `7e3f51b5cc9f` corrected the consumer to load that already-normalized
internal representation without parsing it again as an external manifest.
Production Worker version `d4280dc1-2d1b-46ad-92b8-583f0a99cb96` then built
the exact live Ocean Drive '83 recipe
`19481c6a2b743db4452b5a63de16a33f6f427a789fa7bb3018f994be46e97a1e`;
both its firmware WAV and generated field guide were ready and returned HTTP
200.

The August 12 Z Filter metadata follow-up shipped at `rev-bf3c078fdf07`.
MORPH is now correctly documented as the shape gesture inherited from Braids:
saw through square at noon to triangle. Staging and production build
`a7bfd687e3aad7984319f589388ee0d41fbcb62d90ef996795f3f6d6ea213553`
compiled the exact recipe and LPC frames from production build
`c44bc0e2d62f1c4bb92e6d0e3f381312fc6ea42595f4cfedcfbfd25547606087`.
Both produced the same 4,803,692-byte WAV with SHA-256
`5b2268c4e4bcf76ce81c0be46de47ced5986e2c20bda838d6c2a02efcfdbce3c`,
confirming the rollout changes catalog identity and documentation without
changing compiled firmware bytes.

The complete Braids metadata audit shipped later on August 12 at
`rev-29e3dba36351`. It corrected controls, trigger behavior, and mono/stereo
output descriptions across all 34 ports and added manifest-to-catalog drift
checks. Staging build
`b96589cf244b5300f759ac387c6434cc5a1a15e43ded5a0c82bbb6483bf7804a`
and production build
`98b5b374aa6a625472863e458031a855f42a55aae1f35b0d0e2fc5eb1c5c8942`
both passed the full compiler canary. The production WAV remained 4,803,692
bytes with SHA-256
`5b2268c4e4bcf76ce81c0be46de47ced5986e2c20bda838d6c2a02efcfdbce3c`,
confirming again that the audit changed catalog identity and documentation,
not firmware audio behavior.

The remaining-engine metadata audit shipped on August 14 at
`rev-19666470542c`, completing implementation-level documentation coverage for
all 87 catalog engines. Staging build
`7bf8c5d91ba65cdd33a80e17f63b5fa4d632a83ce0e3957a9dcb4c3c0a4fff46`
passed the full release smoke. The first gradual production promotion was
rolled back after its canary was submitted while one pool slot was still being
scheduled and exhausted the compiler retry budget. A clean promotion waited
for both configured instances to report healthy with no scheduling or starting
instances; production build
`8ca35d64565f028abec84017d657fc43a9ad08ec2c64f9e5cd57fdda2412a913`
then passed on its first attempt. Its 4,803,692-byte WAV has SHA-256
`6876b0f4106e7429b5adb7e4e7f673093303d6fc8d1d463f58839f97624406e1`;
the generated field guide was ready and downloaded as a 9,997-byte PDF with
SHA-256
`c966faf0b738570efbd2714fd39db32f5f2782a5e88d1d1a8ba82259eb6d86d0`.

The August 21 octave-switching root fix shipped at `rev-ccf67eac9661`. Reaching
octave switching from below rooted the mode at note 96 (C7) because the hidden
HARMONICS range selector sweeps and each range crossed rewrites the sounding
note; the wrong root was then saved. No engine digest moved -- `catalog:check`
reported the allowlist already current at the new revision and the website
snapshot matched it, so this was a builder-only rollout with no catalog re-sync
and no site deploy. Production canary build
`154ad71af5fd697e3b51b75cdb0d9f8de76da99087d517ea75345f8820fffaf6`
rebuilt the gate recipe: 4,803,692-byte WAV with SHA-256
`dcae8300a81f0d35612f3ade3779113848fe512c62829758ebfce74dd708d872`,
application binary
`200dcef8ed81f4c5442c43456098244af6d7c2d15b6fbd0983d295b7a16b1a2c`,
203,700/48/28,188 text/data/BSS for the mixed fixture and 58,148/48/20,668 for
the gate recipe. `rev-0eec23f182a1` remains the immediate rollback image.

**This rollout exposed a defect in the staging gate: `computeBuildKey` hashes
the Worker's `PLAITS_SOURCE_REVISION` var, not the revision the container
actually stamps.** The two disagree for the length of a Container rollout. Here
`deploy:staging` returned and `/v1/catalog` reported `ccf67eac9661` immediately
-- that is the Worker var -- while the staging Container was still serving
`rev-0eec23f182a1`, so the smoke's build compiled on the OLD image and
`smoke:staging` passed anyway, because its
`PLAITS_EXPECTED_SOURCE_REVISION` check reads the catalog var rather than the
artifact's `X-Plaits-Source-Revision`. Proven by compiling the identical recipe
against both images locally: the old image reproduces the staged artifact
exactly (58,084/20,652, WAV `f758e695...`) and the new image reproduces the
production artifact exactly (58,148/20,668, WAV `dcae8300...`). Two consequences:

- **A staging gate could validate the previous firmware and still report green.**
  FIXED the same day: the Worker now surfaces the compiler-stamped revision as
  `artifact.sourceRevision` on `/v1/builds/:key` (it was already captured into R2
  `customMetadata`, just never read back), and `smoke:staging` asserts THAT
  against `PLAITS_EXPECTED_SOURCE_REVISION` rather than the catalog var. The
  catalog check is kept, but only proves the Worker deployed. Verified against
  the poisoned staging entry below: the gate now fails with
  `compiler stamped 0eec23f182a1 but this gate expects ccf67eac9661`, and passes
  clean as a production canary. `test/deployment.test.mts` locks both the
  assertion and its remediation message, and both tests fail against the old
  script. **The artifact-stamped revision is authoritative; the catalog var is
  not.** Still wait for the Container pool anyway -- a gate that fails late costs
  a full compile.
- **The staging R2 cache now holds a mis-keyed artifact**: the pre-fix firmware
  stored under a key that claims `ccf67eac9661`. A re-run of the gate at this
  exact revision will serve it from cache rather than recompiling. It is
  staging-only and any future revision keys differently, but that entry had to be
  purged before this revision could be gated again, because the corrected gate
  refuses to pass on it -- a cache hit truthfully reports it was built by
  `0eec23f182a1`. PURGED 2026-08-22 (`wrangler r2 object delete
  plaits-lab-firmwares-preview/firmware/<buildKey>.wav --remote`, staging bucket
  only; production lives in a different bucket). Deleting the firmware object is
  enough -- the cache branch keys off the R2 `head`, so the rebuild path then
  overwrites the stale job state and the misleading `manifests/<buildKey>.json`.
  Staging re-gated GREEN afterwards, recompiling to
  `dcae8300a81f0d35612f3ade3779113848fe512c62829758ebfce74dd708d872` -- byte-identical
  to production, at 58,148/20,668 text/BSS.

The hardware audition for this rollout was deferred rather than skipped: the fix
shipped on a listening-only basis while the module was unavailable. It PASSED on
physical hardware on 2026-08-31, confirming the octave-switching root behaves as
intended. The audition artifact is
`dcae8300a81f0d35612f3ade3779113848fe512c62829758ebfce74dd708d872`; after the
purge and re-gate the staging and production artifacts are the same bytes, so
either copy is the right one. (Before the purge only the production copy was,
which is why the saved gate artifact had to be replaced.)

The August 28 algorithm-32 Carrier Tilt rollout shipped at
`rev-3514880253d9`. Its autonomous 71-patch hardware diagnostic completed the
CPU, render-deadline, silence, and listening checks on a physical Plaits with
the animated-green pass verdict. Production Worker version
`5f940207-04d1-4235-afe3-0601935e8b26` then settled with both configured
instances healthy on the immutable image and no scheduling or starting
instances. Production canary build
`6e670c2b2c883a21accc268f5a12487c9a0f01cec9637750560ecb5753ea191f`
reported the compiler-stamped source revision `3514880253d9`; its
11,187,884-byte WAV has SHA-256
`2814f4d8e8556caa6938a5b3bf3cd1c520c816863183ab9f3dc55264201cccfc`,
and its generated field guide has SHA-256
`22a1425e6f7853dc314f96cfdb1a1094f4c66a6fa4eedc581f2d2fb6fd459442`.

Cloudflare's rate-limit binding allows five new compilation requests per source
IP per minute. Cache hits and repeated polls for an already queued build bypass
that limit. This is a lightweight abuse guard rather than an account or billing
system; IP addresses are not stored in Durable Objects or attached to firmware
artifacts.

Image builds run on GitHub Actions (`.github/workflows/plaits-builder-image.yml`,
manual dispatch with a revision). The local path still works and is documented
below, but it builds linux/amd64 under QEMU on an Apple Silicon Mac and then
pushes ~12 GB over a home connection; a runner is natively x86 and sits beside
the registry. The workflow needs a `CLOUDFLARE_REGISTRY_TOKEN` repository
secret, and it verifies that the checked-out commit matches the requested
revision before building — a tag that disagrees with the source inside it is
the failure the `development` sentinel exists to catch, caught earlier.

The production compiler image is
`plaits-lab-build-service-firmwarebuilder:rev-bdf148346885` (immutable
commit-derived tags replaced the date-based convention; the table below is the
full history — keep this line in step with its last row). After deploying a new
image, use `wrangler containers info <application-id>` and wait until
`configuration.image` matches the intended immutable tag, `active_rollout_id`
is null, `scheduling`, `starting`, `failed` and **`active`** are all 0, then
confirm with `GET /v1/health` before submitting the production canary.
`healthy >= 1` alone means nothing here: for a DO-attached application `healthy`
counts PREPARED instances, not serving ones, so the old image can still be
attached and answering. Do not infer from the deploy succeeding either —
`wrangler deploy` returning means the rollout STARTED. A first-time
staging application can temporarily return "no Container instance available"
while its image is starting; the bounded staging smoke retries that response.

Schema 28 is live. It retains schema 27's independent Trigger / Gate / Velocity
Trigger / Velocity Gate articulation and two eight-table chord banks. That release
adds the Acid community engine and retains schema 26's custom Wavetable banks,
schema 25's Natural Speech engine, schema 24's shared Wave Terrain banks and
native equations,
independent experimental Linear TZFM and Fast FM preferences, Sync In as an
opt-in preference, and custom Speech banks accepting
up to 32 words inside the 1,024-frame ceiling. It inherits schema 22's explicit
Sync capability, schema 21's experimental MODEL-input assignment, schema 20's
replaceable FM-bank preference, schema 19's
triggered and gated FREQUENCY contours, and schema 18's Stock, Drift,
and Step unpatched-attenuverter modes; schema 17's selectable stock LPC banks,
custom text/recording-derived Speech banks, source/engine previews; and the
earlier recipe-driven scale banks and automatic LEVEL routing. The generalized
schema-inheritance hardening from `5b2b077` is also live: current production
source `bdf148346885` descends from that commit, so future supported schemas
inherit older feature shapes without another version-list edit.

Schema 28 promotes the Wavetable resource into one shared Wave Tables library.
The ordered library can be consumed by Wavetable, Chords, Wave Paraphonic,
Wavetable Diatonic Chord, Wavetable Scale Stack, and compatible Wave Terrain
entries. Chords stores a 15-wave route through the library; the three
Braids-derived models share a 33-wave route with per-wave gain; Wavetable uses
the full ordered bank. Routes are compiled rather than interpreted at runtime:
factory cycles point directly at their retained source bank, while only the
distinct custom cycles selected by a route are emitted as integrated tables.
Removing a factory bank therefore removes it from every consumer and permits
the linker to reclaim that bank's independent 16,896-byte section. A retained
factory Wave Terrain is rejected if its source bank has been removed, so an
old terrain selection cannot silently relink waves the library no longer owns.
The schema also keeps legacy schema-26 Wavetable resources valid and migrates
older browser state to the stock 15- and 33-wave routes.

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
| August 12, 2026 (correct Z Filter MORPH shape metadata) | `bf3c078fdf07` | `rev-bf3c078fdf07` |
| August 12, 2026 (complete Braids control, trigger, and stereo metadata audit) | `29e3dba36351` | `rev-29e3dba36351` |
| August 14, 2026 (complete remaining-engine metadata audit; all 87 engines covered) | `19666470542c` | `rev-19666470542c` |
| August 16, 2026 (Analog Percussion community engine; all 88 engines covered) | `0eec23f182a1` | `rev-0eec23f182a1` |
| August 21, 2026 (octave-switching root fix) | `ccf67eac9661` | `rev-ccf67eac9661` |
| August 28, 2026 (schema 24 shared Wave Terrain bank and native equations, manual contract 19) | `e81f62517fc4` | `rev-e81f62517fc4` |
| August 28, 2026 (algorithm-32 Carrier Tilt) | `3514880253d9` | `rev-3514880253d9` |
| August 30, 2026 (schema 25 Natural Speech engine and its word-bank encoder) | `fc6daf6616da` | `rev-fc6daf6616da` |
| August 31, 2026 (schema 26 shared custom Wavetable banks, manual contract 20) | `1f3441f4b15c` | `rev-1f3441f4b15c` |
| August 31, 2026 (Natural Speech recordings; /ping reports its revision) | `d857fbab280f` | `rev-d857fbab280f` |
| August 31, 2026 (BubbleTime, ZXPhase48k, and ZXPulse48k community engines; 92 engines) | `1cf8cd56ba6f` | `rev-1cf8cd56ba6f` |
| September 1, 2026 (invalidate fixed-window Natural Speech preview cache) | `633cc8cc9e1d` | `rev-633cc8cc9e1d` |
| September 1, 2026 (schema 27 four-way articulation, sixteen chord tables, Ro'Ved gestures, and Acid; 93 engines) | `8f97241069cc` | `rev-8f97241069cc` |
| September 1, 2026 (stereo Skins, Circuit Zaps, and Metalwork percussion engines; 96 engines) | `37c608fa2a69` | `rev-37c608fa2a69` |
| September 2, 2026 (schema 28 shared Wave Tables library and per-engine wave routes) | `bdf148346885` | `rev-bdf148346885` |
| September 2, 2026 (Acid native hard sync) | `06d11c08e05e` | `rev-06d11c08e05e` |

The schema-28 shared-wave production canary compiled fresh as build
`e114ffb89814dc91dd8faeff3b8eb46354130497f79ce9267f7e0b8b9185f94a`:
166,608 B text, 48 B data, and 22,608 B BSS. Its binary SHA-256 was
`ab733c4bb5d5215267d50cb73d8646597e7171c0ba468aef8641f10fdfb6642f`;
the 13,212,140-byte updater WAV SHA-256 was
`1b1a8c486d0886b17a51301b4d0fb43bf9a7538a926b2b8a22ed758c2574ed5a`.
The compiler stamped `bdf148346885`, the matching field guide completed, and
the dedicated Speech encoder was rotated to `speech-encoder-v19` only after
the production application reported the new image ready with no active,
starting, scheduling, or failed instances.

The Acid native-hard-sync production canary compiled fresh as build
`ad4d8558fde8c965dc4b0a9f51cf908af428e9be0fedc006092d450d62d5da71`:
228,548 B text, 80 B data, and 28,620 B BSS. Its binary SHA-256 was
`85f8839a565a35f885d97fe7ba661df2a8a1765ccf4b09b3fbcff62f5e129092`;
the 17,727,788-byte updater WAV SHA-256 was
`0d4105ff6cd86043d1e7b7b1c6916b064ea36771794313019018ef835fd2df91`.
The compiler stamped `06d11c08e05e` on a cache MISS, the field guide
completed, and the Speech encoder was rotated to `speech-encoder-v20` only
after the production application reported the new image with no active,
starting, scheduling, or failed instances.

Three things about this rollout are worth carrying forward:

- **A source change to a catalogued engine moves its digest, so it needs the
  allowlist regenerated exactly like a new engine does.** Giving Acid a
  native sync path changed `acid_engine.{h,cc}`, which moved
  `f3ef097de843 -> f80a79f57cbe`. The engine merge landed without that step;
  `sync_public_catalog.sh --check` named the drifting engine and the image
  had to be rebuilt from the regenerated commit. Read step 3 of the publish
  runbook as applying to any engine source edit, not just additions.
- **`wrangler.jsonc` carries the revision TWICE** — the container `image` tag
  and the Worker's own `PLAITS_SOURCE_REVISION` var. Bumping only the image
  gets as far as upload before `check_manual_contract.py` rejects it: "the
  Worker would record a revision the container never compiled."
- **Two deploys exited non-zero without meaning what the exit code implied.**
  Staging died on `code 100146` looking up the Worker version it had just
  uploaded, having left the containers untouched — a straight retry fixed it.
  Production died on `code 10013` from a queues call AFTER `SUCCESS Modified
  application`; the queues were verified intact (1 producer, 1 consumer) and
  no retry was needed, which mattered because retrying would have restarted
  an in-flight container rollout. Neither exit code was a reliable signal;
  `/v1/health` and `containers info` were. Note also that `containers info`
  reported `active: 1` after the pool had rolled while `containers instances`
  showed every instance inactive — prefer the instance list.

The website flash meter's Sync In rows are maintained by
`sync_flash_sweep.py` here (run `--all` inside the builder container to
re-derive the whole table; `sync_anchor_probe.py` re-measures just the
24-model reference palette the anchor is pinned to). Re-measuring them the
day after this rollout found two rows a month stale — `harmonic` 1,968 B high
and `swarm` 560 B high, against a base 1,088 B low — because the sweep as
originally written could not measure any engine the base palette already
carried: adding it produced a duplicate slot, which collapses, so the arms
were identical and the row read 0 with no error. The sweep now removes such an
engine instead. flash-budget.ts gained `syncInputAnchorRevision` so those rows
cannot silently outlive a rollout the way they just did.

A stock-24 palette carrying both Acid and Sync In no longer links at this
revision: it overflows FLASH by 64 bytes. That is a real ceiling users can
reach, and it is what the website flash meter's 816 B Acid sync row exists to
predict before the linker does.

The percussion production canary compiled fresh as build
`63f32b04116c478261f93720e3a85da7798580eed0378ee631a0b5dd28fd2bf5`:
181,952 B text, 48 B data, and 22,544 B BSS. Its binary SHA-256 was
`922ab41f6e55ba88c964b64add601962fea83d30bf310b72c7a4d79eb78fad5e`;
the 14,302,124-byte updater WAV SHA-256 was
`0e984ff32bbbc5c6c63cf29ce49b95786c230fe6176ceef45c666350509b43ec`.
The compiler stamped `37c608fa2a69`, the matching field guide completed, and
the dedicated Speech encoder was rotated to `speech-encoder-v18` only after
the production pool had settled on the immutable image.

The schema-27 production canary compiled fresh as build
`7dc3da2f77d7b8bfbe55e1884f6f7336c61902ac2e0167b26fe89da7442c0885`:
181,952 B text, 48 B data, and 22,544 B BSS. Its binary SHA-256 was
`922ab41f6e55ba88c964b64add601962fea83d30bf310b72c7a4d79eb78fad5e`;
the 14,302,124-byte updater WAV SHA-256 was
`0e984ff32bbbc5c6c63cf29ce49b95786c230fe6176ceef45c666350509b43ec`.
The compiler stamped `8f97241069cc`, and the matching field guide completed.

The August 16 production canary used the same exact 24-slot recipe as staging:
build `6cb08754024d2a7abf43ae0c6390eb23b11a051dbc04c4984a32212040a6e865`
compiled fresh in both environments to 203,924 B text, 48 B data, and 24,748 B
BSS. Its binary SHA-256 was
`0a88e0324c0f7c8efef17b9688cae0c53ed91fb0579a6e172a56bc6740b74b17`,
and the 15,859,244-byte updater WAV SHA-256 was
`a5a7bbeb2a2fbe05ee8d36e1bd1c7628163c55194ce672d1eb4cb0afde1e10c5`.
Contributor Dylan Bolink also auditioned the revised engine on physical Plaits
hardware and confirmed its sound and CPU behavior before publication.

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
