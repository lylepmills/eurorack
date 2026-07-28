# Braids → Plaits Palette port: consolidated implementation specification

**Status:** implementation-ready, pending the five open questions in §8.
**Date:** 2026-07-28
**Scope:** merges five design groups (analog-core, feedback-fm, physical, formant, hybrid) and their
adversarial critiques into one plan. Every `needs-changes` requirement from every critique has been
applied or explicitly deferred to Lyle with a reason. Two engines are dropped (§7).

**Source of truth for the port:** `/…/scratchpad/braids-upstream/braids/`
**Port target:** `/…/scratchpad/lyle-eurorack/`, engines land in `plaits/dsp/engine2/`.

---

## 0. Contents

1. Summary table of final engines
2. Cross-cutting rules (read before writing any engine)
3. Per-engine specifications
4. Shared modules — decision and rationale
5. Implementation order and parallelism
6. Flash budget
7. Rejected candidates
8. Open questions for Lyle
9. Risks

---

## 1. Summary table of final engines

Twelve engines survive, covering **19 Braids models**. Nine are unconditional; three are gated.

| # | id | Name | Braids ancestry | Family | Est. flash | Verdict |
|---|----|------|-----------------|--------|-----------:|---------|
| 1 | `z-filter` | Z Filter | ZLPF, ZPKF, ZBPF, ZHPF (4 models) | Subtractive | 2,200 B | **Build first** — best value in the set |
| 2 | `bowed` | Bowed | BOWD | Physical | 2,400 B | Build — no palette neighbour |
| 3 | `toy` | Toy | TOY* | Bitwise | 1,520 B | Build — cleanest spec received |
| 4 | `csaw` | CSaw | CSAW | Subtractive | 1,400 B | Build |
| 5 | `sub-oscillator` | Sub Osc | SUB↓, SUB↑ (2 models) | Subtractive | 1,300 B | Build |
| 6 | `ring-mod` | Ring Mod | RING | Digital | 1,700 B | Build |
| 7 | `digital-modulation` | Digital Modulation | QPSK / digital modulation | Digital | 1,620 B | Build — after the dead-HARMONICS fix |
| 8 | `vowel-fof` | Vowel FOF | VFOF | Vocal | 3,100 B | Build — after 5 correctness fixes |
| 9 | `saw-comb` | Saw Comb | (saw → comb hybrid) | Feedback | 3,000 B | Build |
| 10 | `raw-fm` | Raw FM | FM, FBFM, WTFM (3 models) | FM | 1,450 B | **Gated** — Lyle go/no-go, §8 Q2 |
| 11 | `fluted` | Fluted | FLUT | Physical | 2,600 B | **Gated** — on a measurement, §3.11 |
| 12 | `triple` | Triple | ⌐⌐x3, saw x3, /\x3, SIx3 (4 models) | Polyphonic | 2,800 B | **Gated** — Lyle duplication call, §8 Q3 |

**Unconditional nine: 18,240 B (17.8 KB). All twelve: 25,090 B (24.5 KB).** See §6 for what that means.

**Dropped:** `vosim` (VOSM), `wave-paraphonic` — see §7.

---

## 2. Cross-cutting rules

These resolve conflicts that appeared independently in three or more groups. **They override any
per-engine text below.** Where a group design said otherwise, the group was wrong.

### R1 — Output bounding and the limiter (fixes a defect found in 7 of 14 designs)

Four separate designs asserted "bounded to ±1.0 by construction, so no limiter is needed" **while the
same document mandated a DC blocker**. That reasoning is invalid. Verified at `plaits/dsp/voice.h`:

```cpp
if (gain < 0.0f) { limiter_.Process(-gain, in, size); }
const float post_gain = (gain < 0.0f ? 1.0f : gain) * -32767.0f;
```

A **positive** `out_gain`/`aux_gain` takes no limiter path at all — it goes straight to `Clip16`.
A **negative** gain is the only way to engage `stmlib::Limiter`.

**Rule:** any engine that (a) contains a DC blocker, (b) contains a feedback loop, or (c) cannot pin
its post-blocker peak analytically **must use negative gains**. Only an engine whose output is a
bounded expression of bounded terms *after* every processing stage may use a positive gain.

Concrete failures this fixes: `raw-fm` WTFM at −0.59 FS DC → post-blocker 1.59 × 0.8 = **1.27**;
`digital-modulation` AUX preamble → −2 × 0.7 = **−1.4**. Both hard-clip as originally specified.

### R2 — `SoftClip`, not `SoftLimit`, for output safety

`stmlib::SoftLimit(x) = x(27+x²)/(27+9x²)` is asymptotic to `x/9` — **unbounded**. It crosses the
host test's `|sample| > 4.0` abort at an input of ~36. `stmlib::SoftClip` is the bounded one and is
what `reed_pipe_engine.cc:165` already uses in this role.

**Rule:** the final safety stage is `SoftClip` or `CONSTRAIN`. `SoftLimit` may only be used *inside*
a loop where asymptotic saturation is the musical intent, never as the output bound.

### R3 — Make-up gain goes INSIDE the clip

`1.6f * SoftClip(x)` emits ±1.6. Every in-tree engine puts the gain inside:
`reed_pipe_engine.cc` → `0.58f * SoftClip(dc_out)`, `0.9f * SoftClip(2.8f * dc_aux)`.

**Rule:** write `SoftClip(gain * x)`, never `gain * SoftClip(x)`, unless `gain < 1`.

### R4 — Frequency ceilings are expressed at the OUTPUT rate

`ring-mod` proposed clamping its increments to 0.45 **at a 96 kHz internal rate** — that is 43.2 kHz,
0.9 × the output rate, 3.6× `plaits::kMaxFrequency`. It aliases catastrophically after decimation.

**Rule:** every phase increment is clamped so that, **expressed at the 48 kHz output rate**, it does
not exceed `plaits::kMaxFrequency` (0.25). For an oversampled engine, clamp the internal increment to
`kMaxFrequency / oversampling_factor`. Where Braids itself clamped tighter (RING clamps all three
increments to MIDI 128 = 13.29 kHz via `ComputePhaseIncrement`), use Braids' own ceiling.

Note `plaits::NoteToFrequency` already does `CONSTRAIN(midi_note - 9, -128, 127)`, capping at
0.44 fs — so a `0.45f` clamp is dead code and must not be written as if it were a guard.

### R5 — Sample-rate reasoning must be redone per model, never transplanted

Braids runs at 96 kHz (`braids.cc:141`, `sys.Init(F_CPU / 96000 - 1, true)`) — **but not every
render function is a 96 kHz algorithm**. A function ending in `size -= 2` is a 2× unrolled loop
writing a 96 kHz stream from a **48 kHz** inner algorithm; its constants transfer verbatim.

Verified in `digital_oscillator.cc`:

| Function | `size -= 2`? | Inner rate | Consequence |
|---|---|---|---|
| `RenderBowed` (:1196) | yes (:1285) | 48 kHz | `delay_ >> 1`, `- (2<<16)` transfers verbatim |
| `RenderFluted` (:1367) | **no** | 96 kHz | `delay_ << 1`, `- (2<<16)` → **48 kHz equivalent is −1.0 sample** |
| `RenderDigitalFilter` | yes | 48 kHz | but the port oversamples 2× deliberately |

This single distinction is the origin of Fluted's ~37-cent pitch error (§3.11) and of Bowed's
inverted output-filter reasoning (§3.2). **Every rate constant in every engine below has been
re-derived from which side of this line its source function falls on.**

### R6 — `kCorrectedSampleRate`, and the reciprocal-of-`NoteToFrequency` idiom

`plaits::NoteToFrequency(note)` returns **cycles per sample**, not Hz. Therefore:

- delay in samples = `1.0f / NoteToFrequency(note)` — **never** `kCorrectedSampleRate / NoteToFrequency(...)`,
  which is wrong by fs² (~9.5e6 samples). The `saw-comb` design contained both forms in different
  sections; the reciprocal form is the only correct one.
- Seconds → samples uses `kCorrectedSampleRate` (47872.34 f, `dsp.h:47`), **not** `kSampleRate`
  (48000.0f). Pitch derives from the corrected constant because the I²S clock is a divider.

### R7 — Anti-aliasing: state a measured number or state the residue

No design may claim a stopband it has not derived. Two claims were overstated by 15–25 dB.

