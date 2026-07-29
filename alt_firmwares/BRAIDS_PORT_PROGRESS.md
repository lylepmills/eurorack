# Braids → Plaits Palette port: implementation progress and corrections

Companion to `BRAIDS_PORT_SPEC.md`. The spec was written where the ARM
toolchain, Docker and qemu could not run, so every flash and CPU number in it
is an estimate and several claims are argued from source rather than measured.
This file records what the local session actually measured, and corrects the
spec where measurement disagreed with it. **Read this alongside §2 of the
spec; where the two conflict, this file is the one with numbers behind it.**

Branch: `claude/braids-engines-plaits-palette-je03ac` (both repos).

**THIS FILE IS THE SOURCE. Edit it here.** It is mirrored into
`rubato-audio:BRAIDS_PORT_PROGRESS.md` — the work spans both repos and either
side needs the whole picture — but that copy is now **generated**, opens with a
DO-NOT-EDIT banner, and is overwritten by:

```
rubato-audio$ shared/coord/sync-braids-progress.sh --repo <this-checkout>
rubato-audio$ shared/coord/sync-braids-progress.sh --check   # verify only
```

Run the sync in the same change that edits this file, so the two land together.

The mirror used to be hand-kept, and it drifted silently: on 2026-07-28 the two
copies had diverged 245 lines in OPPOSITE directions — one repo's session closed
open item 1 in its copy, another closed items 3/4 in the other, and each copy
still listed the other's as open. Neither was a superset, so **both actively
misled about what was still open**. Asking people to remember two copies did not
work; generating one from the other is the fix.

---

## 1. Status

| Engine | State | qemu CPU | A/B vs Braids |
|---|---|---:|---|
| `z-filter` | **landed** | 62% | all 4 models within 0.05 dB AC RMS, +5 cents |
| `toy` | **landed** | 35% / 48% | within 0.42 dB mean, ≤9 cents |
| `csaw` | **landed** | 12% / 13% | 0.00 dB mean at two settings, 0.75 dB at a third |
| `bowed` | **landed** | 37% / 38% | 3–5 dB; chaotic self-oscillator, see §3.10 |
| `ring-mod` | **landed** | 60% / 69% | within 0.04 dB energy-weighted at four detunings |
| `sub-oscillator` | **landed** | 28% | 0.23 / 0.42 dB against both source models |
| `digital-modulation` | **landed** | 20% / 18% | 0.00 dB at the stock frame; 0.09–0.45 dB across settings |
| `saw-comb` | **landed** | 32% | 0.19 / 0.45 / 0.67 dB |
| `vowel-fof` | **landed** | 71% / 73% | 1.96–2.29 dB |
| `raw-fm` | **landed** | 15% | 0.05 / 0.13 / 0.02 dB across all three source models |
| `triple` | **landed** | 52% | 0.24 / 0.25 / 0.03 / 0.02 dB across all four source models |
| `fluted` | **DROPPED** — §3.11 gate measured and failed, see §3.16 | — | — |

**ALL ELEVEN ENGINES ARE LANDED, and the twelfth is dropped.** The §3.11 gate
was run before any `fluted` code was written; it failed, and the spec's own
instruction was to drop rather than patch around it. So the port is **eleven
engines, final**. What remains is measurement and the website, not DSP.

**A `mono / stereo` CPU entry means a Pattern B engine** — one that renders a
different AUX in each aux mode, so it has two costs and the larger is its peak.
`toy` was always one; `bowed`, `csaw`, `ring-mod` and `vowel-fof` became ones
when open item 5 was resolved, and `digital-modulation` when its stereo render
was fixed (`BRAIDS_PORT_AUX_PROPOSAL.md` parts 6 and 7). Note it is the only
one whose stereo path is CHEAPER than its mono one. The
single-number engines render one pair for both modes. Every number here is
`estimate.py --sweep`; the stereo column needs `--stereo`, which did not exist
until 2026-07-28 — before that the table's figures were all mono.

Notes kept for reference:

- `vowel-fof` — the spec's headline finding is CONFIRMED in the source:
  `out += svf_bp[i] * amplitudes[0] >> 17` reads `amplitudes[0]`, not
  `amplitudes[i]`, so 100 of the 125 `formant_a_data` entries have been dead
  since 2013. It also ends in `size -= 2`, so it IS a 48 kHz algorithm and all
  three half-rate compensations the spec identifies are correctly droppable.
  Needs 5 SVFs, ~500 B of vendored tables (`formant_f_data` and
  `formant_a_data`, both [5][5][5] int16), an `Oscillator<SAW>` instantiation
  and a Pattern-B stereo landing — the largest engine left.
- `raw-fm` and `triple` are built and ready for the Q2/Q3 A/B.

### Website registration points — the handoff's list is about HALF of them

Every one of these was found by a FAILING TEST, not by the documentation. The
test suite is the real contract here; §9 of the handoff is not.

1. `catalog.generated.json` + `plaits-pins.json` — `sync-plaits-catalog.mjs`
2. `plaits-engine-sources.generated.json` — `gen-plaits-engine-sources.mjs`
   *(not in the handoff)*
3. `flash-budget.ts` — real measurements, now via
   `alt_firmwares/plaits_lab_builder/flash_sweep.py`
4. `previews.generated.json` + the mp3s — `render-previews.mjs`. *(not in the
   handoff)* It used to be two registration points: `render_previews.cc` kept
   its own HARDCODED engine list (includes, a mono `Emit<>` and a stereo
   `Emit<>` per engine), so a new engine needed a C++ edit and was silently
   skipped without one. That list is now generated — see open item 4 below.

No changes were needed to `engines.ts`, `PlaitsEditor.tsx` or
`plaits-palette.css`: with `origin: "Mutable Instruments"` and no `artwork`
field these fall through the existing fallbacks, and the Lab slot counter
keys off `origin === "Rubato Lab"` so they do not disturb it.

### Measured ARM flash — real, replacing every estimate

Re-swept 2026-07-28 against a local builder container built from the head AFTER
the `origin/master` merge, leave-one-out into Speech's slot in the stock-24
context. **Baseline 205,136 B — the same baseline Helix was measured against**,
so these sit on Helix's footing rather than the smaller pre-merge palette
(181,216) an earlier pass used. Controls reproduced the published table within
16 B (speech 23,296 vs 23,312, reed-pipe 2,000 exact, spectral-spiral 2,048 vs
2,064).

| engine | measured | spec estimate |
|---|---:|---:|
| raw-fm | 912 | 1,450 |
| digital-modulation | 1,216 | 1,620 |
| toy | 1,248 | 1,520 |
| csaw | 1,344 | 1,400 |
| ring-mod | 1,696 | 1,700 |
| z-filter | 1,712 | 2,200 |
| sub-oscillator | 2,224 | 1,300 |
| saw-comb | 2,512 | 3,000 |
| vowel-fof | 2,704 | 3,100 |
| bowed | 2,944 | 2,400 |
| triple | 3,424 | 2,800 |
| **total** | **21,936** | 22,490 |

The spec's AGGREGATE was within 3 %. Its PER-ENGINE numbers ranged −39 % to
+74 %, so treat §1 as a ranking and never as a budget.

**These moved TWICE during the branch**, so any earlier number is superseded.
The AUX rework re-costed `csaw` (1,392 → 1,344), `ring-mod` (1,872 → 1,680) and
`vowel-fof` (2,640 → 2,704) because it changed their DSP; the `origin/master`
merge then shifted eight of the eleven by ±16–32 B. **A DSP change re-costs the
engine, and so does merging a moved main** — re-sweep after either, not just
after adding an engine. `flash_sweep.py`'s controls now FAIL the run (exit 2) if
the base drifted, so a stale comparison stops being silent.

### Measured stereo delta — six Pattern-B ports, and `toy` costs NOTHING

Container built from the branch head. That matters for layering the numbers
onto the website: the head CONTAINS the deployed re-calibration revision
`94e84165`, so these sit on the same firmware lineage the flash meter is
calibrated against, plus the eleven ports.

