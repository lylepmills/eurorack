# Braids → Plaits Palette port, phase 2: porting guide

Wave 1 ported 20 of Braids' 48 models as eleven engines. Phase 2 ports the
remaining 28 — the ones deferred as "Plaits already refines it", the three
dropped with written reasons, and the `????` easter egg nobody had triaged.

`fold` is landed and is the worked example. Read
`plaits/dsp/engine2/fold_engine.{h,cc}` and
`alt_firmwares/plaits_lab_sdk/packages/mutable-instruments/fold/` before
starting; they show every convention this document describes.

**Read `BRAIDS_PORT_SPEC.md` §2 for the cross-cutting rules R1–R10.** They are
not restated in full here, only the ones phase 2 keeps getting wrong.

---

## 1. The trap that cost fold a rewrite — read this first

`BEGIN_INTERPOLATE_PARAMETER_1` / `INTERPOLATE_PARAMETER_1` interpolate
**`previous_parameter_[1]` toward `parameter_[1]`** — that is Braids' **COLOR**.
`BEGIN_INTERPOLATE_PARAMETER_0` uses `parameter_[0]`, which is **TIMBRE**. The
suffix is the parameter INDEX, not "the first parameter".

`RenderSineTriangle` reads `timbre = parameter_[0]` for its fold depth and then
crossfades with `parameter_1`. Read the suffix wrongly and the model looks like
TIMBRE doing both jobs with COLOR unused — which renders perfectly plausibly,
still sounds like a wavefolder, and produced an engine whose fundamental was
14.8 dB down with the third harmonic on top of it. Its header, README and
catalog copy all confidently asserted "COLOR is UNUSED by this model."

Nothing in code review caught it. The A/B caught it on the first run.

**So: for every model, write down explicitly which Braids parameter drives which
behaviour, and cite the line.** Then prove it with an A/B case at each end of
each axis.

## 2. Three more things that are not what they look like

- **Braids' `wav_sine` is not Plaits' `lut_sine`.** It starts at −32512 and
  crosses zero a quarter of the way through: it is `−cos(2*pi*phase)`, where
  Plaits' is `sin(2*pi*phase)`. Inaudible through a linear oscillator; through a
  wavefolder or a phase-distortion stage it is a different waveform. `fold`
  carries a `BraidsSine()` helper — copy it.
- **`kCorrectedSampleRate` (47872.34) vs a nominal 48000 is a fixed +4.61
  cents.** Plaits derives pitch from the corrected constant (R6); the reference
  renderer does not. `ab_engine.py` already subtracts it and prints the
  corrected figure. If your engine reads ±0.5 cents, it is in tune. If it reads
  tens or hundreds of cents, you have an R5 rate bug.
- **`AnalogOscillator::Init()` sets neither `previous_phase_increment_` nor
  `previous_shape_` — and that is fine. Do not "fix" it, and do not re-derive
  the question from a stack-local test rig.** Both fields are zero because the
  object has static storage duration: the module declares `MacroOscillator osc;`
  at file scope (`braids.cc:58`) and `render_braids_model.cc` deliberately
  matches it (`static MacroOscillator osc;`, with the reasoning in its own
  comment). So the first `Render()` ramps `phase_increment` up from a
  *deterministic* zero, not from garbage. Three consequences, each measured:
  - **The ramp is common-mode, so multi-oscillator models are phase-coherent in
    the reference.** Ramping linearly 0 → P over N samples accumulates
    `P*(N+1)/2`, i.e. a lag of `(N−1)/2` = **11.5 samples at N = 24 —
    independent of P**. Every oscillator, at any pitch, lags by the same 11.5
    samples, which is a pure global delay. Probing `phase_/phase_increment_` at
    the first block boundary reads exactly `12.50000` for *every* oscillator of
    `SAW_SQUARE`, `SQUARE_SYNC`, `SAW_SYNC`, `TRIPLE_SAW` (including the
    four-octave `saw-wide` spread), `TRIPLE_SQUARE`, `SQUARE_SUB`,
    `SINE_TRIANGLE` and `MORPH` — spread `0.000e+00`. There is no
    reference-side relative-phase artifact to chase in any of them.
  - **Zeroing the field in `Init()` is a no-op on the first call and a
    regression on later ones.** Measured bit-exact both ways: identical output
    on every model above, *except* `MORPH` under a TIMBRE sweep, which crosses a
    region boundary and re-`Init()`s an oscillator mid-render — where the field
    currently holds a live, correct value and zeroing it injects a spurious
    11.5-sample slip the hardware does not have (14.7% of samples differ, peak
    full-scale, difference RMS only 11.7 dB below signal). The field is
    load-bearing on the re-`Init()` path. Leave it alone.
  - **The asymmetry that *does* exist is the shape-change re-`Init()` pitch
    clobber, not the phase increment.** `Init()` resets `pitch_` to `60 << 7`
    after `set_pitch`, so a re-`Init()`ing oscillator renders one block at MIDI
    60. It is common-mode wherever both/all oscillators change shape on the same
    first `Render()` (saw-square, dual-sync, triple, sub, sine-triangle — all
    documented in their `ab.json`s as costing nothing), and asymmetric only in
    `MORPH`, where `OSC_SHAPE_SAW` is 0 and so matches the zero-initialised
    `previous_shape_` while its partner does not. `morph/tests/ab.json` derives
    and measures that offset — `24 * (f60 − fnote) / 96000` cycles — across four
    octaves; read it before you conclude you have found something new.

  History, so it is not rediscovered a third time: before 2026-07 the renderer
  declared `osc` as a stack **local**, which really did leave both fields
  indeterminate and gave the reference a note-independent ~0.166-cycle skew.
  Every number taken against that build is void, including a "1.4 to 1.7 dB on
  every mixed-shape case" reading and the 2.0 dB tolerance it justified. If you
  are looking at figures in that range, you are looking at the old harness — or
  at a fresh test rig that reintroduced the bug by allocating an oscillator on
  the stack.

