# Braids → Plaits Palette engine port — handoff to a local session

**Status:** design + spec complete. Implementation NOT started.
**Written:** 2026-07-28, from a Claude Code *cloud* session that lacked the
toolchain to implement it properly.
**Pick this up in a LOCAL session** (Lyle's Mac), where the ARM toolchain,
Docker, qemu and hardware are available.

**The spec is `BRAIDS_PORT_SPEC.md`** next to this file (same content at
`alt_firmwares/BRAIDS_PORT_SPEC.md` in eurorack) — 12 engine specifications,
18 cross-cutting rules, build order, rejected candidates, risks. This file is
the orientation layer; the spec is the work.

| Repo | Branch | Contains |
|---|---|---|
| `lylepmills/rubato-audio` | `claude/braids-engines-plaits-palette-je03ac` | this handoff + the spec |
| `lylepmills/eurorack` | `claude/braids-engines-plaits-palette-je03ac` | same two docs under `alt_firmwares/`; branched off `master` @ `92895b7`, no engine code yet |

---

## 1. What was decided

1. **Port the Braids models Plaits does not already refine.** The models it
   does refine (HARM, PLUK, BELL, DRUM, KICK, SNAR, CYMB, VOWL, BUZZ, MORPH,
   the saw/square pair, WTBL/WMAP/WLIN, NOIS, CLKN, TWNQ, PRTC, CLOU, SAW
   SWARM) are **phase 2** — revisit once the first batch is in users' hands and
   OG-vs-refined demand is real rather than assumed.
2. **Firmware lands in `lylepmills/eurorack`** on the branch above.

Design then narrowed the candidate list to **12 engines covering 19 Braids
models** — 9 unconditional, 3 gated on a decision or a measurement, 2 dropped.

## 2. The engines

**Unconditional nine — 18,240 B estimated (17.8 KB)**

| id | Braids ancestry | Est. flash |
|---|---|---:|
| `z-filter` | ZLPF, ZPKF, ZBPF, ZHPF (4 models) | 2,200 B |
| `bowed` | BOWD | 2,400 B |
| `toy` | TOY* | 1,520 B |
| `csaw` | CSAW | 1,400 B |
| `sub-oscillator` | SUB↓, SUB↑ (2 models) | 1,300 B |
| `ring-mod` | RING | 1,700 B |
| `digital-modulation` | QPSK | 1,620 B |
| `vowel-fof` | VFOF | 3,100 B |
| `saw-comb` | saw → comb | 3,000 B |

**Gated three — 6,850 B.** `raw-fm` (FM/FBFM/WTFM — 3 of its 4 knobs replicate
`two-op-fm`, so it needs your A/B), `fluted` (gated on a mode-tracking
measurement, §3.11), `triple` (4 models — reachable today via user
`PLAITS_CHORD_CENTS` tables + `virtual-analog` detune + `swarm`, so the unique
offer is the gesture, not the sound).

**Dropped, with source citations (§7).** `vosim` — the exact topology of the
shipped `granular-formant` (`grain_engine.cc:66-78`); the only unreachable
content is an absolute rather than ratio second formant, which is a re-macro of
an existing engine, not a slot. `wave-paraphonic` — duplicates `ChordEngine`'s
upper MORPH half, was the most expensive candidate (~6.5–7 KB realistic), and
its headline anti-aliasing claim is false (`cutoff` saturates above 375 Hz, so
both filters are inert).

**Every flash number above is an estimate, not a measurement.** See §4.

## 3. Why this is being handed off

The cloud container host-compiles and renders audio fine, but **cannot**:

- compile for ARM (`arm-none-eabi-gcc` absent; the SDK's ARM path needs the
  `plaits-lab-builder:local` Docker image, and there is no Docker daemon),
- therefore **cannot measure flash**,
- run `alt_firmwares/plaits_lab_sdk/qemu/estimate.py` for CPU cycles (no qemu),
- measure on hardware (`build --hardware --cpu-probe`),
- audition anything on a module or in Live.

The design work did not need any of that. The implementation does.

## 4. Flash framing — corrected

An earlier framing in this file said these engines are "swap-ins" against
stock-24's ~688 B of headroom. That is only half right, and the half that
matters is the other one:

- **The builder compiles per recipe.** Growing the catalog from 39 to 51
  engines costs **zero flash** in any recipe that does not select them. Adding
  these engines does not consume anyone's budget.
- **Only preset membership is zero-sum** — a preset is exactly 24 or 32 slots,
  and stock-24 genuinely sits ~688 B under the 224 KB region.

So the catalog can absorb all twelve. The spec therefore recommends
**catalog-only for now, no preset changes**, until Wave 1 produces one real
`arm-none-eabi-size` number to calibrate against (§8 Q4).

## 5. Environment for the local session

```sh
git clone https://github.com/lylepmills/eurorack ~/Code/eurorack   # if absent
cd ~/Code/eurorack
git checkout claude/braids-engines-plaits-palette-je03ac
git submodule update --init --recursive     # REQUIRED — a fresh clone/worktree has
                                            # no submodules and every compile-based
                                            # SDK test then dies on stmlib/dsp/units.cc

git clone --depth 1 https://github.com/pichenettes/eurorack /tmp/braids-upstream

docker build --platform linux/amd64 -t plaits-lab-builder:local \
  -f Dockerfile.plaits-builder .          # for the ARM half
```

## 6. Verification loop — PROVEN WORKING

Run end to end in the cloud session against `glisson`; all of it passed, so the
loop itself is not in question:

```sh
python3 alt_firmwares/plaits_lab_catalog/validate_catalog.py
#   -> "catalog ok: 39 immutable packages"
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py catalog
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py init /tmp/probe --from glisson --author "…"
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py check /tmp/probe --full
#   -> metadata/licensing, host compilation, sanitizer execution + audio health
#      (peak / RMS / DC per scenario), host CPU as a ratio against two-op-fm
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py render /tmp/probe \
  --scenario hero --output /tmp/probe.wav
```

Locally, add the two the cloud could not run:

```sh
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py check <pkg> --arm
python3 alt_firmwares/plaits_lab_sdk/qemu/estimate.py <pkg> --sweep
```

`check --full` warns, correctly, that **host timing does not predict hardware
cost**; publication requires `build --hardware --cpu-probe`.

## 7. Authoring contract (verified against the source)

Engines are **in-tree**, like the 15 Rubato Lab engines — not SDK packages.
`alt_firmwares/plaits_lab_sdk/packages/` holds only two reference examples.

Per engine: `plaits/dsp/engine2/<name>_engine.{h,cc}` subclassing
`plaits::Engine`; a `PLAITS_STEREO_<ID>` gate in
`plaits/dsp/engine/stereo_config.h` (macro name = catalog id upper-cased,
`-` → `_`); the `engines[]` entry plus its `manuals` entry in
`alt_firmwares/plaits_lab_catalog/catalog.json`.

Digests are content-addressed by `validate_catalog.py`: `package_digest` hashes
the catalog record minus `digest` plus the bytes of every declared source file;
`documentation_digest` hashes the record minus `source`/`postProcessing` plus
the manual. **Any source edit changes the digest**, and engine digests are in
the hosted builder's allowlist — firmware image and website catalog snapshot
must roll together.

Two helpers to use rather than reinvent (`plaits/dsp/engine/engine.h`):
`ApplyMacro(stock, min, max, macro)` for the fourth macro, and
`StereoPanGains()` for equal-power pan.

**Correction to an earlier assumption in this file:** the port was initially
specced with `origin: "Braids"`. That is invalid —
`alt_firmwares/plaits_lab_sdk/engine-package.schema.json:36` constrains
`origin` to the enum `Mutable Instruments | Rubato Lab | Community`. The spec
defaults to `Rubato Lab` / `rubato/<id>` with MIT dual copyright (Émilie Gillet
plus the port's own line); **§8 Q1 is your call, and it must be uniform across
all twelve.** Any distinct "Braids" badge is then a website-side presentation
choice, not a catalog value.

## 8. What the adversarial review caught

Worth knowing, because it says how much the per-engine text was corrected —
the specs as written are already fixed for all of this:

- **Seven of fourteen designs claimed "bounded ±1.0, no limiter needed" while
  mandating a DC blocker.** `voice.h` shows a *positive* gain bypasses
  `stmlib::Limiter` entirely and goes to `Clip16`. Concrete hard clips fixed:
  WTFM at 1.27, QPSK AUX at −1.4.
- Three correctness blockers: `z-filter` unwrapped float phase → out-of-bounds
  `lut_sine` read at index ~500,000; `bowed` delay underflow from ~MIDI 85 that
  neither in-tree test would have caught; `saw-comb` net HF loop gain 1.083 > 1
  → self-oscillation.
- `SoftLimit` is unbounded (must be `SoftClip`); make-up gain goes *inside* the
  clip; one design's `NoteToFrequency` formula was wrong by fs²; `size -= 2`
  is what distinguishes 48 kHz from 96 kHz Braids functions (the source of
  Fluted's 37-cent pitch error).

## 9. Website side (`lylepmills/rubato-audio`) — mapped, not written

The catalog the site serves is a generated, hash-pinned snapshot:

```sh
cd website && node scripts/sync-plaits-catalog.mjs --repo ~/Code/eurorack --ref <rev>
```

The deployed builder image must be built from the same revision — a snapshot
ahead of the builder breaks live builds for every engine whose digest changed.

Files to edit once engines exist: `engines.ts` (origin/tone/colour/label — see
the §7 correction), `PlaitsEditor.tsx` (origin filter chips ~1888, the two
label mappings ~1896 and ~1932, the Lab slot count ~635),
`plaits-palette.css` (`artwork-<tone>`), `flash-budget.ts` (`engineFlashBytes`
+ `engineStereoBytes` + `stereoToggleableEngineIds` — **real ARM measurements,
taken the documented leave-one-out way**; `plaitsFlashBudget.test.ts` fails on
drift), `plaitsCatalog.test.ts` (sha256 pin), `plaits-pins.json` (rewritten by
the sync script).

## 10. Build order (spec §5)

`z-filter` alone first — 4 models in one slot, no feedback, no DC blocker,
establishes the 14-step template → then `bowed`, `toy`, `csaw`, `ring-mod` in
parallel → then `sub-oscillator`, `digital-modulation`, `saw-comb` → then the
gated three. **Pattern-B stereo landings must serialise** (`toy`, `vowel-fof`,
`triple` share three files).

## 11. Open questions for you (spec §8)

1. **Attribution** — see the §7 correction. Must be uniform.
2. **Ship `raw-fm`?** 3 of 4 knobs replicate `two-op-fm`. A/B it yourself.
3. **Ship `triple`?** Largely reachable today by other means.
4. **Preset evictions** — zero-sum, and unanswerable until Wave 1 gives one
   real `arm-none-eabi-size` number. Recommendation: catalog-only for now.
5. **`bowed`'s residual octave fold** (17.2 Hz vs Braids' 11.4 Hz) — accept, or
   spend 4–8 KB of the 16 KB arena.