`flash_sweep.py --stereo` differences two builds identical but for the recipe's
`stereoEngines` list (both `auxOutput: "stereo"`, so whatever the stereo aux
option itself costs cancels — and it costs nothing: the all-mono stereo arm
lands at 182,448, exactly the baseline 181,216 plus toy's 1,232 mono marginal).

| engine | stereo delta |
|---|---:|
| toy | **−144** |
| digital-modulation | 48 |
| bowed | 112 |
| csaw | 240 |
| ring-mod | 272 |
| vowel-fof | 496 |

`toy` shipped as the only Pattern-B port; `bowed`, `csaw`, `ring-mod` and
`vowel-fof` joined it in `c84a06a`, which gave each a distinct mono AUX voice
and therefore a second render path worth gating, and `digital-modulation` when
its stereo render was fixed. All six are in the builder's `STEREO_MACROS` and in
the website's `stereoToggleableEngineIds`; the remaining five are Pattern A and
want no entry.

**These move under you.** The set went 1 → 5 → 6 in a single day, and `csaw`'s
delta moved 208 → 240 between two sweeps hours apart, so re-sweep at the head
you are actually shipping rather than trusting a number from earlier in the
branch. `plaitsStereoControls.test.ts` now asserts every engine with a build
macro HAS an `engineStereoBytes` entry, which turns "a new Pattern-B engine
landed uncosted" from something you have to notice into a failing test.

**⚠ The measurement is silently meaningless if the engine is not IN the
palette.** Enabling `PLAITS_STEREO_<X>` for an engine no slot references links
no object, so the delta is exactly 0 — indistinguishable from a real "costs
nothing" result. The first run of this sweep pinned one engine in Speech's slot
and swept `stereoEngines` against it, and duly reported a confident 0 for all
four of the engines above, none of which were in the build. The controls did not
catch it, because `harmonic` and `glisson` happen to be in the base palette.
`flash_sweep.py` now builds a per-engine mono/stereo pair with the target in the
slot; a shared baseline is not worth that failure mode.

`toy` measures **−144 bytes** — compiling the stereo path makes the firmware
SMALLER. That is not a mismeasurement:

- Object-level diff moves exactly ONE object, `toy_engine.o` (1,200 → 1,052),
  and within it exactly one symbol, `ToyEngine::Render`. That is precisely the
  object the makefile's `-DPLAITS_STEREO_TOY=0` rule targets, so the isolation
  is clean and the cause is gcc 4.8 codegen: the runtime `parameters.stereo`
  branch compiles tighter than the specialised mono-only body it replaces.
- It is palette-INDEPENDENT — `toy_engine.cc` reads no `engine_config.h`, and
  the delta is −144 on both a 3-engine palette and stock-24.

Controls validate the harness: `harmonic` and `glisson` re-measure at 2,576 and
416, within 16 B of the website's published 2,560 and 432 — harmonic +16,
glisson −16, i.e. inside the ±32 B quantization the rev-94e84165 pass already
documents for this table.

**Correction, worth keeping as a warning.** This was first written up as "both
exactly 16 B low, a consistent revision offset" — because the controls were
compared against the copy of `flash-budget.ts` on THIS BRANCH, which had gone
stale: it still carried the 8b3e1cb table (harmonic 2,592) while `origin/main`
had been re-calibrated to 94e84165 (harmonic 2,560). Against the stale table the
two errors line up at −16 and look like a systematic offset; against the real
one they are +16/−16 and are just quantization. When a control seems to reveal a
neat systematic pattern, check that the published numbers you are differencing
against are the CURRENT ones first.

The website records this as `0`, not `−144`: `engineStereoBytes`' contract is a
non-negative marginal (its test asserts it), and banking a 144 B saving — well
inside the model's own ~350 B residuals — would only make the meter read
permissively. The entry exists so the measurement is on record rather than the
engine looking merely overlooked.

**Three traps when re-running the sweep.** The builder image bakes the firmware
source in (`COPY . /workspace`), so `docker build` it AFTER the engines land
or every new engine fails with a missing header while the baseline builds
fine — a local rehearsal of the exact deploy-ordering hazard. The image
has an ENTRYPOINT, so the sweep needs `--entrypoint python3` or the command
becomes arguments to the HTTP server and sits idle forever. And a FRESH
worktree has no submodule content — `git submodule update --init stmlib
stm_audio_bootloader` before the `docker build`, or it stops at the
missing-submodule guard.

**Open work, in the order it should be picked up:**

1. ~~**Stereo preview control is gated on the wrong condition**~~ — **DONE.**
   `PlaitsEditor`'s `stereoButtonFor` now tests `stereoToggleableEngineIds`
   FIRST (the build toggle, which edits the firmware recipe and charges flash)
   and falls through to a derived `previewOnlyStereoEngine` — "no build macro,
   but a stereo clip exists", i.e. `hasStereoPreview()`. The same predicate
   replaced the hardcoded set in `engineStereoOn` (which picks the clip) and in
   the localStorage restore filter, so a toggled chip now survives a refresh.
   Verified in the running site: **50/50 models have a stereo control, up from
   40/50** — 35 build toggles (unchanged) + 15 preview-only. `z-filter` and
   `pulsar` both stream their `-stereo.mp3`; `virtual-analog` still charges its
   flash delta and writes `stereoEngines`, and no preview-only toggle leaks
   into the recipe. Regression guard: `website/src/lib/plaitsStereoControls.test.ts`
   (confirmed failing against the old gate before the fix).

   **Correction to this item as written:** the claim that it "also affects five
   STOCK engines — pulsar, attractor, spectral-spiral, phase-distortion,
   string-machine" was already stale. Commit `9e11adad` ("stereo previews +
   toggle for the always-stereo engines") added `alwaysStereoEngineIds` and gave
   exactly those five a working preview-only button; they were verified rendering
   one *before* this change. The engines actually stranded were the ten Pattern A
   Braids ports and only those — they were in neither set. `alwaysStereoEngineIds`
   survives, but only as the seed for the stock-derived Stereo Dreams preset; it
   no longer gates any button, so a future always-stereo engine needs no entry.

2. ~~**Braids 14-segment icons.**~~ **DONE.** A merged engine cycles its source
   codes EVERYWHERE its icon appears — placed slots and the library rail alike.
   The two-renderer split the brief called for was built and then dropped:
   holding the library static was justified by "no slot state to read", but the
   cycle carries its own message anywhere it runs (this slot is several Braids
   models at once) and says it more plainly than a wildcard. The static wildcard
   (`Z**F`, `**FM`, `SUB*`, `**X3`) survives as the **reduced-motion** frame.
   `website/src/lib/braidsDisplay.ts` (font, geometry and frame selection, 15
   tests) and `components/plaits-palette/BraidsDisplay.tsx`.
   Four findings worth keeping:
   - **Nothing needed hand-mapping.** `chr_characters[]` in `braids/resources.cc`
     is a direct byte→16-bit segment-word table — the one `Display::Refresh`
     indexes by raw character byte and shifts to the driver — so the custom
     0x88 / 0x8C / 0x8E glyphs come from the firmware like every letter does.
     The bit→segment mapping was solved from the table itself ('I' = A D J M,
     'X' = the four diagonals, '-' = G1 G2), and bits 0–1 are unused across all
     256 entries. Decoded, the glyphs are waveform traces: 0x88 saw (B C K N),
     0x8C square top (A B C E F), 0x8E comb spike (D J M).
   - **The geometry is measured, not styled.** The display is two Kingbright
     **PDC54-11GWA** modules (named in `braids/hardware_design/Braids.xlsx`), and
     its package drawing gives character box 7.97 × 13.8 mm, segment width
     1.0 mm, digit pitch 12.7 mm, and slant **5°**. The first pass eyeballed the
     slant at 10° and read obviously wrong; the datasheet settled it. A test
     pins all five numbers so a "looks better" edit fails. The one number
     deliberately NOT the datasheet's is the drawn stroke: 1.0 mm is the die,
     and a lit segment blooms through the white diffused lens, so it is drawn at
     1.45× or the code goes thin at the 30 px slot size — the test asserts that
     relationship rather than the raw width.
   - **The mapping is twenty models, not nineteen** — `saw-comb` is a seventh
     1:1 engine, not only the glyph case. Cross-checked against the `upstream`
     field of all eleven `plaits-engine.json` files.
   - **The display is GREEN, and the first pass drew it red.** Kingbright's
     `-GWA` suffix on that part is the green GaP die behind a white diffused
     lens (565 nm peak, 568 nm dominant). Both visual errors this round — a
     slant at twice the real angle, and an entirely wrong colour — were answered
     on pages 1–2 of a datasheet *already open* for the geometry. Lesson kept in
     the `feedback_read_the_datasheet` memory note.

   Blurb ancestry is done too, taken from the engine headers. One correction:
   `vowel-fof` and `speech` share the five-vowel × five-register **grid**, not
   the table — `vowel_fof_data.cc` deliberately vendors its own copy because
   speech's uint8 quantisation is ~half a semitone coarse against a Q = 64
   filter. The copy says grid. `triple` is worded as overlap, not lineage.

   ⚠️ **Deploy coupling:** a description is part of the hashed engine record, so
   `raw-fm`, `triple` and `vowel-fof` moved digests. `public_catalog.json` was
   regenerated in the same commit; verified blast radius is exactly those three
   (47 of 50 byte-identical), so no already-deployed engine is affected — but
   the builder image and this snapshot still ship together, per item 7.

   The wildcard's one legibility wrinkle is now mostly moot: on a 14-segment
   display `*` (H J K L M N) sits close to `X` (H K L N), so `**X3` read a
   little like "XXX3". Since the wildcard only shows under reduced motion it is
   no longer what most readers see — revisit only if that frame becomes
   prominent again.

3. ~~**`engineStereoBytes` has no entry for `toy`**~~ — **DONE 2026-07-28.**
   Swept with `flash_sweep.py --stereo`; toy's delta is −144 B (its stereo path
   compiles SMALLER), recorded on the website as `0`. See the measured-stereo
   section above for the object-level proof, the controls, and the
   not-in-the-palette trap that made a first pass report a false 0.

   Recorded as `0`, not `−144` (`c1c6d975`), because the table's contract is a
   non-negative marginal (`plaitsFlashBudget.test.ts` asserts it), and banking a
   saving that sits inside the model's own residuals would only make the meter
   read permissively. **The premise this item was written on was backwards** —
   a missing entry already meant 0, and the true value is negative, so the meter
   was never under-reading a toy-stereo palette.

   **It widened twice while being fixed.** `toy` was the only Pattern-B port
   when this item was written; `c84a06a` added `bowed`, `csaw`, `ring-mod` and
   `vowel-fof`, and the `digital-modulation` stereo fix added a sixth. Those
   five DO cost flash (48–496 B) and were genuinely uncosted. All six are now measured, in
   `engineStereoBytes` and in `stereoToggleableEngineIds`; the mono marginals
   the AUX rework moved are re-swept too, since a DSP change re-costs an engine.

   This closes the FLASH question only; the Pattern-A ports' stereo BUTTON
   was item 1, and is also now done — they get one from `previewOnlyStereoEngine`,
   and per item 1 they must NOT be added to `alwaysStereoEngineIds`, which no
   longer gates any button.

   The same sweep also found this branch's `flash-budget.ts` had gone stale
   against `origin/main`'s 94e84165 re-calibration — see item 8.