**Rule:** every `antiAliasingPlan` must either (a) give the filter's transfer magnitude at the
specific fold frequencies that matter, or (b) explicitly declare an accepted residue and why.
Measurements of *Braids at 96 kHz* are not measurements of *the port at 48 kHz* and may not be
substituted (this was Fluted's entire aliasing argument).

Reference numbers derived for this document:

| Filter | Use | Response |
|---|---|---|
| `[-1,0,9,16,9,0,-1]/32` (7-tap halfband) | rejected for `ring-mod` | −6.0 dB @ 24 k, −11 dB @ 28.8 k, −25 dB @ 36 k, −54 dB @ 43.2 k |
| 15-tap halfband (4 non-zero mults) | **use for `ring-mod`** | ~−70 dB stopband |
| `[0.25,0.5,0.25]` | accepted for `z-filter` | null @ 48 k, −16.7 dB @ 36 k; costs −1.4 dB @ 12 k, −6.0 dB @ 24 k |
| `lut_4x_downsampler_fir` (existing) | `toy` | 8-tap overlap-add, unity DC gain |

### R8 — Braids' int8 quantizer: one uniform, *declared* deviation

Braids' `>>8`-into-`int8` is an arithmetic shift (**floor**) and the store **wraps** on overflow.
Both `bowed` and `fluted` specified truncate-toward-zero + saturate while claiming bit-identity.

**Rule (uniform):** use **floor** semantics (matching Braids) and **saturate** rather than wrap
(deviating from Braids). Rationale: wrap inside a float feedback loop is a hard stability hazard
with no analogue to Braids' int32 accumulator bounds; floor costs nothing and removes a real
asymmetry. **Delete every "bit-identical"/"bit-for-bit" claim from both engines** — replace with
"voiced reproduction with three declared deviations" (floor+saturate, `SoftClip` for `CLIP`, and the
per-engine items listed in §3).

### R9 — DC blockers are inlined per engine, never shared

Seven engines need a one-pole DC blocker. **Do not create a shared `dc_blocker.h`.**

Reasons, in priority order:
1. **Precedent** — `AttractorEngine` inlines its own. Follow it.
2. **The `voice.h` include bug** (commit `7d63cf2`): `voice.h` used `DelayLine` and
   `HysteresisQuantizer2` via transitive includes from a *few* engines, so any hosted-builder palette
   omitting those engines failed with `compiler_failed`. A shared header that only some engines pull
   in is exactly this hazard class.
3. The digest argument that the original groups gave is **not** a valid reason and should be dropped
   from all prose: `package_digest` hashes only `source.header + source.files`, so a shared
   `dc_blocker.h` would be unhashed — but so are `sine_oscillator.h` and `4x_downsampler.h`, which
   these engines already depend on. The conclusion is right; the stated reasoning was inconsistent.

Same rule for the 2× decimators: `ring-mod` and `z-filter` each inline their own coefficient block.
They deliberately use **different** filters (R7) because their requirements differ.

### R10 — Catalog record completeness (fixes a gap in 13 of 14 designs)

Every design omitted at least one required field. `validate_catalog.py` requires `source.member`
(L92-94) and `packageId` (L83-86); `plaits_lab.py:390` enforces
`origin ∈ {"Mutable Instruments", "Rubato Lab", "Community"}` — **`origin: "Braids"` is a hard schema
violation**, and the claim that "the validator does not type-check origin" was generalised from
`validate_catalog.py` alone.

**Default resolution for all twelve engines** (pending §8 Q1):

```
author:    "Lyle Mills"
origin:    "Rubato Lab"
packageId: "rubato/<id>"
tags[0]:   "experimental"     ← every existing Lab engine leads with this
tags[1]:   "braids"
license:   MIT, carrying BOTH Emilie Gillet's and Lyle Mills' copyright lines
           in LICENSE and in every source file, plus a matching SPDX tag.
```

Rationale for `Rubato Lab` over `Mutable Instruments`: none of these is a faithful reproduction.
Every one carries declared deviations (R8), a fourth macro that Braids has no concept of, and
re-derived rate constants. Attributing them to Emilie overstates her authorship of the result while
the MIT dual-copyright header gives her correct credit for the algorithm. See §8 Q1.

### R11 — No "centre detent" language

`MORPH` is a plain pot. Only the FREQUENCY pot in freqlock mode has a findable noon, and that is what
feeds `parameters.macro` (`voice.cc:195`). Three designs described MORPH positions as detents.
**MACRO has a detent; MORPH does not.** `ApplyMacro` at `macro == 0.5` returns the stock value
exactly (`engine.h`, verified: the `macro < 0.5f` test is false at 0.5, so the else branch's
`(amount - 1)` term is zero), and `voice.cc:195` defaults `p.macro` to 0.5f — so every
"centre-detent-is-stock" claim about **MACRO** is mechanically sound and every one about MORPH is
fiction.

### R12 — CPU intuition (two designs got this backwards)

The SDK README records, from hardware: *"table lookups are cheap here — the cheapest thing measured,
around 1 cycle per instruction. Replacing a LUT with polynomial arithmetic to avoid memory made an
engine slower, not faster."* Two designs argued a per-sample `VDIV` was "cheaper than the table".
It is not; a non-pipelined `VDIV` is among the most expensive single ops in the loop.

**Rule:** drop a LUT for **flash** reasons only, and say so. Never claim a CPU win for it. Host CPU
numbers are worthless (`check --full` once reported "0.6×" for an engine running at 281 % of budget).
Order of authority: `check --full` < `qemu/estimate.py --sweep` (±14 %) < `build --hardware --cpu-probe`.
**Every engine below requires a `--sweep` run before merge and a `--cpu-probe` before release.**

### R13 — Stereo pattern selection

- **Pattern A** (no `stereo_config.h`, no makefile, no `container_server.py` entry): `stereo_capable()`
  returns literal `true`, no extra code, OUT/AUX already decorrelated **at matched gain**. Precedent:
  `pulsar`, `attractor`, `phase-distortion`, `spectral-spiral`, `string-machine` — verified absent
  from `PLAITS_STEREO_MODELS`.
- **Pattern B** (four files must agree): a real second render path. `stereo_config.h` +
  `stereo_capable() { return PLAITS_STEREO_<ID>; }` + `if (PLAITS_STEREO_<ID> && parameters.stereo)`
  on **every** branch + `plaits/makefile` `PLAITS_STEREO_MODELS` + `container_server.py` `STEREO_MACROS`.
  Missing the last one means the hosted builder can never turn it off and every mono recipe pays the flash.

`voice.cc:362` substitutes `out_gain` for `aux_gain` in stereo mode. Consequences both designs got wrong:
- The self-imposed constraint "`auxGain` must equal `outGain` because in stereo they are L/R" is **not
  real** — `aux_gain` is simply unused in stereo. `swarm` ships −3.0/1.0. Set the mono AUX gain on
  its own merits.
- A Pattern-B engine that pans each of N components with `StereoPanGains` (L²+R²=1 per component) is
  **~3 dB quieter per channel than mono** and the same `out_gain` applies. State it or compensate ×√2.

### R14 — RNG draw order is part of the sound

`glisson` deliberately draws its stereo pan randoms only inside `if (stereo)` so the mono render
consumes an unchanged sequence. **Rule:** any stereo-only random draw goes inside the stereo branch.

### R15 — Memory

`plaits.cc:139` gives every engine the **same** 16,384-byte arena, and `voice.cc:41-45` calls
`allocator->Free()` before each `Init`. Consequences:
- "Does not monopolise the arena" is a **non-reason** (`saw-comb` used it) — every engine gets all of it.
- The real consequence, which no design drew: **another engine's buffers alias yours at the same
  addresses**, so `Reset()` **must** zero every delay line / accumulator on engine switch. `saw-comb`'s
  argument that clearing 4096 taps is "a ~4000-cycle spike for no musical gain" is **wrong and must be
  reversed** — it is mandatory.
- `Allocate<T>(n)` returns NULL silently when exhausted. Never dereference unchecked.
- Size scratch off `kMaxBlockSize` (24), never `kBlockSize` (12).

### R16 — Family assignment (resolves cross-group inconsistency)

Families are free text but must come from the in-tree bucket set. Final assignment, chosen to avoid
piling four new engines into one bucket:

`z-filter` → Subtractive · `csaw` → Subtractive · `sub-oscillator` → Subtractive ·
`triple` → **Polyphonic** (three voices at musical intervals; sits with `chords`, `string-machine`) ·
`ring-mod` → Digital · `raw-fm` → FM · `toy` → **Bitwise** (with `tapfield`) ·
`digital-modulation` → Digital · `bowed` → Physical · `fluted` → Physical · `vowel-fof` → Vocal ·
`saw-comb` → **Feedback** (with `loopback`, `spectral-spiral`).

### R17 — id / member collision check (clean)

No proposed id collides with the 39 existing catalog ids. Member names verified distinct from all 39
(note `fm_engine_` exists → `raw-fm` uses `raw_fm_engine_`). Stereo macros:
`Z_FILTER`, `SUB_OSCILLATOR`, `RING_MOD`, `RAW_FM`, `DIGITAL_MODULATION`, `VOWEL_FOF`, `SAW_COMB`,
`BOWED`, `FLUTED`, `TRIPLE`, `CSAW`, `TOY` — all unique.

### R18 — `estimatedFlashBytes` is not a schema field

It exists nowhere in the repo (grep found zero consumers). Keep the estimates **in this document
only**; never add the key to `catalog.json`. All numbers in §1 and §6 are **unmeasured** and must be
replaced by an `arm-none-eabi-size` delta before any preset eviction is decided (§8 Q4).

---

## 3. Per-engine specifications

Common to all: `Init(BufferAllocator*)` calls `Reset()`; `Reset()` zeroes all state (R15);
`LoadUserData` is an inline no-op; `DISALLOW_COPY_AND_ASSIGN` is the last private member;
C++98 only (no `auto`, no range-for, no `nullptr`, `stdint.h` not `<cstdint>`); no libm
(`plaits::Sine`/`SineNoWrap`, `stmlib::SemitonesToRatio`, `stmlib::Sqrt`); no dynamic allocation.

Trigger semantics: `TRIGGER_UNPATCHED` (2) means no jack; detect a strike with
`parameters.trigger & TRIGGER_RISING_EDGE`.

---

### 3.1 `z-filter` — **Z Filter** (Subtractive)

**Braids ancestry:** ZLPF, ZPKF, ZBPF, ZHPF — `DigitalOscillator::RenderDigitalFilter`, four display
models that differ by two `kPhaseReset` entries and two branches. **Four models per palette slot: the
best flash value in the set. Build this first.**

**Source:** `plaits/dsp/engine2/z_filter_engine.{h,cc}` · class `ZFilterEngine` · member `z_filter_engine_`

#### Parameter mapping

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Model` | `HysteresisQuantizer2(4, 0.05f, false)` → {LP, PK, BP, HP} in Braids' display order (`settings.cc`: ZLPF, ZPKF, ZBPF, ZHPF). PK occupies the plateau centred at 0.375. |
| TIMBRE | `Cutoff` | `shifted_pitch = note + (-8.0f + 128.0f * timbre)` semitones, range −8…+120 st |
| MORPH | `Resonance` | window/pulse balance; `balance = 1.0f - fabsf(2.0f * morph - 1.0f)` (exact float equivalent of Braids' `~p1 << 2` uint16 wrap — verified 0 → 65532 → 0 with the peak at `p1 = 16384`) |
| MACRO | `Bend` | envelope shape bend, see below |

**Internal rate: 2×** (96 kHz), decimated by `[0.25, 0.5, 0.25]`.

```cpp
// per block
const float f0 = NoteToFrequency(parameters.note) * 0.5f;      // ← 2x internal rate. MUST be stated;
                                                                //   omitting the 0.5 renders an octave high.
float mod_increment = NoteToFrequency(shifted_pitch) * 0.5f;
mod_increment = min(mod_increment, 0.13f);                      // == Braids' 16383 pitch clamp at the internal rate
```

#### ApplyMacro

```cpp
// stock = 1.0 (linear window). MACRO bends BOTH burst envelopes toward w^4.
const float bend = ApplyMacro(1.0f, 1.0f, 4.0f, parameters.macro);
```

`macro = 0.5` → `bend = 1.0` → exactly Braids. Below noon MACRO is inert by design (min == stock);
above noon the window mean falls from 1/2 toward 1/5.

#### The zhuzh

Braids welded the burst envelope to a linear ramp. MACRO reparameterises it as `w^bend`, sweeping the
formant burst from Braids' flat, buzzy window toward a sharply front-loaded one — the difference
between a resonant filter ping and a plucked, percussive one, on the same four models. HARMONICS
becoming a *continuous* model selector (rather than four separate front-panel models) means the LP/PK
crossfade region is now a reachable timbre.

#### Blockers fixed from the critique

1. **MEMORY-SAFETY (blocker).** `modulator_phase_` and `square_phase_` accumulate freely. Braids'
   `uint32` wraps for free (`digital_oscillator.cc:348-352`); the float port does not. At the top of
   TIMBRE (+120 st) the modulator advances **2¹⁰ = 1024 cycles** per carrier period; `square_phase`
   reaches ~512. `SineNoWrap` is `Interpolate(lut_sine, phase, 512.0f)` into `lut_sine[641]` and is
   documented unsafe for `phase >= 1.25`. **Result: an out-of-bounds flash read at ~index 500,000.**
   *Fix:* wrap both phases every sub-sample — `if (p >= 1.0f) p -= 1.0f;` (one subtract suffices, the
   increment is clamped to 0.13) — or use `Sine()` which wraps internally. The original guidance
   ("check after every reset assignment, since `kZPhaseReset` values are all < 1") addressed the wrong
   hazard.
2. **Carrier increment now stated explicitly** (`× 0.5f`), see above.
3. **`integrator_gain` moved into the sub-sample loop.** Braids recomputes it inside the loop
   (`:351`) so it tracks the glide. `integrator += pulse * 4.0f * mod_increment` with a
   *block-constant* increment is wrong on the assigned model; drive it from the interpolated
   per-sub-sample increment. (`4.0f * mod_increment` is the exact float equivalent of `>>14` then `>>16`.)
4. **`postProcessing` justification rewritten.** The fixed gain is fine, but *not* because level is
   invariant — MACRO moves it ~8 dB. The reason is that nothing clips: bounded window × bounded sine,
   integrator explicitly clamped, no DC blocker, no feedback → **R1 permits a positive gain here.**
5. **Decimator droop declared:** `[0.25,0.5,0.25]` costs −1.4 dB @ 12 kHz and −6.0 dB @ 24 kHz on top
   of the intended 48 kHz null. Accepted (R7b) — the residual grit near Nyquist is the model's
   character and the alternative costs flash this engine does not need to spend.
6. **Flash re-estimated 1,550 → 2,200 B** (four hoisted branch variants, two bend polynomials, a
   3-tap decimator, a `HysteresisQuantizer2`).
7. **Declared deviation:** the port reads `lut_sine` at 512 points/period; Braids reads `wav_sine`
   (257 entries) with an 8-bit index = 256 points/period. Cleaner than the original; declared.
8. **Cross-group action resolved:** no sibling group is speccing ZLPF/ZBPF/ZHPF separately — this
   merged engine is the single owner. (The original spec asserted a coordination dependency it could
   not enforce; it is now moot.)

**Verified correct, do not re-litigate:** `shifted_pitch` range; `kPhaseReset {0, 0.5, 0.25, 0.5}`;
the double reset test `(phase_<<1) < (phase_increment_<<1)` firing at both 0 and 0.5; the
`filter_type & 2` split; the single `(pulse + integrator) >> 1` line that makes PK; `Mix` semantics;
the integrator-overflow trap (34,133 @ 96 k, 68,267 @ 48 k); the block-rate glide being identical
(24 samples @ 96 kHz both sides = 250 µs); the decimator's −16.7 dB @ 36 kHz.

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": 0.8, "auxGain": 0.8 }
```
Positive gain permitted (R1: no DC blocker, no feedback, analytically bounded).

**Stereo: Pattern A.** OUT is the assigned model, AUX the complementary one (LP↔HP, PK↔BP) at matched
gain — inherently decorrelated, no extra code. No `stereo_config.h` / makefile / `container_server.py` entry.

#### Manual

```json
"z-filter": {
  "controls": {
    "harmonics": "Selects the lowpass, peaking, bandpass and highpass filter models.",
    "timbre": "Sweeps the filter cutoff from below the note to ten octaves above it.",
    "morph": "Balances the raw pulse against the resonant ring, peaking at noon.",
    "macro": "Bends the burst envelope from the flat stock ramp toward a sharp percussive attack."
  },
  "trigger": "Restarts the internal envelope when TRIG is patched."
}
```

---

### 3.2 `bowed` — **Bowed** (Physical)

**Braids ancestry:** BOWD — `DigitalOscillator::RenderBowed` (`:1196`). **`size -= 2` at `:1285`
confirms this is a 48 kHz algorithm** (R5), so `kBridgeLPGain` and the biquad coefficients transfer
verbatim. No continuous stick-slip friction voice exists anywhere in the 39-engine catalog.

**Source:** `plaits/dsp/engine2/bowed_engine.{h,cc}` · class `BowedEngine` · member `bowed_engine_`

#### Parameter mapping

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Bow position` | `bridge_delay = 0.0234375f * (1.0f/NoteToFrequency(note) - 2.0f)` … `0.2695f * (…)` across the knob (Braids' `parameter_1 = 6 + (COLOR>>9)` in [6,69], /256) |
| TIMBRE | `Bow pressure` | `friction_scale = parameter_0 / 32`, `parameter_0 = 172 - (TIMBRE>>8)` ∈ [45,172] |
| MORPH | `Nut damping` | Braids hardcodes `-nut_value`; MORPH opens it as a continuous ±range |
| MACRO | `Body` | body-filter pole reparameterisation, below |

The `-2.0f` term is correct here (it is Braids' `- (2<<16)` on an already-48 kHz `delay_ >> 1`).
Contrast Fluted, §3.11.

#### ApplyMacro

```cpp
// Stock biquad, inverted from Braids' literals:
//   r_stock = sqrt(2959/4096) = 0.84995 ;  cos(theta) = 1.6962891/(2r) = 0.99784
//   theta   = 0.06578 rad = 502.5 Hz at 48 kHz ;  Q ~ 0.22 ;  numerator 1 - z^-2
const float body_note = ApplyMacro(kBowedStockBodyNote, kBowedStockBodyNote - 12.0f,
                                   kBowedStockBodyNote + 12.0f, parameters.macro);
const float theta = 6.2831853f * NoteToFrequency(body_note);
const float r     = 0.84995f;
// a1 = -2*r*SineNoWrap(theta*(1/TWO_PI) + 0.25f); a2 = r*r
```

At `macro = 0.5` every coefficient reduces to Braids'. **Declared approximation:**
`2*r*SineNoWrap(theta + 0.25)` computes 1.696219 against Braids' `6948/4096 = 1.6962891` — close, not
exact. Do not call it bit-identical (R8).

#### The zhuzh — honestly scoped

MACRO sweeps a ±1-octave body resonance that Braids welded to one constant, and MORPH opens the
hardcoded nut damping. **Copy must be toned down:** at Q ≈ 0.22 the bandwidth is ~764 Hz around a
167 Hz centre. That is a **broad spectral tilt**, not a resonant body. Do **not** ship "turns BOWD
from one violin into a family… cello and bass-viol weight… small nasal fiddle". Ship: *"tilts the
instrument body darker or brighter around the stock resonance."* The MORPH nut-damping extension is
the genuinely new expressive axis.

#### Blockers fixed from the critique

1. **DELAY UNDERFLOW (blocker).** `plaits::DelayLine::Read(d)` is only meaningful for `d >= 1`;
   `Read(0)` returns the **oldest** slot. `bridge_delay = 0.0234375 * (1/f0 - 2)` reaches 1.0 at
   f0 ≈ 1074 Hz, so at HARMONICS = 0 the bridge tap goes degenerate from ~**MIDI 85** upward, and at
   `NoteToFrequency`'s ceiling both taps go sub-sample and read stale buffer straight into a feedback
   loop. Neither in-tree test would catch it (`ValidateExperimentalEngineExtremes` uses note 60,
   `ValidateExperimentalControlResponse` uses note 48).
   *Fix, mirroring `ReedPipe`'s `CONSTRAIN(target_delay, 4.0f, kReedPipeDelaySize - 4.0f)`:*
   ```cpp
   // AFTER the octave-fold halving loop; guard the loop against a non-finite period.
   CONSTRAIN(bridge_delay, 2.0f, 508.0f);
   CONSTRAIN(neck_delay,   4.0f, 2044.0f);
   ```
   **Add a regression scenario at MIDI 96 with HARMONICS = 0** — the extremes sweep will not find this.
2. **Output one-zero: reasoning was inverted; the filter is replaced.** Braids'
   `*buffer++ = (out + previous) >> 1; *buffer++ = out;` is a **2× linear-interpolating upsampler**
   writing 96 kHz; the underlying 48 kHz samples pass **unfiltered** as the odd output. Its baseband
   effect is the interpolation kernel `(1 + cos(2πf/96000))/2` = **−1.4 dB @ 12 kHz**, −6 dB @ 24 kHz.
   Re-implementing it as `0.5*(x[n] + x[n-1])` at 48 kHz gives **−3.0 dB @ 12 kHz plus a hard null at
   24 kHz** — roughly double the attenuation and a Nyquist null Braids does not have, i.e. *darker*
   than BOWD, the opposite of the fidelity goal. (The originally quoted −3.9 dB matches neither.)
   *Fix:* replace with the correct 48 kHz equivalent — a one-pole with coefficient ≈ 0.15/0.85 giving
   ~−1.4 dB @ 12 kHz — and rewrite the `antiAliasingPlan` paragraph.
3. **Make-up gain moved inside (R3):** `1.6f * SoftClip(x)` → `SoftClip(1.6f * x)`. As written the
   engine emits ±1.6, and `SoftClip` is near-linear at the real −15 dBFS operating level so it
   provided no protection at all.
4. **Delay-line sizing corrected.** "Growing the bridge line does not move that floor" is false: at
   HARMONICS = 1 the **bridge** binds (0.2695 × period ≤ 508 → 25.5 Hz) while the neck only binds at
   17.2 Hz. *Fix:* size `bridge_` as `DelayLine<float, 1024>` → total 12,288 B, comfortably inside the
   16,384-byte arena. That moves the worst-case fold floor from ~25.5 Hz to ~17.2 Hz. Braids folds at
   ~11.4 Hz; the port still folds earlier over part of the HARMONICS range and **that residual must be
   stated** (as specified originally it left MIDI 21 only two semitones of margin).
5. **R8 applied:** floor + saturate, `SoftClip` for `CLIP`, bit-identity language deleted.
6. **R12 applied:** the `lut_bowing_friction` → `min(1, 1/(d+0.75)^4)` substitution keeps its **flash**
   justification (~2,018 B of tables dropped) and **loses** the "three multiplies and one VDIV is
   cheaper than the table" claim, which contradicts hardware measurement. The unverified
   "~50-70 cycles/sample" figure is deleted pending `--sweep` and `--cpu-probe` (R12).

**Verified correct, do not re-litigate:** `parameter_0 = 172 - (TIMBRE>>8)` ∈ [45,172] and
`parameter_1 = 6 + (COLOR>>9)` ∈ [6,69]; the `friction_scale = p0/32` derivation chained through
`>>5`, the `(1<<17)-1` saturation and the table's `x = i/64` axis; `lut_bowing_friction = min(1, 1/(d+0.75)^4)`;
the bowing envelope's 600/120-step structure and its **once-per-four-output-samples** advance (the
25 ms → 50 ms trap is real and is this port's single most important rate correction); the biquad
inversion; the float-domain scaling (±32768 via `<<8`).

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": -0.85, "auxGain": -0.85 }
```
**Negative (R1):** feedback loop + in-loop nonlinearity ⇒ no analytic bound.

**Stereo: Pattern A.** OUT = bridge pickup, AUX = neck pickup, already decorrelated at matched gain
(same idiom as `reed-pipe`'s two taps on one bore). No gated files.

#### Manual

```json
"bowed": {
  "controls": {
    "harmonics": "Moves the bow from near the bridge to well up the neck.",
    "timbre": "Increases bow pressure from a thin whistle to a hard scrape.",
    "morph": "Opens the nut damping from tight and choked to loose and ringing.",
    "macro": "Tilts the instrument body darker or brighter around the stock resonance."
  },
  "trigger": "Restarts the bow stroke and clears the string."
}
```

---

### 3.3 `toy` — **Toy** (Bitwise)

**Braids ancestry:** TOY* — `DigitalOscillator::RenderToy` (`:285-317`). **The cleanest design
received; no blockers.** Nothing in the 39-engine catalog does bit-depth reduction or sample-rate
decimation (`chiptune` is NES square/triangle + arp; `tapfield` is Galois LFSR wavefields;
`rulefield` is CA) — genuinely novel to the palette.

**Source:** `plaits/dsp/engine2/toy_engine.{h,cc}` · class `ToyEngine` · member `toy_engine_`

#### Parameter mapping

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Mangle` | `x = p1 >> 8` ∈ [0,127] driving `(((phase>>24) ^ (x<<1)) & (~x)) + (x>>1)` |
| TIMBRE | `Crush` | bit-depth reduction |
| MORPH | `Clock` | hold-clock source: free-running ↔ note-locked, see zhuzh |
| MACRO | `Fold` | mangle-operand bias |

Internal 4× oversampling decimated by the **existing** `lut_4x_downsampler_fir` (8-tap overlap-add,
unity DC gain — genuinely better than Braids' 4-tap non-overlapping `{10530,14751,16384,14751}`).
512 taps at 4×/96 kHz = 750 Hz matches 256 at 4×/48 kHz — verified.

#### The zhuzh

MORPH crossfades the sample-hold clock from a free-running rate (Braids' behaviour) to one that
**tracks the played note**, so the crush artefacts become harmonically locked to the pitch instead of
inharmonic grit. At MORPH = 1 the aliasing products are the timbre.

#### Fixes applied

1. **`kToyTrackReference` corrected: 182.98f → 183.45f.** `1/NoteToFrequency(60)` with `a0 = 55/48000`
   is `1/0.0054513 = 183.45`, not 182.98 — a 4.4-cent error that defeated the stated design intent
   ("the two coincide at C4 so the knob does not jump during a middle-C audition"). **Preferred:
   compute it at build time as `1.0f / NoteToFrequency(60.0f)` rather than hardcoding.**
2. **R1 applied.** The "bounded to ±1.0 by construction, so no limiter" claim is invalid while a DC
   blocker is present. In practice TOY's post-blocker excursion is small (the mangle's range shrinks
   with `x` and its mean stays near zero), so 0.85 was probably safe — but the reasoning was not.
   *Resolution:* keep 0.85 **but make it negative** per R1, and record a measured post-blocker peak
   from the audition WAV before release.
3. **MORPH-1 ↔ stereo-detune tension declared.** At MORPH = 1 the whole point is that the crush
   *locks* to the note; the right channel's 1.0293× hold clock (~50 cents) deliberately unlocks it on
   one side. That is desirable as chorus but the two features are in **direct tension at MORPH 1** —
   the stereo plan must say so and confirm it is intended (it is; the width is worth the partial
   unlock, and mono users are unaffected).
4. **`ParameterInterpolator` added to the ramp increment**, for parity with the smoothing standard
   this project imposes elsewhere. Without it a pitch CV sweep steps at block rate.
5. **Wording:** "one instance [of `Downsampler`] per output **stream**", not "per output tap" — read
   literally the original would construct four per sample.

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": -0.85, "auxGain": -0.85 }
```

**Stereo: Pattern B.** The right channel runs a second hold clock at 1.0293× — a real second render
path. Requires all four files (R13): `stereo_config.h` `PLAITS_STEREO_TOY`, header
`stereo_capable() { return PLAITS_STEREO_TOY; }`, every branch guarded by
`if (PLAITS_STEREO_TOY && parameters.stereo)`, `plaits/makefile` `TOY:toy_engine`,
`container_server.py` `"toy": "TOY"`. R14 applies to any stereo-only random draw.

#### Manual

```json
"toy": {
  "controls": {
    "harmonics": "Sweeps the bit-mangling operand from clean tone to shattered noise.",
    "timbre": "Reduces bit depth from smooth to coarsely quantized.",
    "morph": "Moves the sample-hold clock from free-running to locked to the played note.",
    "macro": "Biases the fold operand, shifting which harmonics the mangler keeps."
  },
  "trigger": "Restarts the sample-hold clock and the mangler state."
}
```

---

### 3.4 `csaw` — **CSaw** (Subtractive)

**Braids ancestry:** CSAW — `AnalogOscillator::RenderCSaw` (`:91`) plus `MacroOscillator::RenderCSaw`
(`:50`), the emblematic Braids waveform.

**Source:** `plaits/dsp/engine2/csaw_engine.{h,cc}` · class `CSawEngine` · member `csaw_engine_`

#### Parameter mapping

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Depth` | discontinuity depth `d`; `discontinuity_depth_ = -2048 + (aux_parameter_ >> 2)` |
| TIMBRE | `Width` | `pw = parameter_ * 49152`, floored at `8 * phase_increment` |
| MORPH | `Bend` | saw-segment bend |
| MACRO | `Tilt` | segment tilt, below |

#### The DC constant — derivation corrected

The value **0.1016 is right; the published derivation was wrong** and would have been re-derived
incorrectly by an implementer, landing 4.2 dB low and producing exactly the HARMONICS-sweep pumping
the spec warned against.

Verified in `macro_oscillator.cc:59-63`:
```cpp
int16_t shift = -(parameter_[1] - 32767) >> 4;      // max 2047
while (size--) { int32_t s = *buffer + shift; *buffer++ = (s * 13) >> 3; }
```
The shift is added **before** the ×13/8 make-up. Therefore

> **DC = 1.625 × 2047 / 32768 = 0.10151.**

Not `2047/16384 × 0.5 = 0.0625`.

#### Parameter rate — ONE story (the contradiction resolved)

The original listed `ParameterInterpolator` targets for `depth_`, `width_`, `bend_`, `tilt_` **and**
said to compute everything once per block. Both cannot hold.

**Decision: `pw` and `bend`/`tilt` are per-sample interpolated; `d` is latched at the cycle wrap.**

- Per-sample `pw` ⇒ per-sample BLEP step heights ⇒ the fractional BLEP time needs a `previous_pw_`
  term: `t = (phase - pw) / (previous_pw_ - pw + frequency)` — exactly what
  `VariableShapeOscillator` and `VariableSawOscillator` both do. Without it the correction is wrong
  whenever TIMBRE moves.
- `d` is latched **only inside the wrap branch**, as Braids does
  (`self_reset = false; discontinuity_depth_ = -2048 + (aux_parameter_ >> 2)`), so the plateau level
  is constant within a cycle by construction. Updating `d` at block rate mid-plateau inserts a small
  un-BLEP'd step hardware does not have.

**State list (was incomplete):** `phase_`, `next_sample_`, `next_sample_aux_`, **`high_`**,
**`previous_pw_`**, plus the interpolator targets — for **both** the OUT and AUX paths. `high_` is the
edge latch Braids and both plaits varishape oscillators carry; without it a block where the phase
crosses `pw` and wraps, or where `pw` moves backwards past the phase, double-fires or drops a BLEP.
Follow `RenderCSaw`'s `while (...) { if (!high_) … if (high_) … }` structure.

#### ApplyMacro

```cpp
const float tilt = ApplyMacro(0.0f, -1.0f, 1.0f, parameters.macro);
```

#### The zhuzh, and its anti-aliasing cost — declared

MACRO tilts the saw segment. At `tilt = +1` the value discontinuity at `pw` **cancels**, leaving a
pure **slope** discontinuity (−12 dB/oct residue) that a value-BLEP does not correct. The same holds
at `bend = ±1` where the segment ends at slope 0 or 2.

`stmlib::ThisIntegratedBlepSample` / `NextIntegratedBlepSample` already exist and
`VariableShapeOscillator` uses them for precisely this case.

**Decision: apply integrated-BLEP to the `pw` slope discontinuity.** It is needed most exactly where
the zhuzh is most interesting (`tilt → +1`), the helpers are free, and the alternative — declaring
the residue — undercuts the engine's whole reason to exist. Budget +250 B; already in the 1,400 B
estimate.

#### AUX

The original AUX ("the same ramp without the step") is a plain bent saw — near-zero added value
against `virtual-analog` and `swarm`. **Revised AUX: the same ramp with the discontinuity *inverted*
(`-d` instead of `+d`)** — same cost, genuinely complementary, and it makes Pattern A stereo
meaningful.

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": 0.73, "auxGain": 0.73 }
```
Positive permitted (R1): fully BLEP-corrected bounded waveform, no DC blocker, no feedback. 0.73
is the group level-match anchor (see `ring-mod`).

**Stereo: Pattern A** with the revised AUX.

#### Manual

```json
"csaw": {
  "controls": {
    "harmonics": "Deepens the notch cut into each sawtooth cycle.",
    "timbre": "Moves the notch from the start of the cycle toward its end.",
    "morph": "Bends the sawtooth ramp from concave through straight to convex.",
    "macro": "Tilts the notched segment, flattening the step at one extreme and doubling it at the other."
  },
  "trigger": "Restarts the internal envelope when TRIG is patched."
}
```

---

### 3.5 `sub-oscillator` — **Sub Osc** (Subtractive)

**Braids ancestry:** SUB↓ and SUB↑ (`settings.cc` `"SUB\x8C"`, `"SUB\x88"`) — two models merged.

**Source:** `plaits/dsp/engine2/sub_oscillator_engine.{h,cc}` · class `SubOscillatorEngine` ·
member `sub_oscillator_engine_`

#### Parameter mapping

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Shape` | `mu` — 0 = square sub, 1 = comb saw; the twin-ramp mix `out = 2p - pw - sq` |
| TIMBRE | `Width` | `pw` |
| MORPH | `Sub level` | `sub_balance`; 0 at the knob's default |
| MACRO | `Sub width` | `pw_sub`, below |

#### The pw-rate contradiction — resolved

The helper signature took **scalars** `(a, b, c, pw, frequency, phase*, next_sample*, out, size)`,
while the notes insisted `pw` MUST be per-sample interpolated. Worse,
`c = 0.5f * mu * (1.0f - pw)` **depends on** `pw`, so interpolating `pw` forces per-sample `c`,
per-sample wrap and `pw` step heights, and a `previous_pw_`. As written the helper cannot do what the
notes require.

**Decision (consistent with `csaw`): interpolate `pw`.** Change the helper signature to take
`ParameterInterpolator&` for `pw` and compute `c`, both step heights and the BLEP time per sample
with a `previous_pw_` term. The block-rate zipper is a Braids bug, not character, and this is the one
place where reproducing it would be perverse.

#### ApplyMacro

```cpp
const float pw_sub = ApplyMacro(0.5f, 0.12f, 0.5f, parameters.macro);
```
Note **min == stock is deliberate** here (0.5 → 0.5): a narrowed sub below noon, stock above. This is
the shape `triple` must also adopt (§3.12).

#### Level analysis — corrected

The claim *"Both the square and the comb saw reach full scale (±1) by construction — Braids'
`(s - 16384) << 1` fills the int16 range"* is **false**.

`out = 2p - pw - sq` peaks at **`max(pw, 1-pw)`**. So at `mu = 1` (comb saw) with `pw = 0.5` the
engine outputs **±0.5**, not ±1 — the two saws are in antiphase, odd harmonics cancel, and the result
is a 6 dB-down octave-up comb. **There is a ~6 dB loudness swing across the HARMONICS × TIMBRE
plane.** 0.7 remains a safe gain, for this reason rather than the stated one.

#### DC — new section (was absent)

`DC(mu, pw) = mu + 2(1-mu)(1-pw) - 1`:
- exactly **0** at `mu = 1`
- **+0.977** at `mu = 0`, `pw = 0.0117` (98.8 % duty)
- the MACRO-narrowed sub adds a further **+0.76 × balance** at `pw_sub = 0.12`

This is faithful to Braids, and stock Plaits' VA has the same via `pw` up to 0.99. But it **will thump
the LPG at extreme TIMBRE**, and the peak analysis silently assumed a zero-mean waveform. No DC
blocker (it would change the character); the behaviour is documented instead.

#### Honest description of the TIMBRE reversal

The disclosed TIMBRE reversal stands as the right call, but the **defence was a rationalisation**.
"Both endpoints stay up = brighter… fewer comb notches for the saw" is wrong: at `mu = 1` the
`pw → 0.0117` end is where the two saws **coincide**, i.e. a full fundamental saw (fatter, lower),
while `pw → 0.5` is the octave-up antiphase comb. Calling the fundamental-saw end "brighter" is
false. Replace with: *"one end is a full-bodied fundamental saw, the other an octave-up hollow comb."*

`mu = 2/3` must also be described accurately: it is a saw at **2/3 amplitude** with a pw-dependent DC
of `1/3 - (2/3)pw`. True that `b = 0` kills the second discontinuity; **not** true that it is "clean"
or level-matched.

#### AUX discoverability

`AUX = sub * 2 * sub_balance` is **silent at MORPH = 0**, which is the knob's default. That is
defensible (it is what makes the octave crossover click-free) but `outputs[1]` labelled "Sub
oscillator alone" gives no hint. **Revised label: `"Sub only (silent at MORPH 0)"`**, and the MORPH
manual line says so.

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": 0.70, "auxGain": 0.70 }
```
Positive permitted (R1). **Stereo: Pattern A** (OUT = mix, AUX = sub) — but note the AUX-silent-at-
detent caveat means stereo is mono at MORPH 0; declared.

#### Manual

```json
"sub-oscillator": {
  "controls": {
    "harmonics": "Morphs from a square sub through a plain saw to a hollow octave-up comb.",
    "timbre": "Sets pulse width, sweeping between a full saw and a thin comb.",
    "morph": "Mixes in the sub oscillator; the AUX output is silent when fully down.",
    "macro": "Narrows the sub oscillator's pulse below noon, leaving it square above."
  },
  "trigger": "Restarts the internal envelope when TRIG is patched."
}
```

---

### 3.6 `ring-mod` — **Ring Mod** (Digital)

**Braids ancestry:** RING — a three-sine ring modulator (carrier + two detuned modulators) through a
saturating shaper.

**Source:** `plaits/dsp/engine2/ring_mod_engine.{h,cc}` · class `RingModEngine` ·
member `ring_mod_engine_`

#### Parameter mapping

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Detune 1` | modulator 1 offset |
| TIMBRE | `Detune 2` | modulator 2 offset |
| MORPH | `Depth` | `k1`, `k2` modulation depths (0 = carrier only) |
| MACRO | `Drive` | shaper drive, below |

**Internal rate: 2×** (96 kHz).

#### ApplyMacro

```cpp
const float drive = ApplyMacro(1.0f, 0.5f, 4.0f, parameters.macro);
```

#### Fixes applied

1. **Decimator replaced (blocker for the "parity" claim).** For `h = [-1,0,9,16,9,0,-1]/32`,
   `H(w) = (16 + 18cos w - 2cos 3w)/32`: **−6.0 dB @ 24 kHz, −11 dB @ 28.8 kHz (folds to 19.2 kHz),
   −25 dB @ 36 kHz (folds to 12 kHz), −54 dB @ 43.2 kHz.** The claimed "roughly 40 dB of stopband"
   overstates it by 15-25 dB **in exactly the band that matters**. Braids at 96 kHz does **not** fold
   its 24-48 kHz content at all — it leaves via the DAC — so this port would add an audible ~−25 dB
   alias floor hardware does not have, on a shaper output that is dense up there by design.
   *Fix:* use a **15-tap halfband (4 non-zero multiplies, ~−70 dB stopband)**. Budget +200 B.
   The headline "exact parity rather than improvement" claim is withdrawn either way.
2. **Increment clamps corrected (R4).** `0.45` at the 96 kHz internal rate is 43.2 kHz = 0.9 × the
   output rate, 3.6× `kMaxFrequency`, and re-opens the very aliasing the oversampling exists to close.
   Braids clamps **all three** increments to MIDI 128 via `ComputePhaseIncrement` (the modulators go
   through the same function with `pitch_ + detune`) = **13.29 kHz = 0.138 of 96 kHz**.
   *Fix:* clamp all three to Braids' own ceiling, and reconcile with the group frequency rule.
3. **Level re-derived.** `out_gain 0.9` on an analytically pinned 0.479 peak gives **0.43**, against
   0.73 (`csaw`), 0.70 (`sub-oscillator`) and 0.69 (`triple`) — ~4 dB **quieter** than its group-mates,
   the opposite of the stated intent. And the 0.25 pre-shaper peak requires all three sines to peak
   simultaneously, which inharmonic ratios essentially never do, so the practical level is lower still.
   *Fix:* **`outGain` / `auxGain` = 1.5**, giving a post-gain peak of ~0.72, level-matched to the group.
4. **MACRO level claim softened:** "peak level is invariant under MACRO" is true of the **analytic
   peak only**. Perceived loudness still rises substantially with drive — that is what saturation does.
5. **Depth-zero fast paths added.** At MORPH = 0 the engine is a single sine and was still paying 2×
   oversampling, six sine lookups and two `SoftLimit` divisions per output sample. A `k1 == 0 /
   k2 == 0` skip is free.
6. **Attack-transient deviation declared.** Dropping Braids' `1<<30` carrier offset on reset is the
   right call and was disclosed — but it must be said **in the same paragraph** that justifies the
   reset by "the three phases' relative alignment at note start determines the attack": the strike
   transient will **not** A/B against hardware.

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": -1.5, "auxGain": -1.5 }
```
**Negative (R1):** a saturating shaper plus a make-up gain above unity ⇒ engage the limiter.

**Stereo: Pattern A.** OUT = full ring product, AUX = carrier × modulator 1 only, matched gain.

#### Manual

```json
"ring-mod": {
  "controls": {
    "harmonics": "Detunes the first modulator through harmonic and clangorous intervals.",
    "timbre": "Detunes the second modulator, thickening the sideband cluster.",
    "morph": "Fades from a bare carrier to full three-way ring modulation.",
    "macro": "Drives the output shaper, from clean multiplication to hard saturation."
  },
  "trigger": "Realigns the three oscillator phases so each note starts alike."
}
```

---

### 3.7 `digital-modulation` — **Digital Modulation** (Digital)

**Braids ancestry:** QPSK / digital modulation — `DigitalOscillator` `:2172-2230`. Nothing in the
39-engine catalog does packet-framed I/Q; genuinely novel.

**Source:** `plaits/dsp/engine2/digital_modulation_engine.{h,cc}` · class `DigitalModulationEngine` ·
member `digital_modulation_engine_`

#### Parameter mapping (REVISED — the dead-knob fix)

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Frame` | **frame length** — moved here from MACRO |
| TIMBRE | `Symbol rate` | `symbol note = pitch - 12 - 32*(1 - timbre)` (`:2172`) |
| MORPH | `Shaping` | pulse shaping; at 0 the constellation is ±R constants |
| MACRO | `Payload` | **payload byte** — moved here from HARMONICS |

#### The blocker, and why the fix is structural

At `macro = 0.5` — **the default**, since `voice.cc:195` sets `p.macro = 0.5f` unless freqlock option
1 is selected — the frame is the stock 1,088 symbols and **HARMONICS is unreachable for the entire
header**. Verified from source: at MIDI 36 / TIMBRE 0 the symbol rate is ~5.15 Hz, so 1,088 symbols
is **~211 s**. The original called this "a TESTING TRAP, NOT A BUG" and mitigated it with a test
scenario at `macro = 0.1`. **That fixes the reviewer, not the user.** Shipping a panel knob that is
inert at the neutral macro position is a UX defect.

**Chosen fix (option 1 of the three offered): swap HARMONICS and MACRO.** Frame length goes on
HARMONICS — a knob with no detent obligation, always live, always reachable. The payload byte goes on
MACRO, where `ApplyMacro(kStockPayloadByte, 0.0f, 255.0f, macro)` puts Braids' exact payload at the
detent and sweeps the whole byte space either side. The stock voice remains reachable; the dead knob
is gone; no frame-length clamp hack is needed.

```cpp
const float frame_symbols = 32.0f + harmonics * (1088.0f - 32.0f);   // 32/48/64/1088 boundaries preserved
const float payload = ApplyMacro(kStockPayloadByte, 0.0f, 255.0f, parameters.macro);
```

#### AUX headroom — fixed

The mono AUX staircase is **+1.0 constant throughout the preamble** (`data_byte = 0x00` → dibit 0 →
`i = q = +R` → `(R + 0.5R)/(1.5R) = +1`), which at stock frame lengths lasts seconds. The `0.00022f`
one-pole (fc ≈ 1.7 Hz) converges `dc_aux_` to +1 within ~0.1 s. The first sync-A dibit-2 symbol then
gives −1, so the blocked output is **−2** → `−2 × 0.7 = −1.4` → hard clip (R1).

*Fix:* **negative `auxGain`** to engage `stmlib::Limiter`. (Considered and rejected: dropping the
blocker entirely — DC is arguably the point for a modulation source — but the OUT path needs it and
asymmetric handling is more surprising than a limiter.)

#### Stereo claim softened

At MORPH = 0, `shaped_i` and `shaped_q` are ±R constants, so L = ±R·sin θ and R = ±R·cos θ: **the
same sine, 90° apart**, with independent per-symbol sign flips. That is largely a **quadrature phase
relationship** (heard as phasey width), not "a genuinely different waveform per side". Only the
differing sign streams decorrelate, and only at the symbol rate. Also each channel peaks at 0.705 vs
the mono 0.997 — **~3 dB quieter per channel in stereo** (R13); declared.

#### Numeric and copy corrections

- "even at TIMBRE max about 16 seconds" does **not** follow from the stated MIDI 36 case: at MIDI 36 /
  TIMBRE max the symbol rate is 65.4/2 = 32.7 Hz and 1,088 symbols is **33.3 s**. 16 s corresponds to
  MIDI 48. (The 210 s figure **is** correct.)
- AUX is a **symbol-rate staircase** whose fundamental spans carrier/12.7 to carrier/2. At MIDI 36 /
  TIMBRE 0 that is a ~5 Hz stepped LFO — a **modulation source**, not "a genuinely useful second
  voice". It becomes a voice only at high TIMBRE and mid-to-high pitch. Copy tempered accordingly.

**Verified correct:** the constellation signs `{+,+,-,-}` / `{+,-,-,+}` at radius
23100/32768 = 0.70496; the 32/48/64/1088 boundaries; the 1/4 payload one-pole; the `strike_` handler.

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": -0.9, "auxGain": -0.7 }
```
(Asymmetric mono gains are fine — `aux_gain` is unused in stereo, R13.)

**Stereo: Pattern A** (I on L, Q on R, matched gain).

#### Manual

```json
"digital-modulation": {
  "controls": {
    "harmonics": "Shortens the packet frame from a long header to a rapid burst.",
    "timbre": "Raises the symbol rate from a slow stutter to a buzzing carrier.",
    "morph": "Shapes the symbol transitions from hard steps to smooth glides.",
    "macro": "Sweeps the payload byte around the stock packet contents."
  },
  "trigger": "Restarts the packet from its preamble."
}
```

---

### 3.8 `vowel-fof` — **Vowel FOF** (Vocal)

**Braids ancestry:** VFOF — `DigitalOscillator::RenderVowelFof`.

**Source:** `plaits/dsp/engine2/vowel_fof_engine.{h,cc}` · class `VowelFofEngine` ·
member `vowel_fof_engine_`

**The finding that justifies the engine:** `digital_oscillator.cc`'s accumulator reads
`amplitudes[0]` rather than `amplitudes[i]`, and column 0 of `formant_a_data` is 16384 in all 25 rows
— so **100 of 125 table entries have been dead since 2013**. Putting the fix on the `ApplyMacro`
detent is the rare case where fidelity and novelty coincide: detent = Braids' actual (broken) sound,
above noon = the formant amplitudes as the table always intended.

Also verified and correctly identified as droppable: all three half-rate compensations —
`phase_increment_ << 1`, the `+ (12 << 7)` octave offset (`lut_svf_cutoff` is generated at
`sample_rate = 96000` while the bank runs once per two output samples), and the
`(out + previous_sample) >> 1` averager with `size -= 2`. Strongest analysis in the whole set.

#### Parameter mapping (axes FLIPPED to match `speech`)

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Brightness` | excitation spectrum tilt |
| TIMBRE | `Register` | **vocal register** (5 registers) |
| MORPH | `Vowel` | **phoneme** (5 vowels) |
| MACRO | `Formant tilt` | amplitude tilt, below |

**TIMBRE/MORPH are swapped from Braids' knob names** to match `speech_engine.cc`, which passes
`parameters.morph` as the phoneme and `parameters.timbre` as the `vocal_register`. Two vocal engines
in one palette with inverted axes is a worse outcome than a naming deviation from Braids.

#### ApplyMacro — tilt law replaced

```cpp
// Amplitudes are STORED as semitone attenuations (int8 per cell), not linear.
const float tilt = ApplyMacro(0.0f, -1.0f, 1.6f, parameters.macro);
const float amplitude_i = stmlib::SemitonesToRatio(tilt * atten_semitones_i);
```

The original linear law `amplitude_i = max(0, 1 + tilt*(a_i - 1))` is **the wrong shape** and its
excuse — "`a^t` needs `log2`, which has no shared helper" — is **false**. `SemitonesToRatio` is the
exact helper the same spec already uses for pitch. The linear law **zeroes every formant below 0.375
normalised** at `tilt = 1.6` — formants 3-5 in almost every cell (bass 'a' `[16384,7318,5813,5813,1638]`
→ `[1, 0.71, 0.17, 0.17, 0]`; alto 'a' → `[1, 0.61, 0, 0, 0]`). The log-domain form is monotone, never
negative, needs no clamp, and costs no libm.

#### Vendored-table rationale — repaired

The original rejected the shared `NaiveSpeechSynth` table partly because uint8 quantisation zeroes a
few upper formants and "MACRO = 1 would silence formants the model is supposed to voice" — **which is
exactly what its own linear tilt did**. Self-refuting; **delete that argument.**

The rationale now rests **only** on the sound part: pitch quantisation. The shared table's ~0.5
semitone of centre error is intolerable against a **Q = 64 bandwidth of 0.24 semitones**. That is
sufficient on its own. Vendor a 450-byte table.

**Overlap with `speech` must be stated honestly**, not framed as excitation-only: `NaiveSpeechSynth`
is `kNaiveSpeechNumPhonemes = 5`, `kNaiveSpeechNumRegisters = 5`, `kNaiveSpeechNumFormants = 5` —
the **same five vowels, five registers and five formants, from the same Braids table**, already in
the firmware and already on two panel knobs. What genuinely differs: Q = 64 vs Q = 20, a
saw/noise-crossfade excitation vs an impulse train, and the ability to whisper. Say that.

#### Correctness fixes (five)

1. **`SoftLimit` → `SoftClip` (R2).** `SoftLimit` is unbounded and crosses the host test's
   `|sample| > 4.0` abort at input ~36. Then re-verify the extremes corner the spec itself names
   (HARMONICS = 0, MACRO = 0, low note, every vowel/register corner).
2. **Per-formant limiter threshold is 8× too permissive.** Braids drives at full scale and `CLIP`s
   `svf_lp`/`svf_bp` at ±32767 **every sample**; the port drives at `excitation * 0.125f` then
   `SoftLimit`s the **already-scaled** signal — equivalent to ±8.0 FS in Braids terms, so the
   per-formant limiter is a **no-op exactly where Braids hard-clips**.
   *Fix:* limit **before** the drive scale: `out += 0.125f * SoftClip(bp);`
   **Declared deviation:** Braids clips the **state**, inside the resonant loop; the port limits only
   the output tap, so the Q = 64 states remain unbounded. State this explicitly.
3. **Drive halved.** `Oscillator::Render<OSCILLATOR_SHAPE_SAW>` emits `2.0f*this_sample - 1.0f` —
   bipolar ±1, 2.0 p-p; Braids' `phase >> 17` saw is 0…32767, 1.0 p-p. So plaits' saw is **6 dB
   hotter** and "MACRO 0.5 → Braids bit-for-bit" was off by 6 dB. *Fix:* `excitation * 0.0625f`
   (or scale the saw by 0.5f).
4. **Noise makeup made frequency-dependent.** A Q = 64 bandpass has bandwidth `fc/64`: **9.4 Hz at
   bass F1 (600 Hz) vs 77 Hz at soprano F5 (4950 Hz)** — an 8:1 energy spread. One
   `kVowelFofNoiseMakeup` cannot level the crossfade "across every vowel". *Fix:* per-formant makeup
   ∝ `1/sqrt(fc)`.
5. **Noise-tilt rationale dropped.** "A one-pole lowpass at 2 kHz gives −6 dB/oct" is only true
   **above** 2 kHz; below it the noise is flat while the saw already falls at −6 dB/oct. **F1 sits at
   260-1000 Hz across the whole grid — entirely in the flat region.** The argument does not apply
   where it was invoked.

#### Numeric corrections (implementers will validate against these)

- Bass 'a' F1: `9519/128 = 74.37 MIDI = **600 Hz**` (the textbook value), not 587 Hz (which is MIDI 74).
- Soprano F5: `14195/128 = 110.90 MIDI = **4950 Hz**`, not 4699 Hz (MIDI 110).
- The dropped averager `|cos(πf/96000)|` is **−0.30 dB @ 8 kHz, −0.69 dB @ 12 kHz**, −3 dB at 24 kHz
  (the original overstated it ~4×). Conclusion unaffected.

#### Other

- **Stereo mirror declared:** `aux_register = 1.0f - morph` is a **permanent image**, not a
  decorrelation trick — MORPH = 0 is bass-left / soprano-right forever. The manual must say so.
- **Redundant guard removed:** `min(0.24f, NoteToFrequency(...))` — `Oscillator::Render` already does
  `CONSTRAIN(frequency, kMinFrequency, kMaxFrequency)` with `kMaxFrequency = 0.25f`.
- **Flash restated:** code-only + the 450 B table + the `Oscillator<SAW>` instantiation (fresh code,
  ~0.5 KB, in any recipe that does not already select a saw user) = **2.7-3.5 KB**; 3,100 B carried in
  §1. **BSS line:** `float temp[kMaxBlockSize]` + ten `Svf` sections.

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": -0.8, "auxGain": -0.8 }
```
**Negative (R1):** resonant bank with unbounded states.

**Stereo: Pattern B** (the mirrored-register second bank is a real second render path). Four files
per R13: `PLAITS_STEREO_VOWEL_FOF`, `VOWEL_FOF:vowel_fof_engine`, `"vowel-fof": "VOWEL_FOF"`.

#### Manual

```json
"vowel-fof": {
  "controls": {
    "harmonics": "Tilts the excitation from a dark buzz to a bright breathy hiss.",
    "timbre": "Steps the vocal register from bass through tenor to soprano.",
    "morph": "Sweeps the vowel through the five phonemes.",
    "macro": "Restores the formant amplitude balance the stock model leaves flat."
  },
  "trigger": "Restarts the internal envelope when TRIG is patched."
}
```

---

### 3.9 `saw-comb` — **Saw Comb** (Feedback)

**Braids ancestry:** the saw → comb hybrid. Distinguishing features vs `reed-pipe` and `loopback`
(now stated explicitly, per the critique — they were real but invisible): **comb pitch decoupled
±64 semitones from the played note**, **genuine negative feedback**, and a **band-limited saw/pulse
exciter**.

**Source:** `plaits/dsp/engine2/saw_comb_engine.{h,cc}` · class `SawCombEngine` ·
member `saw_comb_engine_`

#### Parameter mapping

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Resonance` | comb feedback, **bipolar**: negative below noon, FIR comb at noon, positive above |
| TIMBRE | `Comb pitch` | `comb_note = note + (-64 … +64)` semitones |
| MORPH | `Exciter` | `VariableShapeOscillator` saw ↔ pulse |
| MACRO | `Loop tilt` | in-loop shelf, below |

**One correct delay formula, stated once (R6):**
```cpp
const float delay = 1.0f / NoteToFrequency(comb_note_lp_);
```
**Delete** the `kCorrectedSampleRate / NoteToFrequency(...)` form that appeared in `paramMapping` —
it is wrong by fs² (~9.5e6 samples) and an implementer working from that section builds a broken engine.

#### ApplyMacro and the stability fix (blocker)

```cpp
const float tilt = ApplyMacro(0.0f, 0.6f, -0.6f, parameters.macro);   // damp below noon, brighten above
```

**The originally specified compensation is arithmetically insufficient and can self-oscillate.**
With `ONE_POLE(loop_lp_, x, 0.35f)` the one-pole's response at Nyquist is `0.35/1.65 = 0.212`, so
`shaped = x + (loop_lp_ - x) * tilt` has HF gain `(1 - 0.788*tilt)` = **1.473 at `tilt = -0.6`**.
The specified pre-scale `1/(1 + 0.6*max(-tilt,0))` = 0.735 leaves a net HF loop gain of **1.083 > 1**.
Braids' loop is flat, so `|g| ≤ 1` was guaranteed by the COLOR knob alone; here MACRO > 0.5 drives HF
self-oscillation at HARMONICS settings well below the self-oscillation point.

*Fix:* derive the compensation from the actual shelf peak —
```cpp
feedback *= 1.0f / (1.0f - 0.788f * min(tilt, 0.0f));
```
— **and** clamp `|feedback * (1.0f - tilt)| <= 1.0f` at block rate as a belt-and-braces bound.
**Verify numerically that HF loop gain never exceeds the HARMONICS setting across the full MACRO
range** before merge.

#### DC — attribution corrected

The original said the bright side "removes the DC that Braids' undamped, DC-blocker-free loop can
park at negative-feedback settings". **Backwards.** A comb `1/(1 - g z^-D)` attenuates DC by
`1/(1+|g|)` when `g < 0` and **amplifies** it by `1/(1-g)` when `g > 0` — DC parking is a
**positive-feedback** hazard. Compounding it, the damping side (`tilt > 0`) has **unity gain at DC**,
so MACRO < 0.5 provides **no bound** on DC runaway at HARMONICS → 1. Only the in-loop clip bounds it,
exactly as in Braids. State this.

#### Corrections applied

1. **`manualControls.harmonics` rewritten.** It said HARMONICS passes "through silence at noon". It
   does not: at resonance 0 the write-back is `0.5*in` and the output is `(in + (delayed<<1))>>1` =
   `0.5*dry + 0.5*one echo` — **a fully audible FIR comb** (`digital_oscillator.cc:275-278`), which
   the spec's own `paramMapping` states correctly.
2. **4,096-tap justification replaced.** The stated reason ("TIMBRE bottoms out at note−64 semitones,
   always above the resulting 11.7 Hz floor") is false: 4,096 taps at 48 kHz floors at 11.72 Hz =
   MIDI 6.23, so `comb_note = note - 64` only clears it for **played notes above MIDI 70.2 (A♯4)**.
   For most of the keyboard the bottom of the TIMBRE sweep is **clamped and dead**. The *conclusion*
   (4,096) is right — Braids clamps identically at 8,192/96 kHz — but for the right reason:
   **4,096 at 48 kHz reproduces Braids' 85.33 ms line and its identical 11.72 Hz floor, and the
   bottom of TIMBRE clamps below MIDI ~70 exactly as it does on hardware.**
3. **RAM argument deleted, and its real consequence added (R15).** "Does not monopolise the arena the
   way `particle_engine.cc:49` does" describes a cost that does not exist. The consequence that **does**
   follow: another engine's buffers alias this delay line at the same addresses, so **`Reset()` MUST
   memset all 4,096 taps on every engine switch**. This directly reverses the `triggerBehaviour`
   paragraph, which argued clearing the line is "a ~4000-cycle spike… for no musical gain".
4. **Exciter scratch buffer declared.** `exciter_buffer` appeared from nowhere in the MORPH mapping
   while `implementationNotes` enumerated every allocation. `VariableShapeOscillator::Render` **writes**
   rather than accumulates (`variable_shape_oscillator.h:236`), so a `float[kMaxBlockSize]` stack array
   suffices. Add it to the memory section.
5. **AUX tap collapse declared and mitigated.** `min(delay * 1.5f, kSawCombDelaySize - 2)` loses the
   3:2 ratio once `delay > 2729` (comb note below ~MIDI 13), and below MIDI 6.2 both taps clamp to the
   same value so **OUT == AUX and the stereo image goes fully mono**. *Fix:* invert the ratio to
   `1/1.5` once `1.5*delay` would clamp, preserving the fifth on both sides.
   Also: "the 3:2 ratio is irrational relative to no comb null" is nonsense as written; the defensible
   statement (verified) is **the two combs' null sets never coincide — `m = 1.5k + 0.25` has no
   integer solutions.**
6. **Flash re-estimated 2,100 → 3,000 B.** `VariableShapeOscillator` is header-only and **fully
   inlined** into the caller's `Render` (5 `ParameterInterpolator`s, the polyBLEP branch tree,
   `ComputeNaiveSample`) — `saw-comb` emits its own copy regardless, and in the hosted builder a
   recipe **need not include the VA engines at all**, so "costs no new DSP" is not a safe assumption.
   State whether the estimate assumes VA co-residency (it does not).

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": -0.85, "auxGain": -0.85 }
```
**Negative (R1):** feedback loop with a DC-amplifying region.

**Stereo: Pattern A** (two comb taps a fifth apart, matched gain).

#### Manual

```json
"saw-comb": {
  "controls": {
    "harmonics": "Sweeps comb feedback from inverted through a single echo to ringing resonance.",
    "timbre": "Detunes the comb up to five octaves either side of the played note.",
    "morph": "Morphs the exciter from a bright saw to a narrow pulse.",
    "macro": "Damps the feedback loop below noon and brightens it above."
  },
  "trigger": "Clears the comb line and restarts the exciter."
}
```

---

### 3.10 `raw-fm` — **Raw FM** (FM) — **GATED, see §8 Q2**

**Braids ancestry:** FM, FBFM, WTFM — three models merged. The merge itself is **correct and
well-argued**: the two Braids functions differ in one line and all three named positions are exact.

**Source:** `plaits/dsp/engine2/raw_fm_engine.{h,cc}` · class `RawFmEngine` · member `raw_fm_engine_`

#### Why this engine is gated

`plaits/dsp/engine/fm_engine.cc` (shipped as `two-op-fm`) already maps:
- HARMONICS → `Interpolate(lut_fm_frequency_quantizer, harmonics, 128.0f)` (`:67`)
- TIMBRE → FM index (`:92`)
- MORPH → **the same feedback-topology axis**: `feedback = 2*morph-1`;
  `phase_feedback = feedback<0 ? 0.5*fb*fb : 0` on the modulator **increment** (`:118`);
  `modulator_fb = feedback>0 ? 0.25*fb*fb : 0` on the modulator **phase** (`:124`).

The shipped catalog manual for `two-op-fm` literally reads *"Feeds the modulator back into itself for
increasingly complex spectra."* **Three of four knobs are the same axes.** The headline zhuzh —
"MORPH turns the feedback TOPOLOGY into a continuous, CV-able axis" — is already in the palette, and
the design cited `fm_engine.cc:150` as precedent without ever confronting that.

**The merge justification was also overstated:** "Plaits' version runs a `2*t*t` index curve where
Braids is linear, so the Braids feel was going to be lost entirely" is false. `fm_engine`'s
`amount = 2*timbre² * hf_taming` reaches 2.0 cycles vs Braids' 1.0, so **Braids' full index range is
entirely reachable at `timbre = 1/√2 = 0.707`.** Only the taper differs.

#### Re-scope (required if it ships)

Renamed `feedback-fm` → **`raw-fm` / "Raw FM"**, explicitly the **raw, native-rate** variant.
Differentiators that copy must lead on, none of which `two-op-fm` has:

1. **Unfiltered full-bandwidth feedback** — no `0.05` one-pole on the feedback path.
2. **No oversampling** — `two-op-fm` runs 4×; this is deliberately the rawer, more aliased sibling.
3. **Linear index law** (Braids) vs squared (`two-op-fm`).
4. **The CW half is WTFM's *chaotic* wavetable-index feedback**, not modulator self-feedback — a
   genuinely different nonlinearity that `two-op-fm` cannot reach.
5. **Modulator on AUX.**
6. **MACRO = feedback depth**, the one unambiguously new control.

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Ratio` | `lut_fm_frequency_quantizer` (23 plateaus, −12…+36 st) |
| TIMBRE | `Index` | **linear** to ±1 cycle (`<< 2`) |
| MORPH | `Character` | FBFM ← plain FM → WTFM |
| MACRO | `Depth` | feedback depth |

```cpp
const float depth = ApplyMacro(kRawFmStockDepth, 0.0f, 1.0f, parameters.macro);
```

#### Fixes applied

1. **R1: gains go negative.** "Both taps are bounded sine lookups at ±1.0 (no limiter needed)" is
   invalidated by the mandated DC blocker: WTFM measures up to **−0.59 FS**, so subtracting a
   converged −0.59 yields **[−0.41, +1.59]** → `1.59 × 0.8 = 1.27` → hard clip.
2. **The un-oversampled MORPH ≤ 0.5 half addressed explicitly.** The load-bearing argument — "the
   feedback path is a ONE-SAMPLE delay; running the loop at 192 kHz is a different nonlinear system"
   — is valid **for the feedback path only**. It does not apply at MORPH = 0.5 (plain FM, zero
   feedback) or to the carrier/modulator sine reads, and the design's own `hf_taming` is a literal
   1.0 for all MORPH ≤ 0.5. So it shipped an untamed, un-oversampled ±1-cycle PM oscillator at
   48 kHz, while Emilie's own strictly *milder* `two-op-fm` runs 4× oversampling **and** a squared
   taming ramp **and** a 0.05 one-pole.
   *Resolution:* **oversample the oscillators 4× while updating `previous_sample_` once per OUTPUT
   sample** — preserves the 1/48000 loop delay exactly and still band-limits. (The design never
   considered this compromise.) If Lyle prefers the raw character, the fallback is to **declare**
   that plain-FM-at-centre is deliberately more aliased than `two-op-fm` and say why — but it must be
   one or the other, not silence.
3. **The 0.504 modulator-octave glide documented.** `kWtfmCentre = 129/256 = 0.504` is correct
   (verified `:814`, `(modulator_phase_increment >> 8) * (129 + (previous_sample >> 9))`, `prev>>9`
   ∈ [−64,63]) and correctly scaled by `amount` — but that means the modulator's effective frequency
   slides continuously from **0.504× (MORPH 0) to 1.0× (MORPH 0.5)**: an audible **one-octave pitch
   sweep** on a knob sold purely as "topology". *Fix:* either scale `kWtfmCentre` so modulator pitch
   is continuous across MORPH, or name the glide in the manual line. **Chosen: make it continuous** —
   the glide is an artefact of Braids' fixed-point centre, not a musical intent.
4. **"Centre detent" language deleted throughout** (R11).
5. **HARMONICS ratio positions: explicit note added.** "Bit-faithful at named positions" needs
   "*except that HARMONICS positions are rescaled*". Braids' `lut_fm_frequency_quantizer` has
   **25 plateaus over −36…+36 st**; Plaits' has **23 over −12…+36** (dropping 0.125 and 0.25 — that
   part was right). But **every ratio moves**: r = 1.0 sits at Braids COLOR 0.42-0.44 and at Plaits
   HARMONICS 0.23-0.24; r = 0.5 at 0.27 vs 0.00; r = 2.0 at 0.59 vs 0.47.

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": -0.8, "auxGain": -0.8 }
```
**Stereo: Pattern A** (carrier on OUT, modulator on AUX, matched gain).

#### Manual

```json
"raw-fm": {
  "controls": {
    "harmonics": "Steps the modulator ratio through twenty-three tuned intervals.",
    "timbre": "Raises the modulation index linearly to a full cycle of deviation.",
    "morph": "Moves from self-feedback through plain FM into chaotic wavetable feedback.",
    "macro": "Sets how deeply the feedback path drives itself around the stock amount."
  },
  "trigger": "Restarts the internal envelope when TRIG is patched."
}
```

---

### 3.11 `fluted` — **Fluted** (Physical) — **GATED on a measurement**

**Braids ancestry:** FLUT — `DigitalOscillator::RenderFluted` (`:1367`).

**Source:** `plaits/dsp/engine2/fluted_engine.{h,cc}` · class `FlutedEngine` · member `fluted_engine_`

#### THE GATE — do not write code until this is measured

Everything justifying this engine rests on one **asserted, underived** claim:
*"Below noon (2 harmonics) only the fundamental survives and the pipe tunes reliably."*

**The topology argues the opposite.** The jet+bore loop is `2/f0` seconds long with one inversion, so
its modes sit at `(2k+1)·f0/4` — **none at f0**. The direct inverting path (bore → reflection → bore
write, bypassing the jet) is a short loop of length `bore_delay = 0.69-0.81 × (2/f0)`, whose modes are
odd multiples of ~0.31-0.36 f0 — so **the mode nearest f0 is the THIRD of that series, not the first**.
That is exactly why the cited study measures 1.50× / 3.47× / 1.01× / 2.44× / 2.48× across the COLOR
sweep. Restricting the reflection to 2 harmonics would therefore **kill the modes that land near f0
and select a sub-f0 one: reliably out of tune instead of chaotically out of tune** — i.e. MORPH does
the opposite of what the zhuzh claims and the engine has no stated reason to exist.

**Required before any implementation:** prototype the loop in the SDK's WASM audition
(`plaits_lab.py dev` is exactly this) and **measure the dominant partial versus the played note across
the MORPH range**. If MORPH below noon does not produce f0 tracking, **drop the engine** — do not
patch around it.

#### Fixes to apply if the gate clears

1. **PITCH TERM IS WRONG: `- 2.0f` → `- 1.0f`.** Verified: `RenderFluted` has **no `size -= 2`**
   (grep confirms the nearest are at `:1285`, inside `RenderBowed`, and `:1823`), so it is the
   **full-rate 96 kHz** model, unlike `RenderBowed` which uses `delay_ >> 1` and is already a 48 kHz
   loop. Braids' `bore_delay = (delay_ << 1) - (2 << 16)` subtracts 2 samples **at 96 kHz**; the
   48 kHz equivalent is **1.0 sample**. Bowed got this right; Fluted transplanted the constant
   without redoing the rate reasoning (R5). Net error ~1 sample on a loop of 2 periods:
   **~5 cents sharp at MIDI 60, ~19 cents at MIDI 84, ~37 cents at MIDI 96.**
   *Better still:* the port's rate-corrected reflection one-pole has a **different group delay** from
   Braids' (~`(1-b)/b`: 1.5 samples at b = 0.4/96 kHz vs 0.25 at b = 0.8/48 kHz), so a fixed constant
   is wrong in principle. **Adopt `ReedPipe`'s computed form as normative:**
   ```cpp
   const float filter_delay = 0.88f * (1.0f - b) / b;
   ```
   The original named this fix in an "ORDERING NOTE" but the normative `paramMapping` did not use it.
2. **In-loop DC blocker coefficient must be stated.** The design cleared "both DC blockers" on trigger
   and counted one in the stereo cost but **never gave the coefficient for the reflection-path
   blocker** — which sits **inside** the feedback loop and therefore sets low-note behaviour and
   per-mode loop gain. Braids uses `kDCBlockingPole = 0.99 * 4096` at 96 kHz, corner ~153 Hz. State
   the 48 kHz equivalent explicitly.
3. **Output DC blocker corrected: 0.995 → ~0.98.** "0.995 — Braids' 0.99 at 96 kHz rate-corrected to
   48 kHz" is backwards: **halving the rate moves a one-pole FURTHER from 1**, so the equivalent is
   `p² ≈ 0.98`. 0.995 puts the corner at 38 Hz, four times too low.
4. **Numeric slip: "about MIDI 73" → MIDI 71.0.** With `b = 6.2832 · 13.9 · f / 48000` capped at 0.90,
   saturation begins at `f = 0.9 · 48000 / (6.2832 · 13.9) = 494.6 Hz = MIDI 71.0`.
5. **Aliasing plan redone at the PORT's rate (R7).** Band-limiting the noise source is cheap and
   correct, but the **half-wave-rectified cubic is applied to the TONAL jet signal and is left
   un-band-limited**. Every figure cited (0.11-7.08 % above 24 kHz; 96-99 % of energy below 12 kHz)
   is of **Braids at 96 kHz**. And "this port already runs HALF as many loop iterations per second"
   is a **restatement of the aliasing hazard, not a CPU credit**. Fluted is precisely the engine where
   the naive-port-aliases-more risk bites. **Required: a measured spectrum of the PORT at 48 kHz,
   at max TIMBRE and across the MACRO drive range where the cubic clamps hardest, before the
   no-oversampling decision is final.**
6. **R8 applied** (floor + saturate; bit-identity language deleted).
7. **Differentiation section added.** The design argues BLOW is redundant because Plaits ships
   `reed-pipe`, then **builds Fluted out of Reed Pipe's parts**: MORPH is Reed Pipe's
   `6.2832f * harmonic * frequency` reflection-corner idiom verbatim (Reed Pipe 1-13 harmonics,
   Fluted 2-40); the stereo plan is Reed Pipe's two-taps-on-one-bore verbatim; TIMBRE is breath in
   both; HARMONICS is a position-along-the-tube control in both. What genuinely differs is the
   **excitation nonlinearity** (half-wave cubic jet vs pressure-controlled reed valve) and the
   **multiplicative breath noise**. Combined with the design's own admission that *"FLUT cannot be
   bit-faithfully ported no matter how carefully you work"*, **this is a new jet-flute engine
   borrowing FLUT's topology, not a FLUT port.** `braidsAncestry` must be reframed as
   *"FLUT-derived jet-flute engine"*, and the catalog description must not promise a port.
8. **Flash arithmetic corrected.** Group total 4,348 B **double-counts `lut_oscillator_delays`' 388 B**
   — the shared-once figure is **3,960**. Counting `lut_oscillator_delays` as "avoided" is also
   generous, since Plaits already provides `NoteToFrequency` and no port would have added it. The
   individual table sizes are correct (`bowing_envelope` 752×2 = 1504, `bowing_friction` 257×2 = 514,
   `blowing_envelope` 392×2 = 784, `blowing_jet` 257×2 = 514, `flute_body_filter` 128×2 = 256,
   `oscillator_delays` 97×4 = 388).

**Verified correct:** the 48/256…79/256 jet fraction; `noise_depth = breath_intensity/4096`; the jet
table's `x = 2*jet_value` and its 0.5 output factor; the 0.65/0.52 envelope levels; the `if (size & 3)`
3-of-4 advance (`kBlockSize = 24` confirmed at `braids.cc:57` — a genuinely sharp catch); delay-line
size parity (58.1 vs 57.9 Hz, 38.2 vs 38.1 Hz); the dead `kRandomPressure`; and the real
`lut_flute_body_filter[pitch_ >> 7]` **out-of-bounds read** (`LUT_FLUTE_BODY_FILTER_SIZE` is 128,
`pitch_` clamps to `kHighestNote = 140*128`, and `RenderBlown` clamps the identical index to 0…127,
proving intent).

#### Parameter mapping (if it ships)

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Embouchure` | jet fraction, 48/256…79/256 |
| TIMBRE | `Breath` | breath intensity + multiplicative noise depth |
| MORPH | `Reflection` | reflection-filter corner, 2…40 harmonics |
| MACRO | `Jet drive` | `ApplyMacro(kFlutedStockDrive, 0.5f, 2.0f, parameters.macro)` |

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": -0.8, "auxGain": -0.8 }
```
**Stereo: Pattern A** (two taps on one bore, matched gain).

#### Manual

```json
"fluted": {
  "controls": {
    "harmonics": "Moves the jet across the embouchure hole from narrow to wide.",
    "timbre": "Increases breath pressure and the noise riding on it.",
    "morph": "Opens the bore reflection filter from two harmonics to forty.",
    "macro": "Drives the jet nonlinearity harder or softer around the stock amount."
  },
  "trigger": "Clears the bore and restarts the breath envelope."
}
```

---

### 3.12 `triple` — **Triple** (Polyphonic) — **GATED, see §8 Q3**

**Braids ancestry:** ⌐⌐x3, saw x3, /\x3, SIx3 (`settings.cc:156-159`) — four models merged.

**Source:** `plaits/dsp/engine2/triple_engine.{h,cc}` · class `TripleEngine` · member `triple_engine_`

#### Why this engine is gated — the duplication section the design omitted

This is the **most expensive engine in the set (2,800 B)** and the **only one that drags the four-file
Pattern-B stereo gate**, and its design was **silent on duplication**. The overlap is larger than for
any other candidate:

- `virtual-analog`'s catalog control 1 is literally *"Detune — Detunes the paired oscillators through
  closely spaced and musical intervals"*, built on **the same `VariableShapeOscillator`**.
- `swarm` is a detuned-oscillator cloud.
- **Critically:** the `chords` engine's chord table is **builder-configurable in cents**
  (`PLAITS_CHORD_CENTS` in `chord_bank.cc`), with existing entries like `{ 0, 1, 1199, 1200 }` — a
  one-cent beating "chord". So **four voices at arbitrary cent intervals with a waveform morph is
  already reachable today at zero marginal flash.**

Triple's genuinely unique offer reduces to **two continuously-swept interval knobs on one voice**.
That may be enough — but it must be Lyle's call, not a silent assumption.

#### Parameter mapping

| Panel | Label | Mapping |
|---|---|---|
| HARMONICS | `Spread` | interval ladder 1 (32-entry `intervals[]` with a 255/256 crossfade) |
| TIMBRE | `Interval` | interval ladder 2 |
| MORPH | `Waveform` | square → saw → triangle → sine |
| MACRO | `Shape` | pulse-width / slope asymmetry, below |

#### ApplyMacro — RE-RANGED (the headline zhuzh was half-redundant)

```cpp
// WAS: ApplyMacro(0.5f, 0.05f, 0.95f, macro)  -- WRONG
const float shape = ApplyMacro(0.5f, 0.5f, 0.95f, parameters.macro);   // min == stock
```

**Why the original was wrong:** a pulse of duty `d` and of duty `1-d` have **identical harmonic
magnitudes** (`|sin(πkd)/(πk)|` is symmetric about d = 0.5), and a `VariableShapeOscillator` triangle
with slope asymmetry `pw` and `1-pw` are **exact time reverses**. So the bidirectional range yields a
knob whose two halves are **spectral mirror images**: the same timbres either side of noon at half the
resolution.

**The correct shape is `min == stock`** — exactly what the sibling `sub-oscillator` already uses
(§3.5). Note this **also** puts stock symmetry at the detent, so the original's rationale for the
bidirectional range ("that is what puts stock symmetry at the detent") must be removed: min == stock
does that too. (Alternative, if Lyle wants MACRO doing work on both halves: give the lower half a
**genuinely different axis** — e.g. voice-level tilt — rather than a spectral mirror.)

#### Anti-aliasing — the denied hole must be closed

The plan states *"Same call for the sine: … a sine has no harmonics to fold — this voice is
rate-insensitive."* **That is only true at `pw = 0.5`.** The proposed two-segment phase skew

```
phase' = (phase < pw) ? 0.5*phase/pw : 0.5 + 0.5*(phase - pw)/(1 - pw)
```

is C⁰ but **not C¹**: at `pw = 0.05` the first half-cycle is compressed 10×, the waveform has a slope
kink, its spectrum falls at only **−12 dB/oct** out past ~10·f0, there is **no BLEP of any kind**, and
**it runs on three voices**. This is the one voice the plan says needs no protection and it is the
least protected thing in the group.

**Fix — pick one and state it:**
(a) integrated-BLEP the phase-skew kink; (b) taper the skew amount with f0; or
(c) **make MACRO inert in the sine region** and say so — which is symmetric with the already-admitted
saw-region inertness. **Recommended: (c)**, cheapest and already consistent.

#### Other corrections

1. **Stereo is ~3 dB quieter than mono (R13).** Each voice contributes `L² + R² = 1`, so at unison the
   per-channel peak is `0.328 × 0.707 × 3 = 0.70` against the mono 0.984, and `voice.cc` applies the
   same `out_gain` to both channels. *Fix:* compensate the stereo branch by **×√2**, or state it.
2. **The `auxGain == outGain` constraint is dropped.** `voice.cc:361-362` already substitutes
   `out_gain` for `aux_gain` when `stereo_render` is true, so `aux_gain` is simply **unused** in
   stereo (`swarm` ships −3.0/1.0). The self-imposed equality needlessly pinned the mono AUX level.
3. **One scratch buffer, not two** — the sine crossfade can be done in place.
4. **Pan saturation reconsidered.** `position = 0.5 ∓ 0.5 * min(1, |detune|/12)` **hard-pans every
   interval at or above an octave**, so the entire top half of both ladders gives no image movement.
   Widen the divisor to `/24` or use a soft law.
5. **"Braids' interval ladder" removed from the shipped manual string** — jargon to a Plaits owner who
   has never used a Braids.
6. **Unison-at-noon regression test required.** With `p = 0.5 * 32767 = 16383` the lookup lands in bin
   31 (`intervals[31] = -4`, i.e. −3.125 cents) and only reaches ~0 because `xfade = 255/256`. Correct
   as specified, but **a pure-float re-implementation of the ladder would break the promise silently.**
   Pin unison at HARMONICS = 0.5 through whatever integer/float form the ladder takes.

#### postProcessing / stereo

```json
"postProcessing": { "alreadyEnveloped": false, "outGain": 0.70, "auxGain": 0.85 }
```
Positive permitted (R1): three BLEP-corrected bounded oscillators, no DC blocker, no feedback.

**Stereo: Pattern B** (per-voice `StereoPanGains` placement is a real second render path). Four files
per R13: `PLAITS_STEREO_TRIPLE`, `TRIPLE:triple_engine`, `"triple": "TRIPLE"`. **R14 applies.**

#### Manual

```json
"triple": {
  "controls": {
    "harmonics": "Spreads the second voice through closely spaced and musical intervals.",
    "timbre": "Spreads the third voice independently of the second.",
    "morph": "Morphs all three voices from square through saw and triangle to sine.",
    "macro": "Narrows the pulse and slope of all three voices below the stock symmetry."
  },
  "trigger": "Realigns all three oscillator phases."
}
```

---

## 4. Shared modules — decision

**Create NO new shared module.** `shared_modules.json` stays `{chord-bank, physical-string,
physical-modal, fm-core}` unchanged.

| Candidate | Groups that wanted it | Decision |
|---|---|---|
| `dc_blocker.h` | feedback-fm (×3), physical (×2), formant, hybrid | **Inline per engine** — R9 |
| 2× decimator | analog-core (`ring-mod`), hybrid (`z-filter`) | **Inline per engine, different filters** — R7 |
| 4× decimator | feedback-fm (`toy`) | Use existing `lut_4x_downsampler_fir` |
| Waveguide / delay | physical (×2), hybrid (`saw-comb`) | **None.** `plaits/dsp/physical_modelling/delay_line.h` is header-only and already `shared_modules.json`-exempt. Bowed/Fluted use `plaits::DelayLine<float,N>`; `saw-comb` uses its own int16 line. |
| Formant table | formant (was ×2) | **Moot** — `vosim` dropped; `vowel-fof` vendors its own 450 B (R-rationale in §3.8) |
| `NaiveSpeechSynth` table reuse | formant | **Rejected** on pitch quantisation alone (0.5 st error vs a 0.24 st bandwidth) |

**Cross-group hazard closed:** three engines (`vowel-fof` via `Oscillator<SAW>`, `saw-comb` and
`triple` via `VariableShapeOscillator`) instantiate header-only templates that are **fully inlined
into the caller** and therefore **cost their own copy in every recipe**. None may claim "already in
the image" — a hosted recipe need not include the VA engines at all. This is folded into the flash
estimates in §1.

---

## 5. Implementation order and parallelism

### Wave 0 — before any engine (0.5 day)

1. Resolve §8 Q1 (attribution) — it sets `author`/`origin`/`packageId`/LICENSE on all twelve.
2. Land a **single reference `LICENSE`** carrying both copyright lines + SPDX, to be copied verbatim.
   `plaits_lab.py check` cross-verifies the LICENSE body, the named rights holder, and every per-file
   SPDX tag. Allowed licences are exactly `{MIT, BSD-2-Clause, BSD-3-Clause, ISC}` — **MIT**.
3. Confirm the `arm-none-eabi` container is reachable on Lyle's machine (it is **not** in this
   container: no toolchain, no Docker, no qemu). **Nothing in §6 is measurable until this exists.**

### Wave 1 — serial, one engine, establishes the template (2 days)

**`z-filter` first.** Four Braids models per slot, no feedback loop, no DC blocker, no shared module,
Pattern A stereo, positive gains. It exercises every registration step (the 14-step checklist) with
the fewest moving parts, and its output becomes the copy-paste skeleton for the rest.

Complete **all 14 registration steps** for it, including the two-commit dance:
commit engine + catalog → `./alt_firmwares/plaits_lab_catalog/sync_public_catalog.sh` (reads
`git archive HEAD`, **not** the working tree) → commit `public_catalog.json` separately.

### Wave 2 — fully parallel, 4 engines (independent; no shared state)

| Engine | Why independent |
|---|---|
| `bowed` | own `DelayLine`, own tables, Pattern A |
| `toy` | uses only the existing `lut_4x_downsampler_fir`, Pattern B (isolated 4-file edit) |
| `csaw` | own BLEP state, Pattern A |
| `ring-mod` | own halfband, Pattern A |

**Serialisation point:** `toy` is the first Pattern-B engine. Whoever lands it owns the four-file
edit; subsequent Pattern-B engines (`vowel-fof`, `triple`) must rebase on it, because
`stereo_config.h`, `plaits/makefile` and `container_server.py` are single-file merge points.

### Wave 3 — parallel, 3 engines

| Engine | Notes |
|---|---|
| `sub-oscillator` | shares the per-sample-`pw` twin-ramp helper *pattern* (not code) with `csaw`; land after it |
| `digital-modulation` | independent |
| `saw-comb` | independent; heaviest single-engine debugging load (loop-gain verification) |

### Wave 4 — parallel, gated

| Engine | Gate |
|---|---|
| `vowel-fof` | none technical; land after `toy` (Pattern B) |
| `raw-fm` | **§8 Q2** — Lyle go/no-go |
| `fluted` | **§3.11 measurement** — WASM mode-tracking probe **before** any code |
| `triple` | **§8 Q3** — Lyle duplication call; land after `toy` and `vowel-fof` (Pattern B) |

### Per-engine definition of done

1. Header + `.cc` in `plaits/dsp/engine2/`, both copyright lines + SPDX
2. `stereo_config.h` / `plaits/makefile` / `container_server.py` — **only** if Pattern B
3. `catalog.json` engines[] **and** manuals{} in the **same edit** (the validator fails otherwise)
4. `plaits/test/makefile` `CC_FILES` (alphabetical — **not globbed**; forget it and the host test
   silently drops the engine)
5. `plaits/test/plaits_test.cc`: include + `RenderAuditionEngine<X>("NN-<id>.wav")` +
   `ValidateExperimentalEngineExtremes<X>()` + `ValidateExperimentalControlResponse<X>("Name")`
6. `plaits/test/cpu_bench.cc`: include + `bench<X>("<id>")`
7. `python3 alt_firmwares/plaits_lab_catalog/validate_catalog.py` → exit 0
8. `make -f plaits/test/makefile -j4` → run from a scratch dir → "Synthesis engine tests passed."
9. `make -f plaits/test/makefile cpu-bench` — **ratios only**, absolute host numbers are worthless (R12)
10. **Commit**, then `sync_public_catalog.sh`, then commit `public_catalog.json` separately
11. `python3 alt_firmwares/plaits_lab_builder/check_config_scope.py` +
    `python3 -m unittest discover -s alt_firmwares/plaits_lab_builder -p 'test_*.py'`
    (expect 1 pre-existing error: `test_render_manual.py` needs `reportlab`)
12. **`qemu/estimate.py --sweep`** (R12) — not runnable in this container; required before merge
13. SDK reference package at `alt_firmwares/plaits_lab_sdk/packages/rubato/<id>/` (conventional for a
    Lab engine; enables `plaits_lab.py render/check/dev`)
14. **Extra per-engine regression scenarios mandated above:**
    `bowed` — MIDI 96 @ HARMONICS 0 (delay underflow; the extremes sweep uses note 60 and would miss it)
    `triple` — unison at HARMONICS 0.5
    `z-filter` — TIMBRE max @ high note (phase-wrap bounds)
    `saw-comb` — HF loop gain across the full MACRO × HARMONICS plane

### Cross-repo (rubato-audio), once at the end

`website/scripts/sync-plaits-catalog.mjs` → refresh `catalog.generated.json` + `plaits-pins.json`
(`sourceRef` must be the eurorack commit the deployed builder image was built from).
Then `alt_firmwares/README.md` + `alt_firmwares/PLAITS_LAB_PROJECT.md`.

**No `artwork` field on any of the twelve** — all 15 existing Rubato Lab engines omit it. If Lyle
wants icons later, that is a separate pass (PNG in two repos).

---

## 6. Flash budget — what these numbers actually mean

### The totals

| Set | Bytes | KB |
|---|---:|---:|
| Unconditional nine | 18,240 | 17.8 |
| Gated three (`raw-fm`, `fluted`, `triple`) | 6,850 | 6.7 |
| **All twelve** | **25,090** | **24.5** |

**All twelve numbers are UNMEASURED estimates.** `estimatedFlashBytes` is not a schema field anywhere
in the repo, there is no in-tree per-engine flash table, and the "1008-3392 B Lab band" cited by two
groups has **no in-tree source**. `arm-none-eabi-gcc`, Docker and qemu are **all absent from this
container**, so no real number can be produced here. Treat §1 as a *relative ranking*, not a budget.

### What the 224 KB limit actually constrains

The critical reframing, which no group stated: **the catalog already holds 39 engines and the flash
region holds ~24.** The hosted builder compiles **per recipe** — `generate_engine_config.py`
`normalize_slots()` emits only the selected slots. So:

> **Adding an engine to `catalog.json` costs ZERO flash in any recipe that does not select it.
> Flash is a per-recipe constraint, not a per-catalog one.**

Growing the catalog from 39 → 51 engines is therefore *free*. What is **not** free:

1. **`PLAITS_ENGINE_COUNT` is capped at 32** (`build_config.h`) because the speech/chiptune behaviour
   masks are `uint32_t` bitfields indexed by slot.
2. **Presets must be exactly 24 or 32 ids** (`validate_catalog.py:110-112`). Current presets: `stock`
   (24), `experimental` (32), `audition` (24). **Adding an engine to a preset always evicts one.**
3. **The stock-24 layout sits at ~228,688 of a 229,376-byte region — 688 bytes of headroom.** Nothing
   can be added to *that* image without an eviction. Average stock engine ≈ 9,529 B, but that includes
   shared tables; the unique-code share of a mid-size stock engine is ~3-6 KB, so **evicting one stock
   engine makes room for roughly two of these ports.**

### Practical consequence

- **Ship all twelve into the catalog.** That is the correct default and costs nothing.
- **Preset membership is a separate, zero-sum editorial decision** requiring 12 evictions if all
  twelve join `experimental` (32). That decision needs measured flash numbers and Lyle's taste — §8 Q4.
- The best-value engines by Braids-models-per-slot: **`z-filter` (4)**, **`triple` (4)**,
  **`raw-fm` (3)**, **`sub-oscillator` (2)**. Everything else is 1:1.
- **Tables avoided** (the important half of the flash argument, and it is verified): every engine
  reuses existing resources — `wav_sine` → `lut_sine`, `lut_fm_frequency_quantizer` → the float twin,
  `kFIR4Coefficients` → `lut_4x_downsampler_fir`, `kConstellationI/Q` → inline signs,
  `lut_bowing_friction` → `min(1, 1/(d+0.75)⁴)`. Net Braids-table bytes **not** carried across:
  **3,960 B** (corrected from the double-counted 4,348 — `lut_oscillator_delays`' 388 B was counted
  twice, and crediting it at all is generous since Plaits already provides `NoteToFrequency`).
  Only `vowel-fof` vendors new data (450 B).

---

## 7. Rejected candidates

### 7.1 `vosim` (Braids VOSM) — **DROPPED: duplicates a shipped stock engine**

The proposed VOSIM's exact topology is **already in the palette** as `granular-formant`
(`GrainEngine`, `plaits/dsp/engine/grain_engine.cc`), and the design **never mentioned it** — a
serious gap in a document claiming to have checked the palette.

Verified in source (`grain_engine.cc:66-78`):
```cpp
const float f1    = NoteToFrequency(24.0f + 84.0f * parameters.timbre);   // absolute formant pitch
const float ratio = SemitonesToRatio(-24.0f + 48.0f * parameters.harmonics);
const float carrier_bleed = ApplyMacro(stock_carrier_bleed, 0.0f, 1.0f, parameters.macro);
grainlet_[0].Render(f0, f1,         carrier_shape, carrier_bleed_fixed, out, size);
grainlet_[1].Render(f0, f1 * ratio, carrier_shape, carrier_bleed_fixed, aux, size);
```
Two sine formants under a **shared f0-locked window**, summed and DC-blocked. Point by point:

- **The pedestal** — the design's headline "do not omit this" element — is already a knob.
  `GrainletOscillator::Grainlet()` returns `carrier*(formant + bleed)/(1 + bleed)`; the `carrier*bleed`
  term **is** a window pulse train at f0, identical in kind to VOSIM's `P*(w-1)` after the engine's
  DC blocker. Granular Formant exposes it **continuously** on MACRO; the port hardwired it.
- **The 1/16-attack bell** is already reachable. `GrainletOscillator::Carrier()` shape branch 1 uses
  `breakpoint = 0.001f + 0.499f*t*t*t` — an asymmetric window whose attack fraction sweeps 0.001…0.5.
  **Braids' 1/16 = 0.0625 sits inside that range.** Granular Formant additionally polyBLEPs the
  grain-reset discontinuity, which the port does not.
- **`pulsar` covers the rest** (windowed grain train at f0 with a formant carrier, variable duty and
  skew, half-period-offset AUX). The design acknowledged this only as a one-line aside.

Genuinely unreachable content: (a) formant 2 as an **absolute** pitch rather than a ratio, (b) the
**2:1 amplitude balance**. That is a **re-macro of Granular Formant, not a 1,280-byte swap-in that
competes with it.**

Independent defects that would have needed fixing anyway (recorded so the decision is not re-litigated
on "but they were only bugs"):
- **Stereo rationale backwards and mono-sum broken.** "The pedestal term — identical in both channels
  — is correlated by construction" is false: AUX's window is evaluated at `aux_phase_ = phase_ + 0.5`,
  so the two pedestals are **anti-phase**, and two identical pulse trains a half-period apart **sum to
  a pulse train at 2·f0** — L+R moves the fundamental up an octave.
- **Output bound invalidated by its own DC blocker.** Braids' range genuinely is [−0.75, +0.75]
  (verified: `lut_bell` is uint16 peaking at 65534; the pre-window sample spans [0, 49150] against a
  24576 pedestal) — but removing the −0.375 mean pushes the peak to **+1.125**, so "hard-bounded at
  0.75", "never needs the limiter" and "0.9 is make-up toward unity" are all wrong (0.9 × 1.125 = 1.01).
- **The 0.45 clamp is dead code** — `NoteToFrequency` already caps at 0.44 fs (R4).
- **Alias budget ignored the new knob** — the −60 dB estimate was derived at Braids' 0.277 fs ceiling,
  but HARMONICS tracking pushes the carrier to 0.44 fs above ~MIDI 96, leaving **0.06 fs** of sideband
  headroom instead of 0.22.
- **CPU count self-inconsistent** — six `SineNoWrap` calls enumerated, then "~40 flops/sample" claimed;
  with four `ParameterInterpolator`s, two phase wraps, two DC blockers and the window branch,
  100-150 cycles/sample is realistic, roughly double the claimed budget share.

**Reinstatement condition:** an explicit A/B demonstrating what Braids VOSM does that
`granular-formant` cannot, with TIMBRE at the same absolute formant pitch, HARMONICS at the f2/f1
ratio, MORPH in the shape-branch-1 region near breakpoint 0.0625, and MACRO at full carrier bleed.

**Better use of the effort:** a re-macro pass on `granular-formant` exposing the absolute-vs-ratio
choice and the amplitude balance. That is a documentation-digest-only change, costs no flash and no
preset slot.

### 7.2 `wave-paraphonic` — **DROPPED: duplicates `chords`, and is the most expensive candidate**

Verified in source: `chord_engine.h:69` declares
`WavetableOscillator<128, 15> wavetable_voice_[kChordNumVoices]`, and `chord_engine.cc:141` computes
`waveform = max((morph_lp_ - 0.535f) * 2.15f, 0.0f)` against a curated 15-entry `wavetable[]` line
(`:95`), with `chord_pan[] = {0.5, 0.2, 0.8, 0.05, 0.95}` (`:64`) driving per-voice equal-power
stereo (`:180`).

So **the upper half of `ChordEngine`'s MORPH already IS** "N wavetable voices tuned to a chord table
scanning one curated wave line, with per-voice pan stereo." Further:
- `ChordBank`'s default table (`chord_bank.cc:42`) **opens with `{0, 1, 1199, 1200}`** — a 1-cent
  near-unison row — and its third table is a wide-voicing set. "Braids' chord list is musically
  different" is much weaker than claimed.
- `ComputeChordInversion` transposes voices by `0.25f * (1 << k)` — **it already performs the octave
  fan the proposed MACRO adds.**

The design's entire duplication defence was about chord **tables** and **never mentioned the wavetable
path at all.**

Independent defects:
- **The headline anti-aliasing claim is false.** "Its `Differentiator` plus the f0-tracking
  `cutoff = min(128 * f0, 1)` one-pole suppress aliasing better than Braids does at DOUBLE the rate."
  In `wavetable_oscillator.h:147`, `cutoff = min(float(wavetable_size) * f0, 1.0f)` with
  `wavetable_size = 128` **saturates at 1.0 for every f0 > 1/128 = 375 Hz** — so above 375 Hz both the
  differentiator smoothing and the `ONE_POLE` are **pass-through**, across exactly the range the
  design says goes harsh. Neither readout band-limits the wave's own content (a 128-point table
  carries to the 64th harmonic; there is no mipmap). The integrated K = 1 readout is equivalent to a
  boxcar of width `128*f0` — about **−3.9 dB at Nyquist and ~−10 dB on the first fold band**. Real but
  modest, and nowhere near what 96 kHz gives you. Strip the false claim and the honest statement is
  "the 48 kHz port aliases audibly more than the hardware" — which removes the engine's stated
  fidelity argument.
