# AUX designs for the eleven Braids ports — proposal

Answers open item 5 of `BRAIDS_PORT_PROGRESS.md` ("AUX designs are all
stereo-split shaped"). Parts 1-5 are the design argument as it was put to Lyle;
**part 6 records what was then implemented, and corrects the numbers parts 4
and 5 projected.** All four recommended changes are landed.

Branch: `claude/braids-engines-plaits-palette-je03ac`. `PROGRESS §N` refers to
`BRAIDS_PORT_PROGRESS.md`; this document's own sections are called parts.
Measurements: `qemu/estimate.py --sweep`, local `plaits-lab-builder:local`,
all eleven baselines re-run and reproduced to within a point of PROGRESS §1.

---

## 1. The tension, and why it is already settled in the tree

The brief was to resolve, before designing anything, whether OUT and AUX can
serve two masters — two independently patched mono outputs, and an L/R pair —
or whether a genuinely different AUX voice necessarily makes a poor right
channel.

**It does make a poor right channel. And the firmware already knows that.**

`Patch::aux_output_option` (voice.h:154) is a global module setting with three
values: `0` the engine's own aux model, `1` stereo, `2` suboscillator.
`voice.cc:193` turns option 1 into `parameters.stereo` and hands it to the
engine, but only for engines reporting `stereo_capable()`. So an engine is
free to render **two different things and pick per block**. The two masters
were never meant to share one render.

Every stock engine with a stereo mode uses that freedom the same way: it keeps
a distinct AUX voice for mono and **drops it entirely** in stereo, building
the L/R pair out of OUT's own constituents instead.

| stock engine | mono AUX | in stereo |
|---|---|---|
| `virtual-analog` | monster-sync oscillator | dropped; the two constituent oscillators pan apart |
| `granular-formant` | Z-oscillator | dropped; the two grainlets pan apart |
| `wavetable` | 5-bit bitcrush of OUT | dropped; a second 8-point read half a wave-cycle away |
| `waveshaping` | sine/overtone blend of `lut_fold_2` | dropped; the two folder tables pan to 0.25 / 0.75 |
| `harmonic` | 8 organ harmonics | **not rendered**; the 24 harmonics spread by index |
| `swarm` | swarm of sines | **not rendered**; the sawtooths spread by detune |
| `inharmonic-string` | raw exciter bus | **not rendered**; the three strings pan round-robin |
| `modal-resonator` | raw exciter | **not sent to AUX**; even/odd modes lean L/R |
| `particle-noise` | raw random pulses | **not sent to AUX**; particles pan to fixed positions |
| `chords` | inversion subset, boosted | boost dropped; each note pans by voice position |
| `filtered-noise` | sum of both sources through BPs | second source through an identical multimode filter |
| `speech` | the secondary formant path | MACRO mix replaced by an equal-power pan of the two paths |

Twelve engines, twelve times the same answer. There is no case in the tree of
a mono AUX voice being reused as a right channel.

**So the anomaly is not that the eleven Braids AUX designs are "stereo partner
shaped." It is that ten of the eleven collapse the two modes into a single
render, which no stock engine does.** Ten of them hardcode `stereo_capable() {
return true; }` with no `parameters.stereo` branch and no `PLAITS_STEREO_<X>`
entry in `stereo_config.h` — one render serving both masters, which is exactly
the compromise the stock firmware refuses to make. `toy` is the sole exception
and already has the split (`toy_engine.cc:123`, `:140`).

### What the split costs

- **CPU: almost nothing.** The branches are mutually exclusive, so per-block
  cost is `max(mono, stereo)`, not the sum. A distinct AUX voice only has to
  fit *instead of* the current partner. This is what makes the question
  tractable given PROGRESS §3.6 — the "no second render path" rule was about
  rendering AUX **in addition to** its partner, and a Pattern B branch never
  does that. (Measured afterwards: the branch itself is not quite free, about a
  point of peak budget. Part 6.)
- **Flash: the nominal price, and smaller than it looks.** Both branches are
  compiled in, so this is where a split would be expected to cost. It does not
  have to: `toy`, the one Pattern B engine already in the tree, measures
  **−144 bytes** for carrying its stereo branch (open item 3, 2026-07-28) —
  gcc 4.8 compiles the runtime `parameters.stereo` branch tighter than the
  specialised mono-only body it replaces. One data point, not a law, and
  none of the four changes below adds a new algorithm — only a second
  combination of state already computed. Measure each with
  `flash_sweep.py --stereo` before committing. The existing per-engine gate
  (`-DPLAITS_STEREO_<X>=0`) caps the downside for recipes that leave an engine
  in mono.
- **RAM: whatever the second voice needs is permanent**, since both branches
  live in one object — and Pattern B FREES nothing, because the stereo branch
  still needs everything the old shared render did. Only `bowed` ended up
  adding any (two floats, for the exciter's DC blocker).

---

## 2. Two gaps in the measurement rig, found on the way

**`harness.cc:83` pins `p.stereo = false`.** Every CPU number in PROGRESS §1
is the mono path. For the ten Pattern-A engines that is the same code either
way, so those numbers stand unchanged. For `toy` it means the reported 35%
**excludes its entire stereo branch** — a second hold counter, a second
downsampler accumulate and a second DC blocker *per sub-sample*, i.e. most of
the inner loop again. Open item 3 closed the same gap on the *flash* side on
2026-07-28 (`toy`'s stereo delta measures −144 B, recorded as 0); the CPU side
was still open, and this closes it.

A `--stereo` flag (`PLAITS_QEMU_STEREO`, four lines in `harness.cc` +
`estimate.py`) does it, and is a prerequisite for measuring any Pattern B
engine. It puts **`toy`'s stereo path at 248.5 instructions/sample — 48% of
budget, against the 35% the table reports.** A thirteen-point gap that nothing
in the tooling could previously see, on the one engine the table already
describes as split.

Worth noting the two measurements point opposite ways, and both are right:
`toy`'s stereo branch costs nothing in flash (gcc 4.8 compiles the runtime
branch tighter than the specialised mono body) while costing thirteen points of
CPU. Flash is the cost of *having* both branches; CPU is the cost of *running*
the more expensive one. A Pattern B conversion has to be costed on both, and
neither predicts the other.

**`harness.cc:78` pins `p.morph = 0.5f`, and `SWEEP_POSITIONS` never moves
it.** The sweep varies harmonics, timbre, macro and note only. Several of
these engines put their most expensive state on MORPH: `raw-fm`'s WTFM chaotic
branch, `triple`'s sine region (three extra `Sine()` reads per sample),
`sub-oscillator`'s sub level, `toy`'s clock tracking. So "worst case" is a
worst case over four of five axes. Not urgent, but it means a MORPH-gated cost
can hide — and `ring-mod` already has an explicit `modulated = depth > 0.001f`
fast path that the sweep can never exercise the expensive side of, because
`depth` *is* MORPH.

---

## 3. Where the eleven actually sit

The premise of open item 5 is that most Plaits engines give AUX a genuinely
different voice. Measured against the actual stock inventory above, that bar
is lower than it sounds. Of sixteen stock engine classes, six carry a second
*voice* (`virtual-analog`, `granular-formant`, `two-op-fm` and the three
drums), seven carry a variant of the same voice, and three carry a raw
exciter.

Judged against that real bar rather than an idealised one:

| engine | current AUX | verdict |
|---|---|---|
| `sub-oscillator` | bare sub at full level | **at bar** — literally `two-op-fm`'s design |
| `raw-fm` | the modulator sine | **at bar** — the operator you cannot otherwise hear |
| `digital-modulation` | the symbol staircase | **above bar** — nothing stock emits a control signal |
| `toy` | unfiltered/aliased copy | **at bar** — literally `wavetable`'s bitcrush AUX |
| `saw-comb` | a comb tap a fifth up | **at bar** — a different comb pitch is a different note |
| `triple` | the undetuned root | **at bar** — comparable to `chords`' inversion subset |
| `bowed` | neck pickup | **below** — same string, same body filter, moved |
| `csaw` | mirrored notch depth | **below** — OUT with one knob moved |
| `ring-mod` | carrier × mod 1 | **below** — reachable from OUT by turning MORPH down |
| `vowel-fof` | reversed formant weighting | **below** — a second weighting of one sum |
| `z-filter` | the complementary model | **below, but no alternative exists** (part 4 below) |

Six are already at or above parity. Four are worth changing. One is a genuine
no.

---

## 4. Per-engine proposals

Costs are `estimate.py --sweep` worst-case instructions/sample and the tool's
budget percentage. "Prototype" numbers come from a throwaway patch in a
scratch worktree that replaces the current AUX with the candidate; nothing is
committed to the branch. All eleven baselines were re-run first and reproduce
PROGRESS §1 to within a point.

**Read the deltas this way.** Under Pattern B the *stereo* branch keeps
today's render, so today's number becomes the stereo cost and the prototype
number becomes the mono cost. Peak cost is `max(mono, stereo)`.

> ⚠️ **Part 6 supersedes these prototype numbers.** Each prototype REPLACED the
> aux computation outright; a Pattern B engine BRANCHES, and the branch is not
> free. Measured after implementation, peak cost rose about a point on three of
> the four, and `ring-mod`'s mono saving came in at nine points rather than
> seventeen. Every recommendation's DIRECTION held; the magnitudes did not, and
> the claim that peak CPU is unchanged in every case was simply wrong.

| engine | baseline (= stereo after) | prototype (= mono after) | mono delta |
|---|---:|---:|---:|
| `csaw` | 60.1 / 12% | 59.3 / 11% | −0.8 |
| `bowed` | 190.3 / 37% | 177.3 / 34% | −13.0 |
| `ring-mod` | 358.5 / 69% | 271.4 / **52%** | **−87.1** |
| `vowel-fof` | 376.3 / 72% | 366.2 / 70% | −10.1 |
| `toy` (already split) | 180.3 / 35% mono | 248.5 / **48%** stereo | *(unmeasured until now)* |

### bowed — CHANGE. Mono AUX = the bow exciter.

Current AUX runs the neck tap through *its own copy of the same body filter* —
the same string, the same resonator, a different pickup position. That is a
stereo partner by construction and nothing else.

Proposal: mono AUX carries `new_velocity`, the stick-slip friction output,
before the string and the body. Precedent is unambiguous —
`inharmonic-string`, `modal-resonator` and `particle-noise` all put the raw
exciter on AUX, and all three drop it in stereo. It is different in kind
rather than in placement: a dry, pitchless scrape you can patch into something
else, against a resonated bowed string on OUT.

Cost: **177.3 / 34%**, against a 190.3 / 37% baseline — 13.0 instructions per
sample cheaper. The change removes a two-pole body biquad and a one-pole tilt
filter per sample, and frees `body_aux_y0_`, `body_aux_y1_`,
`tilt_state_aux_`.

Stereo branch: keep bridge/neck exactly as it is. It is a good L/R pair; the
argument is only that it is a poor second *voice*.

Risk: the exciter's level tracks pressure and bow velocity rather than sitting
near the string's operating point, so `post_processing_settings.aux_gain`
needs re-deriving and the SDK audio-health gate needs re-running on it.

### csaw — CHANGE. Mono AUX = a variable-width pulse off the same transitions.

Current AUX is the same waveform with the notch depth taken from the mirrored
HARMONICS position. Two problems.

It is OUT with one knob moved — the weakest of the eleven by some distance.

And because the mirror is exact, `target_depth == target_depth_aux` at
HARMONICS noon, and so does the DC term that travels with it. **Measured on a
host build: at HARMONICS 0.50, `max|out − aux|` is exactly `0.000e+00` across
300 blocks at an output RMS of 0.46 — the two jacks are bit-identical. At 0.40
and 0.60 the difference is 0.142.** So the stereo image collapses to mono at
the centre of the knob, and a mono patcher gets one signal on two jacks there.
`saw-comb` explicitly guards against exactly this failure (it inverts the tap
ratio rather than clamping both taps together, "which would collapse the
stereo image to mono at the bottom of TIMBRE"); `csaw` does not.

Proposal: mono AUX carries a variable-width pulse — `+1` on the saw segment,
`−1` on the plateau — BLEP'd off the *same two transitions* OUT already
computes, with its duty-dependent mean removed at block rate (one subtract, no
DC blocker, no state). Saw on OUT and PWM square on AUX is the oldest dual-VCO
pairing there is, and TIMBRE already sweeps the duty.

Cost: **59.3 / 11%**, against a 60.1 / 12% baseline — 0.8 instructions per
sample, i.e. a wash. A square has no slope discontinuity, so both
integrated-BLEP pairs disappear, but that is most of what there was to save.

I expected the `plateau_slope_aux` divide to go with them and it does not:
measured divides are 1.40/sample before and 1.38 after. GCC already sinks both
plateau-slope computations into the transition branches where they are used,
so at ~523 Hz they cost about 0.01 divides per sample each, not one. The
divide budget here is `phase_/pw`, `(phase_ − pw)/(1 − pw)` and the BLEP
fractions, none of which this change touches. **Take csaw on the musical
argument and the noon-collapse fix; there is no CPU argument.**

Stereo branch: keep the mirrored notch, and fix the noon collapse separately —
it is a defect regardless of what happens to mono AUX.

Risk: none material. `csaw` has more CPU headroom than any other engine in the
port.

### ring-mod — CHANGE. Mono AUX = modulator 1 as a bare sine.

Current AUX is `carrier × mod1` against OUT's `carrier × mod1 × mod2`. The two
outputs are two points on one knob: turn MORPH down and OUT walks toward AUX.

Proposal: mono AUX carries `SineNoWrap(modulator_phase_)` — modulator 1 alone,
at note + detune 1. Its increment is clamped to MIDI 128 (13.29 kHz, the
`kRingModMaxPitch` ceiling), comfortably under Nyquist at 48 kHz, so it needs
**neither the shaper nor the halfband**.

Cost: **271.4 / 52%**, against a 358.5 / 69% baseline — **87.1 instructions
per sample, seventeen points of budget.** By far the largest CPU move
available anywhere in the port. It drops one `Overdrive` per sub-sample and
the entire 15-tap `Decimate` per output sample, and frees the 64-byte
`history_aux_`.

And the measured divide count goes **4.00 → 2.00 per sample**, exactly halved:
`Overdrive` is `SoftLimit(2x)`, a Padé form carrying one divide. PROGRESS
§3.13 says to treat 69% as a floor precisely because qemu prices a VDIV at one
instruction where an M4 spends about fourteen. Carrying that correction
through, the real figures are roughly **73% → 54%** rather than 69% → 52%.
This is the one change in the port that buys headroom back on the number the
progress doc says to distrust, and it buys a lot of it.

Stereo branch: keep the 3-way/2-way pair.

Risk, stated plainly: a bare sine is a thin AUX voice, and this is the weakest
*musical* case of the four changes. It earns its place on the CPU argument as
much as the parity one. If the sine reads as too plain in listening, the
fallback is `mod1 × mod2` without the carrier (the difference-tone bed), which
is more interesting but needs the decimator back and therefore gives up most
of the saving.

### vowel-fof — CHANGE, with a caveat. Mono AUX = the glottal source.

Current AUX is the same five SVF taps summed under the reversed formant
weighting — a second weighting of one sum, closest in the tree to `harmonic`'s
24-vs-8 or `waveshaping`'s two folder tables.

Proposal: mono AUX carries the excitation ahead of the bank — the saw
crossfaded to noise by HARMONICS, at one neutral noise makeup computed at
block rate. Precedent is the same raw-exciter trio as `bowed`, and it is the
natural sibling of `speech`, whose AUX is its secondary path.

Cost: **366.2 / 70%**, against a 376.3 / 72% baseline — 10.1 instructions per
sample, two points, on the engine with the least headroom in the port. It
drops five multiply-accumulates and five array reads per sample and adds about
three ops.

Caveat, because it decides this one: at HARMONICS 0 the source is a bare saw,
which is a thin AUX voice, and the reversed weighting is at least always
*vocal*. Two points of budget is real but it is not the seventeen `ring-mod`
returns, so the CPU argument does not break the tie on its own. **This is the
one of the four I would put to a listening test rather than just land** — and
keeping is entirely defensible.

Stereo branch: keep the two weightings.

### z-filter — KEEP. There is no alternative that does not collide with OUT.

This is the genuine no, and it is worth stating why rather than just
declining.

The engine has exactly two constituents: the windowed sine-resonator burst
(`saw_tri_signal`) and the pulse-or-integrator (`square_signal`). OUT is a
MORPH-controlled crossfade between precisely those two, with `balance = 1 −
|2·morph − 1|`. So:

- AUX = the resonator burst collides with OUT at **both ends** of MORPH, where
  balance is 0.
- AUX = the pulse/integrator collides with OUT at the **centre**, where
  balance is 1.

The complementary filter model is the only second signal available here that
is never equal to OUT anywhere in the knob's travel. It also already survived
the CPU fight documented in PROGRESS §3.6 (94% from its own resonator pair,
62% sharing OUT's phases). Leave it.

### sub-oscillator — KEEP. It already meets the bar.

AUX is the bare sub at full level, deliberately not blend-scaled. This is
stock `two-op-fm`'s AUX design essentially verbatim ("sub-oscillator, half
frequency"). It has its own pitch, its own waveform and its own
MACRO-controlled pulse width, and the "full level, not mix-scaled" call in the
header is exactly the decision that makes it a voice rather than a partner — a
mix-scaled sub would vanish at MORPH noon, which is where anyone would look
for it.

Nothing to do.

### raw-fm — KEEP.

AUX is the modulator sine: the operator you cannot otherwise hear, at a
quantized ratio to the carrier, already computed for OUT and therefore free.
Same reasoning as `sub-oscillator`; same stock precedent.

Nothing to do.

### digital-modulation — KEEP the mono AUX. FIX the stereo render.

The mono AUX — the symbol staircase — is **above** the stock bar, not below
it. Nothing in the stock palette emits a control signal; this is a stepped LFO
whose rate is TIMBRE and whose pattern is the packet structure, becoming a
voice only at high TIMBRE and mid-to-high pitch. That is more distinct than
most stock AUX designs.

Two things are wrong, both on the stereo side:

1. **The `stereo_capable()` comment describes code that does not exist.** It
   says "Pattern A: I on one side, Q on the other … each channel peaks at
   0.705 against the mono 0.997, so stereo is ~3 dB quieter (R13)". The render
   is `out[i] = shaped_i_ * in_phase + shaped_q_ * quadrature` and `aux[i] =
   dc_aux_out_`, the DC-blocked staircase (`digital_modulation_engine.cc:148`,
   `:158`). Note the arithmetic in that comment is right *for the design it
   describes* — R·√2 mono against R per channel really is 3 dB. It is right
   about a render this engine does not do.
2. **The actual stereo render is a DC-blocked stepped LFO hard right.** Of all
   eleven, this is the one whose stereo behaviour is genuinely bad.

The fix is the I/Q pair the comment already describes, and it is *cheaper*
than what runs now: `in_phase` and `quadrature` are both computed every sample
already, so the stereo branch becomes one multiply per channel instead of two
multiplies and an add, and skips the staircase's DC blocker entirely. This is
the one place in the port where Pattern B is needed for the **stereo** side
rather than the mono one.

### triple — KEEP.

AUX is the undetuned root voice alone: a clean pitch reference against a
beating three-voice mix, free because voice 0 is rendered regardless.
Comparable to stock `chords` (all notes on OUT, the inversion subset on AUX).

One alternative worth naming and not forcing: AUX could carry the **two
detuned voices without the root** — equally free, and arguably more distinct
(pure beating with no fundamental under it). That is a taste call for a
listening test, not a correctness argument. My weak preference is to leave it;
the clean root is the more *useful* of the two, and usefulness is what AUX is
for.

### toy — KEEP the shape. Measure the cost.

`toy` is the only one of the eleven that already does this correctly: mono AUX
is the same stream with no reconstruction filter (matching stock `wavetable`'s
bitcrush AUX exactly), and stereo replaces it with a second hold clock 2.93%
fast. It is the worked example for the other four.

What is missing is numbers. Its 35% is the mono path only, because the harness
pins `p.stereo = false`. Measured with the `--stereo` flag, its stereo path is
**248.5 instructions/sample, 48% of budget** — thirteen points above the
figure PROGRESS §1 reports for it, and its true peak. Still comfortable, but
the table should carry both numbers rather than the smaller one.

This is also the clearest evidence for the whole proposal: a Pattern B
engine's two branches really do cost materially differently, which is why the
split is what lets a distinct AUX voice exist at all.

---

## 5. What I would do, in order

1. **Land the harness `--stereo` flag** (`PLAITS_QEMU_STEREO`). Small, and
   nothing else here can be costed without it. Add `toy`'s stereo number to
   PROGRESS §1 next to its mono one; the table currently reports one path and
   implies two.
2. **Fix `digital-modulation`.** The stale `stereo_capable()` comment is a
   documentation defect on a shipped engine, and the I/Q stereo render it
   describes is both correct and nearly free. Independent of everything else
   here.
3. **Fix `csaw`'s HARMONICS-noon stereo collapse.** Also independent of the
   AUX question, and the same class of bug `saw-comb` already guards against.
4. **Convert `ring-mod` to Pattern B.** Seventeen measured points of budget
   (roughly nineteen once the VDIV correction is applied), on the engine
   PROGRESS §3.13 singles out as reading optimistically. Worth doing for the
   CPU alone even if the parity argument were thrown out. Wants a listening
   pass on the bare-sine AUX before it lands — that is the weakest musical
   case of the four.
5. **Convert `bowed`.** Clean parity win, three points cheaper, three floats
   of state freed, and the raw exciter is the best-precedented AUX design in
   the tree.
6. **Convert `csaw`** — but on the musical argument only. The measured CPU
   change is a wash and the doc above says so.
7. **Take `vowel-fof` to a listening test.** Two points is not enough to
   decide it on CPU, and the reversed weighting has a real virtue the glottal
   source lacks: it is always vocal.
8. Leave `z-filter`, `sub-oscillator`, `raw-fm`, `saw-comb`, `triple` and
   `toy`'s design alone.

Each conversion needs a `PLAITS_STEREO_<X>` entry in `stereo_config.h` so the
new stereo branch can be dead-stripped by recipes that do not enable it — the
ten Pattern-A engines have no entry today, which is correct while there is no
second branch to strip and wrong the moment there is.

Flash is the one cost this proposal does not have numbers for. Run
`flash_sweep.py --stereo` over each converted engine before committing. The
prior is encouraging — `toy` carries its stereo branch for −144 B — and none of
these adds a new algorithm, only a second combination of state already
computed. But PROGRESS §1's own table is the reason not to guess: the spec's
per-engine flash estimates ranged −39% to +74% while its aggregate was within
2%.

---

## 6. As implemented

All four changes are landed. Every number below is measured on this branch, not
projected.

### CPU — `estimate.py --sweep`, worst case over the grid

Mono and stereo are now separate render paths, so each engine has two numbers.
Peak is the larger, which is the stereo path in all four cases.

| engine | before | mono after | stereo after | peak |
|---|---:|---:|---:|---:|
| `csaw` | 60.1 / 12% | 64.2 / 12% | 65.4 / 13% | +1 pt |
| `bowed` | 190.3 / 37% | 190.4 / 37% | 198.4 / 38% | +1 pt |
| `ring-mod` | 358.5 / 69% | **309.6 / 60%** | 357.6 / 69% | unchanged |
| `vowel-fof` | 376.3 / 72% | 367.9 / 71% | 377.1 / 73% | +1 pt |

**Where part 4 was wrong, and why.** The prototypes replaced the aux
computation; the shipped engines branch on `parameters.stereo`, and the branch
costs. `ring-mod`'s mono path came in at 60% rather than the prototype's 52%,
so the saving is nine points rather than seventeen — still the largest CPU move
in the port, and still on the engine PROGRESS §3.13 says reads optimistically
(its divides do drop 4.00 → 2.00 per sample as predicted). `csaw` and `bowed`
gained about a point of peak instead of getting cheaper. Nothing regressed
enough to matter, but "peak CPU is unchanged in every case" was an artefact of
measuring the wrong thing.

**`vowel-fof` needed a second try.** Written as one loop with `if (stereo)`
inside the FIVE-iteration formant loop, gcc 4.8 re-evaluated the branch per tap
rather than hoisting it: 388.7 instructions/sample, **75%** — a regression
against the 72% it started at, on the engine with the least headroom in the
port. Giving each aux mode its own sample loop (sharing an `inline
RenderFormant`, so the two cannot drift apart) brought it to 367.9 / 71%. Worth
carrying forward: on this engine a per-tap branch costs about 21
instructions/sample, so hoist to the widest scope the code allows.

### Levels

`aux_gain` is used ONLY in mono — `voice.cc` hands the stereo AUX `out_gain` so
an L/R pair leaves at matched gain. So each new mono voice could be levelled
without touching stereo. Each is scaled in-engine to sit at OUT's RMS, which
keeps `auxGain == outGain` in the catalog exactly as before:

| engine | mono AUX vs OUT | worst DC |
|---|---:|---:|
| `bowed` | −0.44 dB | +0.036 |
| `csaw` | +0.06 dB | −0.002 |
| `ring-mod` | +0.02 dB | −0.013 |
| `vowel-fof` | +0.01 dB | −0.062 |

All well inside the SDK audio-health gate's ±0.2 DC limit. Two of the four
needed explicit DC handling: `bowed`'s bowing envelope is unipolar so the
friction output carries bow pressure as offset (one-pole blocker, ~7.6 Hz), and
`csaw`'s pulse mean travels with its duty (removed arithmetically as `1 - 2*pw`
rather than with a blocker, since TIMBRE moves pw far too fast for a 7 Hz pole
to follow without pumping).

**Measure a level over a whole NOTE, not a settled tail.** `bowed` taught this:
its string keeps building for ~100 ms while its exciter settles toward
stick-slip equilibrium, so the same two signals measure 10 dB apart over 30 ms
and 16 dB apart once settled. The shipped gain is fitted over 500 ms from onset.

### Verification

- **Stereo is bit-identical to before the change** on all four, over 18.7 MB of
  rendered samples each across a 243-point parameter grid — the guard that says
  Pattern B only added a path rather than moving the existing one. Mono differs
  on all four, which is the point.
- ARM compiles clean under `-Wall` for all four, in BOTH gate states.
- `PLAITS_STEREO_<X>=0` correctly reports `stereo_capable() == false` and
  dead-strips the stereo branch.
- `test_container_server.py` gained a test pinning `STEREO_MACROS` against the
  makefile's `PLAITS_STEREO_MODELS`. They are two hand-written lists of the same
  fact and drift between them is silent — a macro the makefile knows and the map
  does not can never be switched off. Verified it fails when drifted.

### Registration points touched

`stereo_config.h` (four macros), `plaits/makefile` (`PLAITS_STEREO_MODELS`),
`container_server.py` (`STEREO_MACROS`), `catalog.json` (each engine's second
`outputs` entry, which names the MONO aux — see `toy` and `modal-resonator`),
and `public_catalog.json` regenerated by `sync_public_catalog.sh`. That last one
moves all four engine DIGESTS, so the deployed builder must be rebuilt from this
branch head before the site can offer these engines — which is open item 7's
existing ordering constraint, not a new one.

### Still open

- ~~Flash is unmeasured for these four.~~ **Measured 2026-07-28**, and it is
  small: `vowel-fof` +496 B (it carries two copies of its sample loop),
  `ring-mod` +272, `csaw` +240, `bowed` +112, `digital-modulation` +48, `toy`
  −144. About 1.2 KB across all six, against the port's 22 KB — and the
  per-engine gate strips it for builds that leave an engine in mono. Controls
  reproduced to ±16 B (`harmonic` 2,576 vs 2,560, `glisson` 416 vs 432), inside
  the ±32 B quantization the table documents. Recorded in the website's
  `engineStereoBytes`. Still due, as `flash-budget.ts` notes: a re-sweep in the
  full production pass at the DEPLOYED revision — these were measured in a local
  container.
- **The listening test on `vowel-fof`'s glottal source still stands.** It is
  the one change whose case was never CPU (one point of mono), and a bare saw
  at HARMONICS 0 is a thin AUX voice against a weighting that is always vocal.
- ~~The two defects part 4 identified~~ — **both fixed**, see part 7.


---

## 7. The two defects

Both were found while writing parts 1-4, both are independent of the AUX
question, and both are now fixed.

### digital-modulation — the stereo render never matched its own spec

The `stereo_capable()` comment described "Pattern A: I on one side, Q on the
other", with a correct 3 dB level derivation. The code rendered
`out = I·cos + Q·sin` and `aux = the DC-blocked symbol staircase` — no I/Q
split at all, so in stereo the right channel was a stepped LFO.

This was not a stale comment drifting off working code. `BRAIDS_PORT_SPEC.md`
specifies exactly that split ("L = ±R·sin θ and R = ±R·cos θ ... each channel
peaks at 0.705 vs the mono 0.997"). The implementation shipped the staircase on
AUX for BOTH modes and the comment kept the spec's language, so the divergence
read as documentation drift when it was the render that was missing.

Fixed by making it Pattern B: mono keeps the staircase (above the stock bar —
nothing stock emits a control signal), stereo drops it and splits OUT into its
two quadrature components. Both terms are already computed for the mono sum, so
this is a decomposition rather than a second render:

- **L + R reproduces the mono OUT exactly** — measured
  `max |mono − (L + R)| = 5.6e-08` over a 125-point grid, i.e. float epsilon.
  The pair is perfectly mono-compatible, and no make-up gain is applied because
  applying one would break that identity.
- Each channel peaks at 0.705 against the mono 0.997, the 3 dB the spec
  predicted, and the two channels measure within 0.01 dB of each other.
- Stereo is CHEAPER than mono: 93.7 instructions/sample (18%) against 103.7
  (20%), because it skips the staircase's DC blocker.

### csaw — the stereo pair collapsed to mono at HARMONICS noon

Measured before the fix: at HARMONICS 0.50 the two channels were bit-identical,
`max |out − aux| = 0.000e+00` at an output RMS of 0.46. The mirrored notch
depth is its own fixed point at the knob centre, and the DC term that travels
with the depth mirrors too.

**No remapping of the notch depth can fix this.** A continuous `f` mapping the
depth range into itself with `f(x) ≠ x` everywhere would need `f(x) > x` for all
x — impossible at the maximum — or `f(x) < x` for all x, impossible at the
minimum. A fixed point is guaranteed, so any "better" mirror only MOVES the
collapse. That is worth stating because the obvious fixes all fail this way.

So the channels are separated on a second axis. `BendSegment` is AFFINE in its
bend parameter —

    BendSegment(u, a) − BendSegment(u, b) = (a − b)(u² − u)

— so a constant, non-zero bend difference makes the two saw segments differ at
every interior point of the segment, at every knob position, with no fixed point
anywhere. The segment always exists (pw tops out at 0.375), so the channels can
never coincide. The offset deliberately carries the aux bend outside the [−1, 1]
the knob reaches; clamping it back would reintroduce a fixed point at the
extreme. At the top of MORPH the aux bend reaches 1.35, where the segment dips
2.3% below its start before rising — a continuous wiggle, not a new
discontinuity, and the integrated BLEP stays exact because `BendSegmentSlope` is
the true derivative at any bend.

Verified by sweeping 8,000 points across all four knobs (with a sample landing
exactly on HARMONICS 0.50): worst L/R separation is now **0.0496 RMS**, against
0.000000 before. Cost: stereo 68.3 instructions/sample (13%) against 65.4;
mono is untouched and bit-identical.

One thing this cost a round-trip: the aux bend terms were first computed at the
top of the sample loop with a `stereo ? … : …` ternary, which put an add and two
ternaries per sample into the MONO path for values only stereo reads — 73.0
instructions/sample against 64.2, a regression on a path whose output had not
changed. Moving them inside the stereo guards restored it to 63.8. Same lesson
as `vowel-fof`'s formant loop, one level up: put the work where the branch
already is.

### Verification for both

Mono renders are bit-identical to before on both engines (18.7 MB of samples
each); stereo renders differ, as intended. ARM compiles clean under `-Wall` in
both gate states. `digital-modulation` gained a `PLAITS_STEREO_<X>` entry in all
three registration points, so both engines' stereo branches are now strippable.
