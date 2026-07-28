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
| `digital-modulation` | not started | — | — |
| `saw-comb` | not started | — | — |
| `vowel-fof` | not started | — | — |
| `raw-fm` | not started (Lyle: build it, then he A/Bs) | — | — |
| `triple` | not started (Lyle: build it, then he A/Bs) | — | — |
| `fluted` | still gated on the §3.11 mode-tracking measurement | — | — |

Not yet done: real `arm-none-eabi-size` flash measurements (§5 below), and the
whole website side.

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

### 3.9 Check the spec's DSP against the source before implementing it

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

### 3.10 Some engines cannot be matched spectrally, and that is not a defect

`bowed` is a nonlinear self-oscillator. The port's standard ~8-cent
kCorrectedSampleRate offset is half a percent of a 434-sample loop at MIDI 45,
which is enough to settle the stick-slip system into a **different limit
cycle**. Third-octave spectra sit 3–5 dB apart with every coefficient
agreeing. Pitch, level and gross tilt track; bin-level agreement is not a
meaningful target. `saw-comb` and `fluted` are the same class — do not spend a
session chasing their last few dB. The oscillator engines (`z-filter`, `csaw`,
`ring-mod`, `toy`) genuinely do match to a fraction of a dB, so the contrast
is informative rather than an excuse.

### 3.11 qemu undercounts divides

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