- **Uncosted flash.** `WavetableOscillator<128, 33>` is a **brand-new template instantiation**
  (`num_waves` is a template parameter; the only instantiation in the tree is `<128, 15>`), so the port
  emits a **second complete inlined copy** of `Render()` — 400-700 B on top of the stated 5,720. With
  the chord lerp, fan/clamp and Pattern-B stereo, realistic marginal cost is **~6.5-7 KB**: the most
  expensive candidate in the whole set, against a region with ~688 B of headroom.
- **Its one required core-file edit is the one tooling cannot review.** `set_phase()` in
  `plaits/dsp/oscillator/wavetable_oscillator.h` **moves no package digest** (`package_digest` hashes
  only `source.header + source.files`), which the design admitted. And the stated fallback — skip the
  randomisation — **reintroduces the comb-filtered attack on precisely the two sub-semitone unison rows**
  (`chords[0] = {2,4,6}`, `chords[1] = {16,32,48}`, i.e. 0.016-0.375 st) that the design argues most
  need it. The fallback is not a fallback; it deletes the trigger behaviour on the rows it exists for.
- **New generated resource data** — 4,488 B of waves regenerated through
  `plaits/resources/wavetables.py`, which is **python2.5-era** (`numpy.fromstring`, `xrange`, `file()`,
  an unconditional `import pylab`), plus a new `.cc` in `source.files`. A build-provenance surface no
  other candidate carries.

