# Braids → Plaits Palette port: implementation progress and corrections

Companion to `BRAIDS_PORT_SPEC.md`. The spec was written where the ARM
toolchain, Docker and qemu could not run, so every flash and CPU number in it
is an estimate and several claims are argued from source rather than measured.
This file records what the local session actually measured, and corrects the
spec where measurement disagreed with it. **Read this alongside §2 of the
spec; where the two conflict, this file is the one with numbers behind it.**

Branch: `claude/braids-engines-plaits-palette-je03ac` (both repos).

---

## 1. Status

| Engine | State | qemu CPU | A/B vs Braids |
|---|---|---:|---|
| `z-filter` | **landed** | 62% | all 4 models within 0.05 dB AC RMS, +5 cents |
| `toy` | **landed** | 35% | within 0.42 dB mean, ≤9 cents |
| `csaw` | **landed** | 12% | 0.00 dB mean at two settings, 0.75 dB at a third |
| `bowed` | **landed** | 37% | 3–5 dB; chaotic self-oscillator, see §3.10 |
| `ring-mod` | **landed** | 69% | within 0.04 dB energy-weighted at four detunings |
| `sub-oscillator` | **landed** | 28% | 0.23 / 0.42 dB against both source models |
| `digital-modulation` | **landed** | 18% | 0.00 dB at the stock frame; 0.09–0.45 dB across settings |
| `saw-comb` | **landed** | 32% | 0.19 / 0.45 / 0.67 dB |
| `vowel-fof` | **landed** | 72% | 1.96–2.29 dB |
| `raw-fm` | **landed** | 15% | 0.05 / 0.13 / 0.02 dB across all three source models |
| `triple` | **landed** | 52% | 0.24 / 0.25 / 0.03 / 0.02 dB across all four source models |
| `fluted` | still gated on the §3.11 mode-tracking measurement | — | — |

**ALL ELEVEN ENGINES ARE LANDED.** What remains is measurement and the
website, not DSP. Notes kept for reference:

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
4. `previews.generated.json` + the mp3s — `render-previews.mjs`, **and**
   `scripts/plaits-previews/render_previews.cc`, which keeps its own
   HARDCODED engine list (includes, a mono `Emit<>` and a stereo `Emit<>`
   per engine). *(not in the handoff, and it needs a C++ edit, not a script
   run)*

No changes were needed to `engines.ts`, `PlaitsEditor.tsx` or
`plaits-palette.css`: with `origin: "Mutable Instruments"` and no `artwork`
field these fall through the existing fallbacks, and the Lab slot counter
keys off `origin === "Rubato Lab"` so they do not disturb it.

### Measured ARM flash — real, replacing every estimate

Swept 2026-07-28 against a local builder container built from `dc1650c14`,
leave-one-out into Speech's slot in the stock-24 context. Baseline 181,216 B.

| engine | measured | spec estimate |
|---|---:|---:|
| raw-fm | 880 | 1,450 |
| digital-modulation | 1,200 | 1,620 |
| toy | 1,232 | 1,520 |
| csaw | 1,392 | 1,400 |
| z-filter | 1,712 | 2,200 |
| ring-mod | 1,872 | 1,700 |
| sub-oscillator | 2,256 | 1,300 |
| saw-comb | 2,496 | 3,000 |
| vowel-fof | 2,640 | 3,100 |
| bowed | 2,928 | 2,400 |
| triple | 3,408 | 2,800 |
| **total** | **22,016** | 22,490 |

The spec's AGGREGATE was within 2 %. Its PER-ENGINE numbers ranged −39 % to
+74 %, so treat §1 as a ranking and never as a budget.

**Two traps when re-running the sweep.** The builder image bakes the firmware
source in (`COPY . /workspace`), so `docker build` it AFTER the engines land
or every new engine fails with a missing header while the baseline builds
fine — a local rehearsal of the exact deploy-ordering hazard. And the image
has an ENTRYPOINT, so the sweep needs `--entrypoint python3` or the command
becomes arguments to the HTTP server and sits idle forever.

Still not done: real `arm-none-eabi-size` flash measurements (§5 below), and
the whole website side.

---

## 2. Attribution — Q1 RESOLVED

Lyle's call: over-index on crediting Mutable, keep dual copyright in the
licence. The precedent that settles it is already in the tree — the **stock
Plaits engines gained a fourth `ApplyMacro` macro** they were never designed
with (`grain_engine.cc`, `fm_engine.cc`, and eight more) **and kept
`origin: "Mutable Instruments"` / `author: "Emilie Gillet"`.** The Braids ports
are the same situation: Émilie's algorithm plus a Rubato fourth macro. So they
are treated exactly like Plaits rather than differently from it.

Uniform across all twelve:

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

### 3.9 A 15th registration step

The spec's 14-step per-engine checklist misses
`alt_firmwares/plaits_lab_builder/test_generate_engine_config.py`, which pins
`len(CATALOG)`. It has to move with every engine or the builder suite fails.

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

### brass -- STILL NOT BUILT, and here is exactly how far it got

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

**Where to resume.** Attempt 4's prototype is the one to rebuild. The open
question is bounding the flow term: the lip opening was clamped at `3*x0` but
the runaway comes through `u = opening * sqrt(|pd|)` with `pd` unbounded, so the
next thing to try is clamping `pd` (physical: mouth pressure cannot exceed what
the player supplies) and searching `zc` an order of magnitude finer near where
oscillation starts. A sweep of threshold-of-oscillation vs mouth pressure would
locate that window directly instead of grid-searching blind.

**Scope honestly.** This is a Rubato Lab engine after Cook, not an STK port, and
it is DSP research rather than transcription. The gap it fills is real and still
open: the catalog has a reed (`reed-pipe`) and a bow (`bowed`) and no lip, and a
lip valve is not a reed -- it is outward-striking and can be tuned *away* from
the bore, which is what lipping and overblowing are.
