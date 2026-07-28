# Braids → Plaits Palette engine port — handoff to a local session

**Status:** design/spec complete (see companion spec), implementation NOT started.
**Written:** 2026-07-28, from a Claude Code *cloud* session that lacked the
toolchain to do the implementation properly.
**Pick this up in a LOCAL session** (Lyle's Mac), where the ARM toolchain,
Docker, qemu, and hardware are available.

Branches already created and pushed, same name in both repos:

| Repo | Branch | Contains |
|---|---|---|
| `lylepmills/rubato-audio` | `claude/braids-engines-plaits-palette-je03ac` | this handoff + the port spec |
| `lylepmills/eurorack` | `claude/braids-engines-plaits-palette-je03ac` | branched off `master` @ `92895b7`, no changes yet |

---

## 1. What was decided (Lyle, this session)

1. **Scope: port the ~15 non-redundant Braids models now.** The Braids models
   that Plaits already refines (HARM, PLUK, BELL, DRUM, KICK, SNAR, CYMB, VOWL,
   ZLPF/BP/HP, BUZZ, MORPH, the saw/square pair, WTBL/WMAP/WLIN, NOIS, CLKN,
   TWNQ, PRTC, CLOU, SAW SWARM) are **phase 2** — revisit once the first batch
   is in users' hands and the OG-vs-refined demand is real rather than assumed.
2. **Firmware lands in `lylepmills/eurorack`**, pushed to the branch above —
   not delivered as a patch.

## 2. Why this is being handed off

The cloud container can host-compile and render audio, but **cannot**:

- compile for ARM (`arm-none-eabi-gcc` absent; the SDK's ARM path needs the
  `plaits-lab-builder:local` Docker image and there is no Docker daemon),
- therefore **cannot measure flash** — and flash is the binding constraint here
  (stock-24 already sits ~688 B under the 224 KB region),
- run `alt_firmwares/plaits_lab_sdk/qemu/estimate.py` for CPU cycles (no qemu),
- measure on hardware (`build --hardware --cpu-probe`),
- audition anything in real Live / on a real module.

Every flash and CPU number in the spec is therefore an **estimate benchmarked
against measured comparables**, not a measurement. Treat them as ordering
hints, not budget truth.

## 3. Environment the local session needs

```sh
# firmware (the port target)
git clone https://github.com/lylepmills/eurorack ~/Code/eurorack   # if not already present
cd ~/Code/eurorack
git checkout claude/braids-engines-plaits-palette-je03ac
git submodule update --init --recursive        # REQUIRED — a fresh worktree/clone
                                               # has no submodules and every
                                               # compile-based SDK test then dies
                                               # on stmlib/dsp/units.cc

# upstream Braids source, for reference while porting
git clone --depth 1 https://github.com/pichenettes/eurorack /tmp/braids-upstream
```

Plus, for the ARM half:

```sh
docker build --platform linux/amd64 -t plaits-lab-builder:local \
  -f Dockerfile.plaits-builder .
```

## 4. The verification loop — PROVEN WORKING in the cloud session

This was run end to end against an existing engine (`glisson`) and all of it
passed, so the loop itself is not in question:

```sh
cd <eurorack>
python3 alt_firmwares/plaits_lab_catalog/validate_catalog.py
#   -> "catalog ok: 39 immutable packages"

python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py catalog          # list engines
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py init /tmp/probe --from glisson --author "…"
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py check /tmp/probe --full
#   -> metadata/licensing, host compilation, sanitizer execution + audio health
#      (peak / RMS / DC per scenario), and a host CPU smoke test stated as a
#      ratio against stock two-op-fm
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py render /tmp/probe \
  --scenario hero --output /tmp/probe.wav
```

Locally, add the two steps the cloud could not run:

```sh
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py check <pkg> --arm   # needs Docker image
python3 alt_firmwares/plaits_lab_sdk/qemu/estimate.py <pkg> --sweep    # CPU, with error band
```

`check --full` prints its own warning worth heeding: **host timing does not
predict hardware cost**, and publication requires a real hardware measurement
via `build --hardware --cpu-probe`.

## 5. The authoring contract (verified by reading the source)

Engines are **in-tree**, like the 15 Rubato Lab engines — not SDK packages.
`alt_firmwares/plaits_lab_sdk/packages/` holds only two reference examples
(`rubato/pulsar`, `mutable-instruments/virtual-analog`); the real engines live
in `plaits/dsp/engine/` (stock) and `plaits/dsp/engine2/` (Plaits 1.2 + Lab).

Files touched to add one engine:

- `plaits/dsp/engine2/<name>_engine.h` / `.cc` — subclass `plaits::Engine`:
  `Init(stmlib::BufferAllocator*)`, `Reset()`, `LoadUserData(const uint8_t*)`,
  `Render(const EngineParameters&, float* out, float* aux, size_t, bool* already_enveloped)`,
  and `stereo_capable()` returning the engine's `PLAITS_STEREO_<ID>` macro.
- `plaits/dsp/engine/stereo_config.h` — add the `PLAITS_STEREO_<ID>` gate
  (default 1; the hosted builder passes `=0` per object to dead-strip it).
  Macro name = catalog id upper-cased with `-` → `_`.
- `alt_firmwares/plaits_lab_catalog/catalog.json` — the `engines[]` entry
  (`id`, `packageId`, `name`, `author`, `origin`, `family`, `description`,
  `tags`, `controls[4]`, `outputs[2]`, `source.{header,className,member,files}`,
  `postProcessing.{alreadyEnveloped,outGain,auxGain}`) and the matching
  `manuals` entry (`controls.{harmonics,timbre,morph,macro}` + `trigger`).
- `alt_firmwares/plaits_lab_catalog/shared_modules.json` — only if the engine
  shares code with another.

Digests are content-addressed and recomputed by `validate_catalog.py`:
`package_digest` hashes the catalog record (minus `digest`) plus the bytes of
every declared source file; `documentation_digest` hashes the record minus
`source`/`postProcessing`, plus the manual. **Any source edit changes the
digest**, and engine digests are part of the hosted builder's allowlist — so
the firmware image and the website catalog snapshot must roll together.

Two helpers to use rather than reinvent, both in `plaits/dsp/engine/engine.h`:

- `ApplyMacro(stock, min, max, macro)` — the fourth-macro mapping. `macro=0.5`
  returns exactly `stock`, so the original Braids sound sits at the centre
  detent. Every port's fourth macro should go through this.
- `StereoPanGains(position, &l, &r)` — equal-power pan built on `stmlib::Sqrt`
  (compiles to a bare `VSQRT`; the firmware links no libm `sqrtf`).

## 6. The porting hazards that drove the design

- **Braids runs at 96 kHz** (`braids.cc`: `sys.Init(F_CPU / 96000 - 1, true)`);
  **Plaits runs at 48 kHz.** A naive port therefore aliases *more* than the
  original. Every engine in the spec carries an explicit anti-aliasing decision;
  "accept it, that's the model's character" is a legitimate answer for CSAW and
  TOY* but has to be a decision, not an oversight. High-index feedback FM
  (FBFM/WTFM) is the risky end.
- **Flash is the binding constraint**, not CPU. Measured comparables to
  benchmark against: Rubato Lab engines run 1,008–3,392 B; stock runs 1,344 B
  (two-op FM) to 23,216 B (speech). A Braids model that drags in a large
  `braids/resources.cc` table is the classic estimate-buster — check hard
  whether Plaits already carries an equivalent table.
- **Licensing is clean.** Braids is MIT (Émilie Gillet). Ports carry her
  copyright plus the port's own line. The SDK's license allowlist (MIT,
  BSD-2/3-Clause, ISC) exists because everything is statically linked into one
  distributed firmware image; MIT is fine.

## 7. Website side (`lylepmills/rubato-audio`) — mapped, not yet written

The catalog the site serves is a **generated, hash-pinned snapshot** of the
firmware repo. It moves only via:

```sh
cd website && node scripts/sync-plaits-catalog.mjs --repo ~/Code/eurorack --ref <rev>
```

and the deployed builder image must be built from the same revision — a
snapshot ahead of the builder breaks live builds for every engine whose digest
changed.

Files needing edits once the engines exist:

- `src/components/plaits-palette/engines.ts` — add `"Braids"` to the
  `EngineOrigin` union, a matching `EngineTone`, an entry in `artworkOrder`, a
  colour, and the badge label (currently `origin === "Rubato Lab" ? "LABS" : …`).
- `src/components/plaits-palette/PlaitsEditor.tsx` — the origin filter chips
  (~line 1888), the two origin label mappings (~1896 and ~1932), and the
  Rubato-Lab slot count (~635) if Braids engines should count separately.
- `src/components/plaits-palette/plaits-palette.css` — the `artwork-<tone>` class.
- `src/components/plaits-palette/flash-budget.ts` — `engineFlashBytes` entries
  (and `engineStereoBytes` / `stereoToggleableEngineIds` where applicable).
  **These must be real ARM measurements**, taken the documented leave-one-out
  way against the live builder; `src/lib/plaitsFlashBudget.test.ts` fails if the
  catalog and the table drift apart.
- `src/lib/plaitsCatalog.test.ts` — sha256 pin of `catalog.generated.json`.
- `src/components/plaits-palette/plaits-pins.json` — rewritten by the sync script.

Also worth doing, and cheap: a **"Braids Classics" preset** alongside stock /
experimental / Stereo Dreams / Empty in `engines.ts`. Twenty-four slots of the
OG models is a much stronger hook than the engines scattered through a catalog
of 50+.

## 8. Open items for the local session

1. Implement the engines per the spec, one at a time, running
   `check --full` + `render` after each.
2. `check --arm` and `qemu/estimate.py --sweep` on each — the numbers this
   session could not produce. Expect the spec's flash estimates to move.
3. Audition on hardware. The specs are argued from source, not from listening;
   the "zhuzh" parameter ranges in particular want ears on them.
4. Re-measure the flash table and roll the builder image + website catalog
   snapshot together.
5. Decide whether Braids ports get their own catalog origin/badge (the spec
   assumes `origin: "Braids"`, `author: "Émilie Gillet"`, `packageId
   braids/<id>`) or fold in under Mutable Instruments.