4. ~~**`render_previews.cc` should read the catalog**~~ **DONE.**
   `render-previews.mjs` reads the catalog plus
   `plaits-engine-sources.generated.json` and writes
   `preview_engine_list.generated.h` into the build dir (picked up with `-I`);
   the `.cc` expands one `PREVIEW_ENGINE_LIST(X)` macro, and emits a stereo
   clip iff the constructed engine reports `stereo_capable()`, so the separate
   stereo list is gone. 50 engines, 100 clips, manifest unchanged.

   **It surfaced a latent bug, since fixed.** A clip was not a pure function of
   its engine: renders changed content when the emission order changed, because
   engines read state they never wrote. Verified in source: `FMEngine::Init`
   never touches `sub_fir_` / `carrier_fir_`, which the downsamplers read on the
   first block; `SixOpEngine::Init` and `ChiptuneEngine::Init` take scratch out
   of the shared 16 KB `g_ram_block` arena and do not zero it. Every render
   reused the same stack frame and the same arena, so each picked up what the
   previous one left behind.

   `RenderAudition` now resets all three carriers before each render: reseed the
   PRNG (it already did), `memset` the arena, and placement-new the engine into
   zeroed storage — placement-new because a `memset` over a CONSTRUCTED object
   would clobber its vptr. The per-block `out`/`aux` scratch is hoisted and
   zeroed once for the same reason. **Verified by rendering the catalog forwards
   and fully reversed: 100/100 WAVs identical.** Re-rendering moved 8 of the 100
   clips: `two-op-fm` and the DX7 banks differ only in a segment-0 startup
   transient (−38 to −69 dB relative, later segments bit-identical), and
   `chiptune` picks a different arpeggio through segment 1 at the same level and
   envelope — deterministic now, rather than a readout of arena leftovers.

   **This one took the slow route, and the lesson generalizes.** The fix already
   existed in `rubato-audio`'s `plugins/palette`: its golden-parity harness
   zeroed arena and engine storage from the start, and its CLAUDE.md carried a
   "worth reporting upstream — the website renderer would be more robust doing
   the same" note that sat unactioned. One product solving what a sibling is
   still living with is this repo's standing failure mode. Palette's suite is
   green against the fixed renderer (6/6, `engine_parity` included), and the one
   remaining divergence — the reference leaves its per-block `out`/`aux`
   uninitialized — was measured inert: poisoning both with `0x7F7F7F7F` before
   every `Render` changed 0 of 100 clips, so no engine leaves a block sample
   unwritten.

5. ~~**AUX designs are all stereo-split shaped.**~~ — **RESOLVED 2026-07-28.**
   Design + costing in `BRAIDS_PORT_AUX_PROPOSAL.md`; the four recommended
   changes are landed. The crux — a genuinely different AUX voice makes a poor
   right channel — is real, and the firmware already settles it: twelve stock
   engines render the two aux modes SEPARATELY off `parameters.stereo` and drop
   the mono AUX voice in stereo. Ten of the eleven ports collapsed both modes
   into one render, which no stock engine does. Six were already at or above the
   real stock bar and are untouched; `z-filter` is a genuine no (OUT is a
   crossfade of its only two constituents, so any AUX built from one collides
   with it somewhere in MORPH). Now Pattern B: **`bowed`** mono AUX is the bow
   exciter, **`csaw`** a variable-width pulse off the same transitions,
   **`ring-mod`** the bare first modulator (mono 69% → 60%, divides 4 → 2),
   **`vowel-fof`** the glottal source. Stereo renders are bit-identical to
   before on all four.

   Two defects found while writing that proposal are fixed alongside it (part 7).
   **`digital-modulation`'s stereo render never matched the spec**: the spec
   specifies an I/Q split, the code shipped the DC-blocked symbol staircase on
   AUX in both modes, and the header comment kept the spec's language — so it
   read as doc drift when the render was what was missing. Now Pattern B, and
   L + R reproduces the mono OUT to float epsilon. **`csaw`'s stereo pair
   collapsed to mono at HARMONICS noon** (measured bit-identical); no depth
   remapping can fix that — a continuous self-map of the depth range always has
   a fixed point — so the channels are separated on a second axis, a constant
   bend offset, which `BendSegment` being affine in bend makes provably
   collapse-free everywhere.

   Still open: flash unmeasured for the six Pattern B ports
   (`flash_sweep.py --stereo`), and a listening test on `vowel-fof`'s source.

6. ~~**`fluted` is still gated**~~ **DONE — measured, failed, dropped.** See
   §3.16. No `fluted` code was ever written. Nothing downstream needs doing:
   the engine never entered the catalog, so there is no registration, preview,
   flash or builder entry to unwind.

7. **Deploy** — builder image from the branch head FIRST, then the site. Engine
   digests are the builder's allowlist. The site already surfaces "Builder
   update required" on its own when they disagree.