**Credit where due, so it is not re-derived:** the Braids-side homework was correct and was verified
line by line — the detent arithmetic (`p1>>11`, `(p1 mod 2048)*32`, the 30720/34816 dead zones, the
×16 expansion) is exactly equivalent to the proposed float rewrite **including continuity at the step
boundary**; all 16 claimed Braids→Plaits wave index mappings (2→129 … 174→178) are correct against
`wavetables.py` bank_3; the 17-item absent list is exactly right; `waves.bin` is 33,024 B so all 17
are regenerable; the `size -= 2` diagnosis is correct (a 2× unroll, and the `phase >> 1` is the 7-bit
index extraction that keeps `Interpolate824` inside the 129-byte wave, **not** a pitch halving);
`#define SEMI * 128` is read correctly (row 8 is {7, 12.023, 19.039} st); and
`ApplyMacro(0, -12, 24, m)` does land exactly on the Braids voicing at noon. **None of that is why it
is dropped.**

**If Lyle overrides the drop**, the minimum conditions are: take the austerity wave path (substitute
the 17 absent waves from `wav_integrated_waves`, ~135 B) instead of vendoring 4,488 B — dropping the
engine to ~1.5-2 KB; **drop the MACRO octave fan** (ChordEngine's inversion already does it) and spend
MACRO on something ChordEngine cannot do; **keep the per-voice wave fan**, the one genuinely novel
axis and nearly free; rewrite `antiAliasingPlan` honestly; and either commit to the `set_phase()`
core edit with a **named human reviewer** (`sync_public_catalog.sh --check` cannot see it) or redesign
the attack decorrelation to need no core edit.

---

## 8. Open questions for Lyle

These are judgement calls, not technical unknowns. Each has a recommendation and a default.

### Q1 — Attribution: `Rubato Lab` or `Mutable Instruments`? *(blocks Wave 0)*

`origin` is enum-constrained to `{"Mutable Instruments", "Rubato Lab", "Community"}`
(`plaits_lab.py:390`) — **there is no third value**, so `origin: "Braids"` (proposed by one group) is
a hard schema violation. A `braids/<id>` packageId namespace passes the regex but introduces a **third
namespace no consumer knows**.

The tension: the **algorithms** are Emilie Gillet's MIT code; the **engines** are re-derived at a
different sample rate with declared deviations and a fourth macro she never designed for.

**My recommendation (and the document's default): `author: "Lyle Mills"`, `origin: "Rubato Lab"`,
`packageId: "rubato/<id>"`**, with MIT dual copyright (hers first, then yours) in `LICENSE` and every
source file. Rationale: none of the twelve is a faithful reproduction, and attributing them to Emilie
overstates her authorship of *this* result while the header gives her correct credit for the algorithm.

**But this is yours to call** — the opposite view (these are ports; credit the originator in `origin`
and take a byline in `author`) is entirely defensible, and it also affects how the website presents
them. Whatever you pick, it must be uniform across all twelve.

### Q2 — Ship `raw-fm` at all? *(gates one engine, ~1,450 B)*

Three of four knobs replicate the shipped `two-op-fm` (§3.10), whose manual already says
*"Feeds the modulator back into itself for increasingly complex spectra."* The differentiators are
real but subtle: unfiltered feedback, no oversampling, linear index, WTFM's chaotic branch, MACRO
depth.

Options: **(a)** ship as re-scoped "Raw FM" with the differentiated copy in §3.10; **(b)** drop it and
spend the effort on a Braids model with no palette neighbour. The critique's own recommendation was
*"two better engines beat three where one is a re-skin."*

**My lean: (a), but only if you personally A/B it against `two-op-fm` and hear a difference worth a
catalog row.** If it sounds like a re-skin to you, it will to a customer. This is a taste call.

### Q3 — Ship `triple`? *(gates the most expensive engine, 2,800 B, plus a Pattern-B stereo edit)*

Four voices at arbitrary cent intervals with a waveform morph is **already reachable today at zero
marginal flash** via user-authored `PLAITS_CHORD_CENTS` tables in `chords`
(existing entries include `{0, 1, 1199, 1200}`), plus `virtual-analog`'s Detune and `swarm`. Triple's
unique offer is **two continuously-swept interval knobs on one voice** — genuinely different in
*gesture* (a knob you sweep vs a table you author), but not in *reachable sound*.

It also merges four Braids models, which is the second-best value ratio in the set. **My lean: ship
it**, because the gesture matters and 4:1 is good value — but you are the one who knows whether
Palette users author chord tables at all.

### Q4 — Preset membership: which 12 engines get evicted? *(cannot be answered without measurements)*

Adding to a preset is strictly zero-sum (24 or 32 exactly). This decision needs
`arm-none-eabi-size` deltas that **cannot be produced in this container** (no toolchain, no Docker, no
qemu). **Recommendation: ship all twelve into the catalog now, change no preset, and make the preset
call as a separate pass after Wave 1 gives one real measured number to calibrate the estimates against.**

### Q5 — `bowed`'s residual octave fold *(a small, permanent fidelity gap)*

Even with `bridge_` grown to `DelayLine<float, 1024>` (12,288 B total, fits the arena), the port folds
at ~17.2 Hz where Braids folds at ~11.4 Hz. Going further costs another 4-8 KB of the 16 KB arena for
notes below MIDI 21 that almost nobody plays.

**Recommendation: accept 17.2 Hz and state it in the header comment.** Flagging it because it is a
declared, permanent deviation from a "fidelity-first" engine and you may feel differently about that.

---

## 9. Risks

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| R-1 | **Every flash number is unmeasured.** No ARM toolchain, no Docker, no qemu in the analysis container. If the estimates are 40 % low, the preset arithmetic in §6 is wrong. | High | Wave 1 (`z-filter`) produces the first real `arm-none-eabi-size` delta. **Recalibrate all twelve against it before Q4.** |
| R-2 | **Host CPU numbers are worthless** and two designs already reasoned from them backwards (R12). `check --full` once reported "0.6×" for an engine at **281 % of budget**. | High | `qemu/estimate.py --sweep` (±14 %) before merge, `build --hardware --cpu-probe` before release. Budget is ~1500 cycles/sample **including** LPG, output stage, UI and ADCs. |
| R-3 | **`fluted`'s mode-locking gate may fail**, in which case the engine has no reason to exist. | Medium | Gate is explicit (§3.11): measure in `plaits_lab.py dev` **before writing code**. If it fails, drop — do not patch. |
| R-4 | **Pattern-B stereo drift is four-way** and three engines (`toy`, `vowel-fof`, `triple`) each touch the same three shared files. Precedent for how bad it gets: `container_server._recipe_is_stereo` spelled `== 3` inline after the options menu moved stereo to 1, so **stereo recipes compiled every engine mono AND sine-subosc recipes paid ~26 KB for an unreachable path.** | Medium | Serialise Pattern-B landings (§5). Add a checklist assertion per engine that all four files agree. |
| R-5 | **`saw-comb` HF self-oscillation.** The originally specified compensation leaves net HF loop gain 1.083 > 1 (§3.9). | Medium | Corrected formula + block-rate clamp + a mandated numeric sweep of loop gain over MACRO × HARMONICS before merge. |
| R-6 | **`bowed` delay underflow escapes both in-tree tests** (extremes uses note 60, control-response note 48; the bug bites from ~MIDI 85). | Medium | Explicit clamp + a mandated MIDI 96 / HARMONICS 0 regression scenario (§5 step 14). |
| R-7 | **`z-filter` out-of-bounds `lut_sine` read** at high TIMBRE — an unwrapped float phase reaching index ~500,000. Garbage or hardfault. | High (pre-fix) | Fixed in §3.1 (wrap every sub-sample). Add the high-TIMBRE/high-note scenario. |
| R-8 | **Stale `public_catalog.json`** — `validate_catalog.py` **never verifies digests**, so a stale allowlist passes the validator cleanly while the deployed Worker rejects the very recipes the website emits ("the recipe contains an unavailable package version"). Every stock build silently broke this way once. | High | The two-commit dance is mandatory (§5 step 10). `sync_public_catalog.sh --check` is the **only** gate; it is wired as `pnpm predeploy`. |
| R-9 | **C++98 divergence.** The host check compiles `-std=c++11` and accepts `auto`, range-for, `constexpr`, `nullptr`, `<cstdint>`; `arm-none-eabi-gcc 4.8.3` defaults to `gnu++98` and does not. **Surfaces only on the ARM build.** | Medium | Code review checklist; first ARM build after Wave 1 will catch a batch. |
| R-10 | **libm sneaks in.** `std::sin/cos/exp/log/pow` all **pass the host compile** then fail the bare-metal link. `plaits_lab.py NON_PORTABLE_STD` catches them for SDK packages but **nothing catches them in an in-tree engine until the ARM build.** | Medium | `plaits::Sine`/`SineNoWrap`, `stmlib::SemitonesToRatio`, `stmlib::Sqrt`. `log2` has no shared helper — roll a bit-trick approximation (needed by no engine here as specified). |
| R-11 | **Member-name collision is silent.** `generate_engine_config.py render_config()` dedupes `PLAITS_ENGINE_MEMBERS` by the `member` string while still emitting one `RegisterInstance` per slot — two different engines sharing a member would collapse into **one instance with no error**. | Low | Verified clean in R17; re-verify on every addition. |
| R-12 | **`voice.h` transitive-include class of bug** (commit `7d63cf2`). If any of these engines makes a core file depend on a header only it pulls in, **every palette without that engine breaks** with `compiler_failed`. | Medium | R9: no new shared headers. Regression palette = a recipe containing none of the twelve. |
| R-13 | **Twelve engines is a lot of surface for one review pass.** The critiques found a memory-safety bug, two hard-clip paths, a delay underflow, an fs²-wrong formula and a self-oscillation — **in designs that were otherwise unusually careful.** | Medium | The waved order in §5 front-loads the simplest engine so the template is proven before the hard ones. Do not parallelise Wave 2 across more than two people. |

---

*End of specification.*