## 3. R5 — do the rate analysis before writing a line

Braids runs at 96 kHz. A render function ending in `size -= 2` is a 2x-unrolled
loop writing 96 kHz from a **48 kHz** inner algorithm, and its constants
transfer verbatim. A function without it is a **96 kHz** algorithm, and every
rate constant has to be re-derived for a 48 kHz port.

Check for internal oversampling on top of that. `fold`'s folders advance phase
by `phase_increment >> 1` twice per output, so Braids folds at 192 kHz; the port
runs 4x from 48 kHz to reach the same 192 kHz, and *because the internal rates
match*, the pitch-dependent guards transfer unchanged. Skipping that check is
what cost `fluted` its slot in wave 1 — a 37-cent pitch error.

State your conclusion in the header comment, with the line number you read it
from.

## 4. Tables: verify, never assert

Braids' resource tables are generated by the python2.5 scripts in
`braids/resources/`. Before embedding a table, reproduce it from its generator
formula and report the maximum deviation in LSB. `fold`'s two shapers reproduce
at 0 LSB; `bowed`'s friction curve at 1 LSB.

Embed the data when the closed form needs transcendentals in the inner loop;
substitute the formula when it is cheap. Either way the header says which, and
says the measured deviation. Extract the numbers programmatically from
`braids/resources.cc` — do not retype them.

## 5. Files you own, and files you must NOT touch

**Yours** (nobody else writes these):

```
plaits/dsp/engine2/<id>_engine.h
plaits/dsp/engine2/<id>_engine.cc
alt_firmwares/plaits_lab_sdk/packages/mutable-instruments/<id>/
    LICENSE            copy fold's verbatim
    README.md
    plaits-engine.json
    tests/scenarios.json
    tests/ab.json
```

**Not yours** — these are shared and are registered in one serial pass at the
end. Editing them means a collision with 21 other agents:

```
alt_firmwares/plaits_lab_catalog/catalog.json
plaits/dsp/engine/stereo_config.h     (your PLAITS_STEREO_<ID> is already there)
plaits/test/cpu_bench.cc
plaits/test/plaits_test.cc
plaits/test/makefile
```

Do not run `make -f plaits/test/makefile` — it writes a shared `build/`
directory. `plaits_lab.py check` and `ab_engine.py` both compile into private
temp directories and are safe to run concurrently.

## 6. Metadata conventions

Wave 1 resolved the attribution question (SPEC §8 Q1) **opposite to the spec's
default**. Follow the landed engines, not the spec:

```
author:    "Emilie Gillet"
origin:    "Mutable Instruments"
packageId: "mutable-instruments/<id>"
tags[0]:   "braids"
license:   MIT, carrying BOTH Emilie Gillet's and Lyle Mills' copyright lines
           in LICENSE and as SPDX tags in every source file
```

`description` is one or two sentences, plain, no marketing. Where the model has
a Plaits descendant, name the relationship and say what is actually different —
that is the whole reason these are being ported. Do not claim bit-fidelity
anywhere (R8); say "declared deviations" and list them in the header.

Never write user-facing copy asserting something you have not measured. The
first `fold` shipped "COLOR is UNUSED by this model" into three files.

## 7. The loop

```bash
cd alt_firmwares/plaits_lab_sdk
python3 plaits_lab.py check packages/mutable-instruments/<id>
python3 ab_engine.py packages/mutable-instruments/<id> --bands
```

`tests/ab.json` declares cases mapping Braids' `p1`/`p2` to your four macros,
with the two macros Braids does not have held at their stock position (usually
0.5) — otherwise you are comparing a sound the module cannot make. Cover each
Braids axis at both ends.

Target, from the landed engines: **AC RMS within ~1 dB, pitch within ~1 cent,
spectrum under ~1.5 dB.** A number outside that is a finding to explain in the
header, not a tolerance to widen quietly. `bowed` legitimately sits at 3–5 dB
because it is a chaotic self-oscillator and says so.

`--bands` is how you tell a real defect from an expected one: a difference
confined to the top octave is a decimator difference, while one in the band
carrying the fundamental is a bug.

## 8. The fourth macro

Braids has two knobs; Plaits has four. The two spare ones must be **stock at
noon** — `ApplyMacro(stock, min, max, parameters.macro)` returns `stock` at 0.5,
which is what keeps the model A/B-able and what a user expects from a centre
detent. Spend them on something the model implies but the module could not
reach, and prefer an axis the Plaits neighbour does not already have.

MACRO has a detent; MORPH does not (SPEC §2). Put the "stock value" axis on
MACRO.