8. ~~**Merge `origin/main` into this branch before deploying**~~ — **DONE
   2026-07-28**, both repos. The branch was long-lived and main had moved under
   it: it still carried the pre-`94e84165` flash table, so merging would have
   silently REVERTED the re-calibration — including `flashSafetyMarginBytes`
   192 → 512, which would have made the buildable DEFAULT palette read "over"
   (the margin must stay under stock-24's headroom, now 272 B, was 688). **The
   branch could not self-diagnose it: its own suite passed throughout**, because
   its catalog snapshot was also pre-`helix`.

   Order matters and is cross-repo: **eurorack first, then the website**, because
   the site's catalog snapshot is GENERATED from the firmware repo. Merging
   `origin/master` into the eurorack branch gave a catalog holding both `helix`
   (from master) and the eleven ports; only then can the website regenerate a
   51-engine snapshot. Regenerating from the un-merged eurorack branch would have
   silently DROPPED `helix`.

   How the conflicts resolved, for the next time:
   - `catalog.json` / `public_catalog.json` / `previews.generated.json` —
     conflicts were pure FORMATTING (master reformatted to expanded JSON) plus
     disjoint additions. No shared engine differed. Rebuilt as a programmatic
     union rather than hand-merged.
   - `catalog.generated.json` / `plaits-pins.json` /
     `plaits-engine-sources.generated.json` — REGENERATED from the merged
     eurorack head (`sync-plaits-catalog.mjs`, `gen-plaits-engine-sources.mjs`),
     never hand-resolved. 51 engines, 51 measured, no gaps either way.
   - `render_previews.cc` — took the branch's generated `PREVIEW_ENGINE_LIST`
     (item 4) over master's hardcoded list; being catalog-driven it picks up
     `helix` on its own.
   - `test_generate_engine_config.py` — its hardcoded `len(CATALOG)` is 51 now.
     Note master's own value was left at 39 against a 40-engine catalog when
     `helix` landed, so that assertion was ALREADY failing on master; this fixes
     it in passing.

   Verified after: merged firmware still builds in the container and reproduces
   the same flash numbers; 274 website tests pass.

   **Two follow-ups this leaves open.** (a) `previews.generated.json` is a UNION
   of two renders and its `sourceRevision` says so verbatim — a re-render from
   the merged head collapses it back to single-revision provenance. (b) The
   local `npm test` appeared unrunnable — `devEngines` is `onFail: error` at
   node 24.15.0 / npm 11.12.1 and the shell was on 24.18.0 — so the suites were
   run directly with `node --test`. **That diagnosis was wrong**: it is a PATH
   problem, not a pin problem. `nvm use` in `website/` reads `.nvmrc` and gives
   node 24.15.0 AND npm 11.12.1, satisfying both pins exactly; v24.15.0 is
   installed for precisely this. `npm test` runs: 274 passing. The pins are a
   deliberate deploy-parity contract and are unchanged. The trap for agent
   sessions is that shell state does not survive between tool calls, so the
   activation has to be in the same command — see `website/CLAUDE.md`.


---

## 2. Attribution — Q1 RESOLVED

Lyle's call: over-index on crediting Mutable, keep dual copyright in the
licence. The precedent that settles it is already in the tree — the **stock
Plaits engines gained a fourth `ApplyMacro` macro** they were never designed
with (`grain_engine.cc`, `fm_engine.cc`, and eight more) **and kept
`origin: "Mutable Instruments"` / `author: "Emilie Gillet"`.** The Braids ports
are the same situation: Émilie's algorithm plus a Rubato fourth macro. So they
are treated exactly like Plaits rather than differently from it.

Uniform across all eleven:

```
origin:    "Mutable Instruments"
author:    "Emilie Gillet"
packageId: "mutable-instruments/<id>"
tags[0]:   "braids"          (gives the website a badge hook)
license:   MIT, carrying BOTH copyright lines in LICENSE and in every
           source file, plus the SPDX tag.
```

This is safe on the website: with no `artwork` field an engine falls back to a
colour swatch, exactly like the 15 Lab engines, and gets the neutral `#8B7B6E`
rather than either the coral or an MI bank colour — so the ports read as their
own group without a new `origin` value. `engines.ts:94` also gives them a
5-character slot label from the engine name, which is coincidentally the Braids
display convention. **No website change is required for attribution.**

Other answers: Q4 catalog-only, no preset change. Q2/Q3 build both, Lyle A/Bs
after. Q5 deferred to a listening test — `bowed` should ship the 1024-tap
bridge line and state the 17.2 Hz fold, and NOT spend 4–8 KB of arena yet.

---

## 3. Corrections to the spec, found by measurement

### 3.1 `wav_sine` and `lut_sine` do not share a phase origin — AFFECTS EVERY ENGINE

**The single most important finding, and the spec does not mention it at all.**

- Braids `wav_sine[0] = -32512`, `[64] = 126`, `[128] = 32766` → it is
  **−cos(2πx)**.
- Plaits `lut_sine[0] = 0.0` and rises → it is **sin(2πx)**.

A Braids phase read straight through `Sine()` therefore comes out a **quarter
period early**. It sounds plausible in isolation and is completely wrong
against hardware — the z-filter A/B showed matching RMS with a spectral
envelope 17 dB out until this was fixed.

```cpp
// sin(2*pi*(x - 0.25)) == Sine(x + 0.75); InterpolateWrap folds it back.
inline float BraidsSine(float phase) { return Sine(phase + 0.75f); }
```

Every remaining engine that reads `wav_sine` must do this: `ring-mod`
(three sine reads), `raw-fm`, `vowel-fof`, `digital-modulation`, `triple`'s
sine voice. Do not assume `Sine()` is the drop-in.

### 3.2 `RenderDigitalFilter` is a 96 kHz algorithm, not 48 kHz

Spec §2 R5's table lists it as `size -= 2` = **yes**, inner rate 48 kHz. It is
not: `size -= 2` occurs at lines 709, 980, 1185, 1285, 1823, 1937, 2163, 2362
and 2447, and `RenderDigitalFilter` spans 328–408. It has none.

The consequence is benign — §3.1's own implementation guidance already treats
it as 96 kHz (the `× 0.5f` carrier increment), so the port is rate-matched and
Braids' constants transfer verbatim. But **do not trust the R5 table as a
lookup**; re-grep per engine, which is what R5 itself demands.

### 3.3 Braids' 15-bit parameters

`parameter_[2]` is `int16_t` carrying **0…32767**, not 0…65535. Several
derivations only work at 15 bits — notably z-filter's `balance`, where
`(p1 < 16384 ? p1 : ~p1) << 2` is a clean triangle peaking at the knob centre
at 15 bits, and a four-tooth sawtooth at 16. The spec's float equivalent
`1 - |2*morph - 1|` is right, but only for the 15-bit reading.

### 3.4 `toy`'s TIMBRE is decimation, not bit depth

Spec §3.3's table says "TIMBRE | Crush | bit-depth reduction". The source sets
`decimation_count = 512 - (parameter_[0] >> 6)` — the **sample-and-hold rate**.
The held sample is a `uint8` at every setting; there is no bit-depth control.

### 3.5 Initial latch states come from a `memset`

`DigitalOscillator::Init()` does `memset(&state_, 0, sizeof(state_))`, so every
latch (z-filter's `polarity`, and the equivalents elsewhere) starts **LOW**.
Only the sync/trigger handler raises it. Getting this backwards inverts the
output and is easy to miss because the level and spectrum stay plausible.

### 3.6 CPU: a second render path is not affordable

Spec §3.1 specifies AUX as the complementary filter model. Rendering it from
its own resonator pair measured at **94% of the CPU budget** under
`qemu/estimate.py --sweep` — not shippable. The four models differ in two ways,
their output combination and their reset phases, and only the combination is
free once the resonators have advanced. Sharing OUT's phases costs ~5
operations and lands at **62%**; OUT, the model the user selected, is
unchanged. Declared in the header.

**General rule for the rest of the port: assume a full second render path for
AUX will not fit, and check with `--sweep` before designing one in.** The
in-tree idiom is that AUX is a byproduct of the same computation
(`reed_pipe`'s reed flow, `csaw`'s mirrored notch depth), not a second voice.
`csaw` is the cheap case worth copying: OUT and AUX share the phase, the
transition times and the BLEP values, and differ only in two step magnitudes.

### 3.7 Two more Braids fixed-point behaviours that are audible

Both found on `ring-mod`, both likely to recur:

- **A phase offset formed from a zeroed stored phase.** `RenderTripleRingMod`
  builds its carrier phase as `phase_ + (1 << 30)` on entry and unwinds it on
  exit, so from a zeroed state the carrier starts a quarter cycle AHEAD of the
  modulators. It is invisible everywhere except the one setting where the
  detunes meet and all three oscillators would otherwise collapse into one.
- **Knob quantization that never reaches zero.** Detune is
  `(parameter_ - 16384) >> 2` in 1/128-semitone units — an arithmetic shift,
  so a floor. At the knob centre it lands on −1, not 0, so the hardware beats
  very slowly at "unison" where a smooth float detune phase-locks into a
  static waveform. Reproduce the quantization; it is the sound.

### 3.8 Braids' knob and table QUANTIZATION is repeatedly the sound

Three times now, reproducing an integer step that looked like a rounding
detail was what closed an A/B gap. Assume quantization is audible until shown
otherwise, especially anywhere positional:

- `ring-mod` detune, `(parameter_ - 16384) >> 2` — never reaches zero, so the
  hardware beats at "unison" where float phase-locks.
- `bowed` bow position, `6 + (COLOR >> 9)` over 256 — bow position sets a comb
  null, so a 2 % error moves the null a whole harmonic.
- `bowed` friction, read at an **integer** index with the interpolating call
  commented out — a 256-step staircase inside the stick-slip loop, which is
  where the slip happens.

### 3.9 The in-tree control-response test is a real gate, not a formality

`ValidateExperimentalControlResponse` renders **1,024 blocks — about half a
second** — and fails a control whose effect it cannot see in that window. It
caught `digital-modulation`'s payload knob doing literally nothing: at a few
tens of Hz of symbol rate half a second is ~13 symbols, and a linear frame law
left the packet header 33 symbols long at noon.

That is the SAME defect the spec moved frame length off MACRO to avoid — it
had simply relocated to the other knob rather than being fixed. Reachability
has two axes, the default knob position AND elapsed time, and the spec only
reasoned about the first. An exponential frame law fixed it; HARMONICS = 1 is
still Braids' 1,088 symbols exactly.

**Any engine with a slow internal sequence needs its control law checked
against that half-second window**, not just against its endpoints.

### 3.10 Check the spec's DSP against the source before implementing it

`sub-oscillator` is the clearest case. The spec designs it around a twin-ramp
formulation (`out = 2p - pw - sq`, a `mu` control, per-sample
`c = 0.5*mu*(1-pw)`) and spends a section resolving a pw-rate contradiction
inside it. `MacroOscillator::RenderSub` does none of that — it is two
AnalogOscillators and a `Mix`. Following the source and reusing the in-tree
`VariableShapeOscillator` made the whole contradiction moot.

The spec also mislocates MORPH's null: it says AUX is "silent at MORPH 0",
but Braids' sub level is a **V** with its zero at the CENTRE — loudest at both
ends, and never above an equal blend.

Two implementation traps worth carrying forward:

- **`VariableShapeOscillator`'s `waveshape` is TRIANGLE at 0, saw at 0.5,
  square at 1.** Mapping a control across the full 0–1 range when you wanted
  square→saw runs off into a triangle and reads as the port going dark.
- **A narrow pulse carries a large DC term by construction**, and the SDK's
  audio-health gate rejects it (>0.2). Braids leaves it and the spec chose to
  document it, but it also thumps the LPG. A blocker an order of magnitude
  below the engine's lowest note removes it without shaping the pulse — and
  then the gains must go negative (R1).

### 3.11 Some engines cannot be matched spectrally, and that is not a defect

`bowed` is a nonlinear self-oscillator. The port's standard ~8-cent
kCorrectedSampleRate offset is half a percent of a 434-sample loop at MIDI 45,
which is enough to settle the stick-slip system into a **different limit
cycle**. Third-octave spectra sit 3–5 dB apart with every coefficient
agreeing. Pitch, level and gross tilt track; bin-level agreement is not a
meaningful target. `fluted` is likely the same class. **`saw-comb` turned out NOT to be** — it
matched to 0.19–0.67 dB, because its loop is linear with a clip rather than a
chaotic stick-slip system, so it does not wander into a different limit cycle.
The distinction is chaotic-vs-linear feedback, not feedback-vs-not. The oscillator engines (`z-filter`, `csaw`,
`ring-mod`, `toy`) genuinely do match to a fraction of a dB, so the contrast
is informative rather than an excuse.

### 3.12 `Sine()` reads BELOW the table for a negative phase

`Sine()` is `InterpolateWrap`, whose `index -= (int32_t)index` truncates
TOWARD ZERO — so it wraps positive arguments and indexes **before** `lut_sine`
for negative ones. Any engine doing phase modulation can reach a negative
argument: `raw-fm` hits −0.25 (carrier phase 0, plus the wav_sine 0.75 offset,
minus a full cycle of deviation) and the in-tree audition caught it as a 6e12
sample. Add a whole-cycle positive offset to every phase handed to `Sine`;
`InterpolateWrap` discards the extra cycles, so it is free.

Related: Braids accumulates phase in a **uint32**, which wraps at any
magnitude. A float phase with a single-subtract wrap does not, so any feedback
path that can drive an increment past 1.0 needs that increment clamped.

### 3.13 qemu undercounts divides

`qemu/estimate.py` models `cycles = A*instructions + B*flash_reads` with
`COST_INSN = 1.0`. A VDIV is one instruction and about fourteen cycles on an
M4, so a divide-heavy engine reads optimistic. `ring-mod` carries two divides
per sub-sample; treat its 69 % as a floor, not a number.


### 3.14 AUX is INVENTED here, not ported

Braids is mono. It has no AUX at all, so every second output in this port is a
design decision rather than something inherited — and calling these engines
"already stereo" is wrong in a way that misleads. What is true is narrower:
each engine's OUT and AUX were chosen to be a decorrelated PAIR (bridge vs neck
pickup, a filter model vs its complement, a notch vs its mirror), which is why
routing them L/R costs nothing extra. Pattern A vs Pattern B is a statement
about COST, not about inheritance. Note also that most Plaits engines give AUX
a genuinely different VOICE rather than a stereo partner — see open item 5.

### 3.15 A 15th registration step

The spec's 14-step per-engine checklist misses
`alt_firmwares/plaits_lab_builder/test_generate_engine_config.py`, which pins
`len(CATALOG)`. It has to move with every engine or the builder suite fails.

### 3.16 `fluted`: the §3.11 gate was measured and FAILED — engine dropped

Run 2026-07-28, before any engine code. Instrument and every script:
`experimental/fluted_gate/`. The instrument was validated first (`validate.py`)
by rendering Braids' own FLUT and reproducing the known chaotic detuning across
COLOR (measured 1.53 / 1.02 / 1.04 / 4.49 / 2.51 against the cited study's
1.50 / 3.47 / 1.01 / 2.44 / 2.48 — same scatter, same character).

**The claim under test:** *"below noon (2 harmonics) only the fundamental
survives and the pipe tunes reliably."* It is false, and it is not close.

**At the spec's own coefficients** (in-loop DC blocker = Braids' 0.99 @ 96 kHz
rate-corrected to 0.98 @ 48 kHz, which is fix #2's answer), MORPH below noon
tuned **0 of 25** HARMONICS × MACRO settings at *every* note from MIDI 36 to
60, and at most 5/25 above it. 2,000 renders; 13% in tune overall. The engine
either fails to oscillate at all — the low notes at MORPH ≈ 0, where the
reflection is darkest, which is the exact opposite of "only the fundamental
survives" — or it locks to 3× or 5× f0.

**It is a real mode hop, not a bright fundamental.** Where the peak sits at
3 × f0, the played note measures **41 to 55 dB below** the loudest partial and
the surviving partials are 3, 6, 9 × f0 — harmonics of 3f0, with nothing at f0
or 2f0. The period is genuinely wrong. (`period.py`; this is the check that
separates "in tune but bright" from "playing a different note", and it has to
be made explicitly — a dominant-partial number alone cannot tell them apart.)

**Best case available, and it still fails.** Moving the in-loop DC blocker to
0.999 (8 Hz) — a change beyond the spec's eight fixes — recovers a lot, and
one setting (HARMONICS 0.5, MACRO 0.75) is in tune 40/40 below noon. But that
is one point in the control space, not the control space:

- **HARMONICS transposes the engine.** At the best MACRO / DC blocker,
  HARMONICS 0.00–0.13 sits **+7 semitones**, 0.83–1.00 sits **+16 semitones**,
  and only 12–17 of 25 knob positions hold pitch, the ends failing identically
  at every note. It is deterministic, not noise-driven (three breath-noise
  seeds agree everywhere except one bistable point).
- **MORPH transposes it too**, which is the gate's actual question. Give the
  design *both* fixes — 8 Hz blocker *and* HARMONICS pre-restricted to its
  in-tune window — and 78–82% of (note × HARMONICS × MORPH) holds pitch, with
  the failures being **+22 and +26 semitone** jumps that MORPH triggers
  mid-travel, at a boundary that moves with the note. E.g. MIDI 60 at
  HARMONICS 0.20: in tune to MORPH 0.2, then +22 semitones from 0.3 up.

**Mechanism.** The bore-only loop *cannot* self-oscillate: the bore write takes
`reflection >> 1`, capping its round-trip gain at |H|/2 ≤ 0.5. So the
oscillation is set by the **jet** path, whose delay is 48/256…79/256 of the
total — and that fraction is HARMONICS. Sliding it slides the whole mode
family, so the played note is only reachable near the middle of the knob. This
is also why the cited study's scatter appeared **across the COLOR sweep**:
COLOR *is* the jet fraction. The detuning was never MORPH's doing.

**On the spec's topology objection: right verdict, wrong arithmetic.** §3.11
predicted the survivor would be a **sub-f0** mode near 0.31–0.36 f0, the third
member of the bore-only series. That series is not available at all (the ½
above), and every mode measured is **super-f0** — 1.5×, 2.5×, 3.5×, 4×. The
conclusion "MORPH does the opposite of what the design claims" stands; the
named mechanism does not.

Two by-products worth keeping:

- Braids' `lut_flute_body_filter` works out to a nearly constant **8.8–13.2
  harmonics of the note** across the keyboard (13.2 at MIDI 43 → 8.8 at MIDI
  79), so the port's "corner in harmonics" MORPH really was the right
  generalisation of it, and Braids' own fixed setting sits at MORPH ≈ 0.27 —
  *below noon*, in the region the claim says is reliable. Braids is famously
  not, and the measurement reproduces that.
- **`DigitalOscillatorShape` is NOT the `fn_table_` index.** `fn_table_` is
  indexed by `MacroOscillatorShape - MACRO_OSC_SHAPE_TRIPLE_RING_MOD`; the
  `DigitalOscillatorShape` enum is in a different order, so
  `set_shape(OSC_SHAPE_FLUTED)` silently renders **Snare**. It cost an hour
  here, presenting as "FLUT does not sustain" (one loud block, then decay to a
  DC value of 10). Anything driving `DigitalOscillator` directly is exposed;
  drive `MacroOscillator` with a `MACRO_OSC_SHAPE_*`.

---

## 4. The A/B harness

Lives in `/tmp/braids-ref/` (scratch, deliberately not committed — it links
Braids' MIT DSP and must not pull in `braids/test/braids_test.cc`, which is
GPL v3 while the DSP around it is MIT).

- `braids_ref.cc` — renders any Braids model at its native 96 kHz with linear
  parameter sweeps, then decimates to 48 kHz through the **same**
  `[0.25, 0.5, 0.25]` kernel the ports use, so the comparison is not
  confounded by a different anti-imaging filter. **For a `size -= 2` model,
  set `BRAIDS_REF_DROP=1` instead**: those write 96 kHz through a 2× linear
  interpolator, so the odd samples ARE the 48 kHz algorithm and taking them
  recovers Braids exactly. The default kernel composes with that interpolator
  into `(u[k] + u[k-1])/2` — a 2-tap average, −3.0 dB at 12 kHz — which
  silently darkens the REFERENCE and will send you hunting a filter bug in
  the port that is really in the harness.
- `ab.sh` — renders both sides at matched controls and compares. Braids'
  `parameter_[0]`/`[1]` map to different port knobs per engine, so they are
  passed explicitly (args 9 and 10) rather than assumed.
- `compare.py` — reports f0 by autocorrelation and a third-octave band
  envelope, **energy-weighted**. The weighting is not cosmetic: a sparse
  spectrum (ring-mod, and later digital-modulation / raw-fm / triple) shifted
  by the port's +5 cents puts a sideband in a neighbouring bin, which reads as
  −60 dB on a band holding 0.001 % of the signal. Unweighted, ring-mod scored
  13 dB "MISMATCH" while its centroid and RMS matched to three figures.
  **Do not use bin-by-bin spectral correlation** either: two renders a
  fraction of a hertz apart decorrelate completely while sounding identical,
  which reads as a catastrophic mismatch and sent the first z-filter A/B
  chasing a bug that was not there.

Two harness traps worth knowing: `render_model.cc` writes a **stereo** WAV
(OUT/AUX), and reading it as mono interleaves two signals into a convincing
octave-down artefact; and the fork's `braids/` is byte-identical to upstream,
so build everything from one include root — `/tmp/braids-upstream` also
contains a `plaits/`, which will shadow the fork's headers.

Rebuild:

```sh
REPO=~/rubato-worktrees/eurorack-braids-port
g++ -O2 -w -I$REPO /tmp/braids-ref/braids_ref.cc \
  $REPO/braids/{macro_oscillator,analog_oscillator,digital_oscillator,resources}.cc \
  $REPO/stmlib/utils/random.cc $REPO/stmlib/dsp/units.cc -o /tmp/braids-ref/braids_ref
```

---

## 5. Flash — still unmeasured, and how to measure it

`website/src/components/plaits-palette/flash-budget.ts` documents the method:
a **leave-one-out sweep against the LIVE builder**, replacing one slot of
stock-24 with a duplicate of an already-present engine and reading the
text/data delta. It must be done in the full stock context, and the deployed
builder must be built from the same revision as the catalog snapshot.

That is a deploy-gated batch job, so it is deliberately left until every
engine has landed rather than run per engine. `check --arm` (which does pass
for both landed engines) proves the ARM build, not the size.

---

## 6. Environment notes

- A git worktree does **not** inherit submodules. Run
  `git submodule update --init --recursive` inside the worktree or every
  compile-based SDK check dies on `stmlib/dsp/units.cc`.
- `qemu-system-arm` is present via Homebrew; `plaits-lab-builder:local` builds
  clean; `check --arm` and `qemu/estimate.py --sweep` both work locally.
- `compare.py` needs numpy — the system python3 has none. Use
  `~/Desktop/claude/rubato-audio/plugins/just_play/.venv-bundler/bin/python3`.

---

## 7. The Community three (2026-07-28) — landed, unmeasured

Not part of the twelve. These come from OTHER people's Braids firmwares and are
the first engines in the catalog with `origin: "Community"`, seeding that
category. Branch `claude/open-source-synth-models-hp0o6y`, off this one.

| id | upstream | author | models |
|---|---|---|---:|
| `bytebeat` | Bees-in-the-Trees (`timchurches/Mutated-Mutables`) | Tim Churches | 4 |
| `diatonic-chord` | Braids Renaissance (`boourns/eurorack-renaissance`) | Tom Burns | 5 |
| `scale-stack` | Braids Renaissance | Tom Burns | 5 |

Both upstreams are MIT. Bees carries Churches' copyright line beside Gillet's in
every file he touched; Renaissance's README licenses its STM32F code MIT, but
**its new files carry no per-file header** — worth a one-line confirmation from
Tom Burns before this ships publicly. He is reachable and commercial
(burns.ca).

**Renaissance's SAM speech models are deliberately not ported.** SAM is
proprietary to SoftVoice, Inc.; every circulating port descends from a
reverse-engineering whose own README says it "cannot be put under any specific
open source software license". Wrong risk for a firmware distributed under the
LLC with a checkout attached.

### What is verified, and what is not

Verified in the cloud container: `plaits_test` (audition render, extremes, and
the control-response gate on all four controls of all three engines);
`validate_catalog.py` ok at 53; `plaits_lab.py check --full` clean on all three,
including the licence check that LICENSE text and per-file SPDX tags agree;
`sync_public_catalog.sh` regenerated; builder generator suite 48 green; SDK
suite 57 green.

Host CPU, ratios only (the file header is right that absolute ns mean nothing):
`bytebeat` 0.29× triple, `diatonic-chord` 0.96×, `scale-stack` 0.97×. Stock
`chords` is 1.78× triple, `two-op-fm` 5.4×.

**Not verified — same gaps as §5, same reasons.** No ARM toolchain, no Docker,
no qemu, no hardware in the container: flash cost, `qemu/estimate.py --sweep`,
and the `build --hardware --cpu-probe` that publication requires. These three
belong in the same leave-one-out batch as the twelve.

### Website side is blocked on exactly that

`website/src/lib/plaitsFlashBudget.test.ts` asserts *every catalog engine has a
measured flash cost*, so syncing the catalog snapshot before the sweep reds
`npm test`. Do the sweep first, extend `engineFlashBytes`, then sync.

Good news found while checking: the Community path on the website is already
built, not a stub — the origin filter chip, the `Community` label mapping, the
solid identity chip for engines with no drawn symbol, and a dashed
`.flash-chip.is-approx` ring for unmeasured community engines all exist
already. `engines.ts` maps `origin === "Community"` to the `community` tone
without an edit. So registration should be the flash numbers plus a colour
choice, not new UI.

### Two upstream defect classes worth carrying forward

Both readmes have the full argument; the short version, because it generalises:

1. **Renaissance's chord offsets are consumed cumulatively but written as
   absolute scale degrees.** `RenderStack` pre-accumulates its own spans and
   then `renderChord` accumulates them again; every label in `diatonic_chords`
   only parses as absolute. The port does the intended thing. If anyone ever
   wants the shipped-Renaissance voicings, that is a *different engine*.
2. **Two divisions by zero and an out-of-bounds row read** across the two
   firmwares, all reachable from a knob. Assume a dormant alt firmware has not
   been fuzzed.

And one of ours, caught by the in-tree audition gate rather than by review:
`Sine()` is documented safe "for phase >= 0.0f", and a bipolar signal fed to it
indexes `lut_sine` out of bounds — a ~5.0 spike on OUT. Same defect the earlier
review found in `z-filter`. **Any new use of `Sine()` with a computed argument
needs a whole-period offset.**

---

## 8. Outside Mutable: the STK two (2026-07-28), and the one that was dropped

First engines from outside the Mutable world. STK
(`thestk/stk`, Perry Cook and Gary Scavone) is MIT — the variant adding a
non-binding request to send modifications upstream — and it is by a wide margin
the largest permissively-licensed body of instrument models anywhere: about
twenty-five physical models against a Braids fork scene that has been dormant
since 2020.

| id | upstream | fills |
|---|---|---|
| `shakers` | STK `Shakers` (PhISEM) | 16 acoustic percussion instruments; the catalog had none |
| `banded-waveguide` | STK `BandedWG` | a bowed BAR; the catalog strikes bars and bows strings |

**The patent note STK's headers carry is spent.** Stanford's waveguide patent is
US 4,984,276, filed 1989-09-27 and issued 1991-01-08; under the pre-1995 rule
(later of 17 years from issue, 20 from filing) it expired no later than
2009-09-27. It never applied to `shakers` at all — PhISEM has no waveguide.

### `brass` was specced, written, and DROPPED. Do not re-attempt it as a port.

STK's `Brass` does not sustain. Built standalone and measured across nine
(sample rate, pitch) combinations, it produces sound in exactly one — 22050 Hz
at 440 Hz — and is silent at its own default 44.1 kHz. At 48 kHz it emits a
0.019-peak blip for the first 100 ms and then *exact* silence; with vibrato it
passes the vibrato through at 0.0048 and still does not oscillate.

The mechanism: the lip filter is an all-pole resonator with a DC gain near 50,
so a steady mouth pressure drives the squared lip position past the model's
clamp of 1.0, the valve pins fully open, the output becomes a constant, the DC
blocker removes it, and the bore never fills. It works only as long as the ADSR
attack transient is still ringing the lip.

A faithful port reproduced this exactly — the control-response gate caught it as
a MORPH difference of literally 0.000000. **The gap is still real** (the catalog
has a reed and a bow and no lip, and a lip valve is not a reed: it can be tuned
away from the bore, which is what lipping and overblowing are). But closing it
means a redesign — a DC-zeroed bandpass lip with a static opening bias and a
tuned drive — which would be a Rubato Lab engine after Cook, not an STK port,
and should be scoped as such.

### What the two shipped engines needed beyond transcription

Four defects, all found by SWEEPING the engine rather than by ear or review.
Worth generalising: STK's control mappings assume a MIDI controller and a
player, and re-exposed as four Plaits knobs they leave large dead regions.

1. **STK's resonators are un-normalised.** All-pole, numerator 1, peak gain
   1/(1−r²) — spanning 1.6 to 125 across the sixteen shaker instruments. Cook's
   per-instrument gains only partly offset it because STK assumes a downstream
   master gain; his own commented-out debug line in `tick` checks for output
   over 1.0. Normalise to unit peak, then the instrument's gain means its
   loudness.
2. **A bow has a minimum speed.** The bow table's grip falls off as the *fourth
   power* of the bow-to-bar velocity difference, so below ~0.055 a ringing bar
   is damped rather than driven. Upstream's 0.03 floor = silence over the bottom
   fifth of the knob.
3. **Bow force costs grip.** Upstream runs the friction slope to 1.0, silent
   from ~0.6 of the knob up. Stop at 3.0 and compensate velocity for force.
4. **A preset can be inaudible in a mode upstream never used it in.** The
   uniform bar's pow(0.9, i+1) gains give a loop gain of 0.899 against the other
   presets' 0.999 — 600× quieter *under a bow*. Upstream never hits it because
   that preset defaults to being struck.

**Sample-rate correction is mandatory for anything from STK**, and it is easy to
miss because nothing breaks: every decay constant and filter radius is a raw
per-sample number tuned at 44.1 kHz, so carried to 48 kHz they are all ~8.8%
wrong *in the same direction*. Raise to the power 44100/48000; scale event
probabilities by the same ratio. Frequencies are in Hz and need nothing.

**The 16 KB arena is per-engine, not shared.** `Voice::Init` calls
`allocator->Free()` before each engine's `Init`, so `banded-waveguide` can take
10.5 KB of delay lines without costing anything else. This is what makes
waveguide models affordable at all, and it is worth knowing before rejecting one
on memory grounds.

### Taxonomy: SETTLED as Rubato Lab

Both are `origin: "Rubato Lab"`, packages under `rubato/`, author
"Lyle Mills, after Perry R. Cook and Gary P. Scavone", with STK's copyright and
licence carried unchanged.

Lyle's reasoning, and it is the right cut: nobody in the modular community
arrives at these with prior expectations, unlike Bees-in-the-Trees and Braids
Renaissance, which people have used for years and expect to behave a certain
way. That frees these two to be *adapted* rather than matched -- and once
adapted they are more Rubato than Community, which resolves the strain instead
of papering over it. `Community` keeps meaning what the website's copy already
implies.

Practical rule that falls out: **an engine whose upstream is widely used stays
Community and stays faithful; an engine whose upstream nobody here has heard of
becomes Rubato Lab and gets tuned to be played.**

### Still blocked on the same two things

Flash and the hardware CPU probe, exactly as in §5 and §7 — and the website
catalog sync behind them, since `plaitsFlashBudget.test.ts` reds on any catalog
engine without a measured cost. These two join the same leave-one-out batch.


---

## 9. Adaptations after the rebadge (2026-07-28)

Both STK engines were measured and adjusted rather than left faithful, per §8's
taxonomy rule.

**shakers -- level-matched.** The instrument selector spanned **46 dB**
(cabasa +12.7, water drops -33.5 against the mean), because STK expects each
instrument on its own fader rather than swept under one knob. Per-preset makeup
gains measured across the shake/decay/object space, clamped to [0.15, 20] so
the sparsest are lifted as far as their crest factor allows. Worst remaining
deviation 6.5 dB; thirteen of sixteen inside 0.7 dB. Re-measure and re-bake if
the resonator normalisation or the energy model ever changes.

**banded-waveguide -- bow noise.** Upstream's bow velocity is perfectly smooth,
which is the main reason a waveguide bow sounds synthetic. Noise on the
*velocity* (not the output) modulates the friction curve, so it colours the
attack rather than sitting on top as hiss.

### brass -- BUILT (2026-07-28). The account below is how, kept because the
### findings generalise to any waveguide work.

Attempt 5 speaks, tunes and plays. What follows is the failure trail plus the
six things that fixed it.

Four structured attempts, all measured. Do not start from scratch; start from
attempt 4, which was close.

1. **Faithful port of STK Brass.** Silent, because upstream is (see §8).
2. **DC-zeroed bandpass lip + mixing scattering** (`out = area*mouth +
   (1-area)*bore`). Oscillates, but chaotically and at a frequency independent
   of the bore -- the mixing form is not a scattering junction and has no
   mechanism locking it to the delay.
3. **STK Clarinet's reed-table topology, non-inverting for a brass harmonic
   series.** A passive-loop diagnostic proved the waveguide itself correct
   (inverting rings at fs/2L, non-inverting at fs/L). But a non-inverting loop
   parks on its DC mode, so it needs an in-loop DC blocker -- and *with* the DC
   blocker the reed table has no oscillation mechanism at all, because the
   linear loop gain `|refl| * reflect * |lowpass|` is always below 1. It only
   ever "worked" by ringing up DC.
4. **Mass-spring lip valve + Bernoulli flow into a DC-blocked non-inverting
   waveguide.** THIS IS THE RIGHT STRUCTURE. Confirmed by measurement: the
   oscillation frequency tracks the bore length (MACRO sweep moved the ratio
   0.89 -> 0.32) and tracks 1V/oct across the keyboard at a constant ratio.
   The failure is gain staging only -- the window between "does not speak" and
   "runs away into the clip" was narrower than the grid resolution used.

5. **The junction was wrong in all four.** `p_plus = p_minus + Zc*u` with
   `p = p_plus + p_minus`, and the VALVE reads the total junction pressure --
   not the reflected wave. Fixing that alone turned chaos into locking.

**The six findings, all measured, all reusable:**

1. **Junction form** (above). Feeding a valve from the reflected wave decouples
   the feedback that makes it lock to a bore at all.
2. **A non-inverting loop needs an in-loop DC blocker.** Settled with a passive
   probe: inverting rings at fs/2L, non-inverting at fs/L, and non-inverting
   *without* the blocker rings at 0 Hz. Attempt 3 had only been ringing up DC,
   which is why adding the blocker looked like it broke a working model.
3. **A hard nonlinearity destroys mode selection.** Lips slamming shut every
   cycle generate a full harmonic series and the fundamental wins every time,
   whatever the lip is tuned to. Opening and stiffening the valve is what lets
   the lip resonance decide.
4. **Lock zones are narrow: partial n captures for lip/f_bore in
   [0.90n, 1.01n].** About 40% of a linear lip sweep is silent, so the control
   must map to a PARTIAL INDEX and place the lip inside the zone. Zones widen
   with mouth pressure, so the softest playing sets the safe placement.
5. **Partial 1 misbehaves** -- always sharp, erratic zone, exactly as a real
   pedal tone does. Tune the bore an octave down and start at partial 2.
6. **The lip pulls the pitch sharp by `-4.8 + 67.2/n` cents.** One partial
   sounds at a time, so null it per partial. Final: mean 6.3 cents, worst 25.8,
   ~1% cracks over 312 points.

**The harness is committed** at `alt_firmwares/research/brass_lip/` -- the
model, nine experiments, and a README saying which question each one answers.
`passive_loop.cc` is the one to run first whenever a self-oscillating model goes
quiet: it isolates the waveguide from the valve, and it is what settled findings
1 and 2. `zones.cc` is the one that mattered most -- every control-mapping
decision came out of its Arnold-tongue table.

**Two more for any future waveguide engine.** The output tap is the MOUTHPIECE,
not the bell -- the radiated signal is physically correct and slides 30 dB
across the keyboard, while mouthpiece pressure is flat to 0.7 dB. And **both
taps must be DC-blocked**: blowing makes a steady flow as well as an
oscillation, and the unblocked tap measured RMS 27923 against DC 26548. It
passed the audition and control-response gates, neither of which looks at DC.
**Consider adding a DC check to the in-tree gates** -- `check --full` catches it
per scenario, but `plaits_test` does not, and this would have shipped.

---

## 10. First listening pass (2026-07-28) — catalog is 55, not 56

The six from sections 7–9 finally went through a speaker. Three outcomes, plus
the two blockers that turned out not to be blockers.

### banded-waveguide is CUT. Do not re-add it without a reason to.

Lyle's call, and the numbers back it: it was the most expensive of the six
(81% of CPU budget, error band running to **104%** — the only one whose band
crosses the limit) and the least interesting to listen to. Cost is almost
entirely per-mode — about **53 instructions per mode over ~103 fixed** — and
HARMONICS selects presets of 4, 4, 5 and 6 modes, so the Prayer bowl is the
worst case. Capping at 4 modes would have brought it to ~61%, but thinning the
bowl is the opposite of making it more interesting, and nothing else on offer
made it both cheaper and better.

Removed from `catalog.json` / `public_catalog.json`, the SDK packages, the
engine sources, and all three `plaits_test`/`cpu_bench` hooks. **The source is
in this branch's history** (last present at `1b4bac3`), so re-adding is a
revert, not a rewrite. Catalog validates at **55**.

Worth keeping from it: the 16 KB arena being per-engine (§8) is still true and
still what makes any waveguide model affordable.

### shakers — two real defects, both fixed

**It could not be struck.** The engine injected shake energy continuously
regardless of trigger state — it never tested `TRIGGER_UNPATCHED`, even though
the comment directly above the line already claimed it did. A patched trigger
therefore got a drone with a bump on it, and the only way to hear a struck
shaker was to turn TIMBRE fully down, which also gave up control of how hard it
was struck. Now gated like every other sustaining engine here
(`ReedPipeEngine`'s `blowing`): shaking while TRIG is unpatched **or** a patched
gate is high. Every `triggerHz: 0` scenario renders byte-identical, so the drone
is untouched. The ratchet instruments change and should — a triggered guiro
scrapes once per trigger instead of rasping forever.

*Generalises:* a comment asserting a conditional is not evidence the conditional
exists. This one had been read and believed several times.

**The selector ducked.** The makeup gains had been clamped to an arbitrary
`[0.15, 20]`, leaving water drops 8.6 dB and little rocks 10.1 dB below the
rest. **Nothing clamps makeup at runtime** — that ceiling bought nothing. The
real constraint is *peak headroom*: a high-crest instrument clips before its RMS
reaches the target. Re-measured against that constraint directly and re-baked:
worst deviation **10.1 dB → 1.5 dB**, fourteen of sixteen inside 0.31 dB.
Crunch and big rocks stay ~1.2 dB low deliberately — crest factors near 40 mean
they are peak-limited, and for a sound that impulsive peak drives loudness more
than RMS, so matching RMS would make them the loud ones.

Harness committed at `alt_firmwares/research/shakers_levels/` (108 points per
instrument), so a re-bake after any energy-model change is a re-run.

### brass — still owed a buzz

Accepted as musical, but Lyle's verdict is that it "lacks the buzz of brass",
and he is right. The cause is structural rather than a bad constant: finding 3
in §9 is that a *hard* valve nonlinearity destroys partial selection, so the
valve was deliberately made gentle — and a gentle valve generates few upper
harmonics. The brightness therefore cannot come from the valve.

Where it should come from instead is **nonlinear wave propagation in the bore**:
in a real instrument the compression phase travels faster than the rarefaction,
the wavefront steepens toward a shock, and that is both the physical source of
brassiness *and* why brass gets brighter the harder it is played. It lives in
the propagation path, not the valve, so in principle it adds harmonics without
touching the mode selection that was so hard to win.

Two cautions for whoever picks this up. Implement it as an amplitude-dependent
propagation *delay* rather than a waveshaper in the loop — a delay modulation
is a timing effect, so it cannot raise the loop gain and cannot break passivity,
where an in-loop waveshaper can do both. And re-run `zones.cc` and `signed.cc`
afterwards regardless: lock zones are only `[0.90n, 1.01n]` wide and the tuning
was nulled to a measured `-4.8 + 67.2/n` cents, so both are exactly what a new
nonlinearity would disturb.

### The two "blocked" items from §7–9, re-scoped

**qemu was never blocked here** — `qemu-system-arm` is on PATH via Homebrew and
`cycles_plugin.so` is already an arm64 Mach-O. The tooling was written for this
Mac, not for a container. Measured worst case, all six:

| engine | insn/sample | budget |
|---|---:|---:|
| bytebeat | 107.0 | 21% |
| brass | 204.5 | 39% |
| shakers | 225.5 | 43% |
| scale-stack | 371.3 | 71% |
| diatonic-chord | 371.9 | 72% |
| ~~banded-waveguide~~ | 421.7 | 81% (cut) |

**Flash is blocked on something else entirely.** Not the toolchain — `GET
/v1/catalog` returns `approvedEngineIds`, the builder rejects anything outside
it, and all six were absent. A not-yet-deployed engine's flash cost is
unmeasurable by construction, so the order is: roll the builder image → sweep →
extend `engineFlashBytes` → sync the catalog.

Two corrections to §7–9 while doing this: the **eleven Braids engines are
already measured** (all 40 catalog engines have costs and
`plaitsFlashBudget.test.ts` is green), so the remaining job is the new engines
only — about **8 builds**, not 110. And the sweep harness, which §5 assumed
existed somewhere, was living in a session scratchpad under `/private/tmp`; it
is now committed at `website/scripts/plaits-flash-sweep/` in the rubato-audio
repo.

### The DC check from §9 is in

`plaits_test` now measures the mean of both taps across each audition render and
fails above 0.2, matching what `plaits_lab.py` already applied to packaged
engines. All 27 remaining audition engines pass; the largest is saw-comb at
+0.126, then ring-mod and csaw at +0.058, and brass sits at +0.003. It reports
every offender before aborting rather than stopping at the first.
