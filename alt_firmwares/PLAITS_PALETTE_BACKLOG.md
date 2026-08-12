# Plaits Palette future work

This is the central index for Plaits Palette features that are worth retaining
but are not active work. Use it for ideas that have enough product or technical
substance to resume later; quick implementation TODOs should stay beside the
code they describe.

## Statuses

- **Idea** — plausible, but not investigated enough to estimate.
- **Parked** — investigated or prototyped, deliberately not a current priority.
- **Planned** — accepted work with a clear next step.
- **Declined** — retained for decision history, but should not be pursued unless
  its constraints materially change.

When an item ships, remove it from this file and let the implementation,
release notes, and user documentation become the source of truth.

## Index

| Feature | Status | Last reviewed | Resume from |
| --- | --- | --- | --- |
| Experimental Linear TZFM + Fast FM | Planned | 2026-08-11 | eurorack `codex/plaits-tzfm-expanded` at `2b6a556`; website `session/codex-plaits-tzfm-options` at `c7d5e473` |
| Renaissance-style Speech remix | Parked | 2026-08-11 | `codex/renaissance-sam-prototype` at `a815337` |
| Four independent pitch CVs for chord engines | Parked | 2026-08-11 | `session/plaits-four-voct` at `ebed081` |

## Planned

### Experimental Linear TZFM + Fast FM

**Concept.** Offer two independent, opt-in FM experiments. Linear TZFM changes
the attenuverter law on qualified models: counter-clockwise is fixed-Hz linear
through-zero FM, clockwise remains ordinary exponential FM, and the center is
off. Fast FM continuously digitizes the FM jack at 50 kHz on qualified models;
because FM and LEVEL share the STM32F373 converter, a Fast-FM build gives up
LEVEL CV across the whole firmware. Either option can be used alone or together.

**Decision.** Planned after Sync In reached production, but not deployed. The
current schema-23 candidate is rebased onto the source-export/safety-gate work,
composes with Sync In, and replaces the original three-engine experiment with
explicit catalog policy: 29 models qualify for Linear TZFM and 34 for Fast FM.
Unsupported models retain their stock bipolar exponential FM behavior. A failed
periodic-LEVEL experiment and borderline Brass result were deliberately excluded.

**Candidates.** Firmware and builder work is on eurorack branch
`codex/plaits-tzfm-expanded` through `2b6a556` (`Add experimental FM badges to
field guides`). It includes the schema-23 contract and generator, independent
flags, engine implementations, hardware diagnostic/decoder tooling, capability
catalogs, post-link-compatible builds, and active-only `TZ` / `50k` field-guide
badges. The companion Rubato Audio branch
`session/codex-plaits-tzfm-options` through `c7d5e473` (`Separate experimental
Plaits options`) adds the editor controls and copy, capability-aware model
badges, presets, analytics, schema handling, and measured palette-aware flash
estimator. Both branch tips are pushed; neither is production.

#### Constraints and work required

1. Rebase both candidate branches onto their current `origin/master`, resolve
   any catalog/pin drift as one coupled change, and keep schema 23 undeployed
   until the exact merged source passes every gate below.
2. Re-run the full synthesis, builder/contract/manual, website, focused
   resampler/TZFM, ARM flash, and catalog CPU suites on the merged candidates.
   Verify off-state builds and combinations with Sync In, stereo, replaceable FM
   banks, Speech resources, and already-heavy models.
3. Hardware-audition the final product build across both options separately and
   together: carrier/modulation extremes, negative-frequency crossings, model
   changes, stereo, prolonged operation, and the qualified model lists. Confirm
   the recovery diagnostics stay quiet and that unqualified models retain stock
   behavior.
4. Make the LEVEL tradeoff impossible to miss in the final editor and field
   guide: Fast FM disables LEVEL CV throughout the build even while a model that
   does not consume the fast stream is selected. Linear TZFM alone keeps LEVEL.
5. Stage the schema-23 builder image and matching website snapshot, verify the
   generated guide/badges and flash estimates against exact artifacts, then run
   the physical-module staging gate and production canaries before rollout.
6. After production settles on the intended immutable image, update the deploy
   ledger and project memory, then remove this item from the backlog as shipped.

## Parked

### Renaissance-style Speech remix

**Concept.** Keep stock Plaits Speech and eventually offer a second speech
model centered on retriggerable word playback and frozen-frame scanning. A
selected word starts at a position chosen within that word when triggered; with
no trigger, the same axis can hold and scan individual LPC frames as a playable
formant oscillator. Custom word banks should feed both models, but each instance
of the remix can initially carry one bank rather than reproducing stock Speech's
multi-bank interface.

**Decision.** Parked after shipping custom banks for the current split Speech
architecture. Plaits Palette now exposes Speech Sounds and LPC Words as
separate models, while Original Speech and LPC Words share a bank editor and
selectable stock/custom LPC banks. The listening work established enough
musical difference to keep the Renaissance remix as a possible third model: it
is a focused word instrument whose distinctive behavior is starting within,
retriggering, and freezing a word, rather than another bank-management variant.

**Prototypes.** Firmware branch `codex/renaissance-sam-prototype`, through
commit `a815337` (`make LPC word playback jack aware`). The website listening
and custom-bank A/B work is on Rubato Audio branch
`codex/plaits-custom-words-prototype`; commit `b5172b49` added previews through
both engines and `c3ce1085` deliberately returned the active editor to stock
Speech. Treat all of these as research, not release-ready firmware.

#### What we learned

- The musically useful Renaissance idea is the interaction model, not its
  circulated SAM data: choose a word, choose a frame offset, trigger forward
  playback from that offset, or hold a frame when no trigger is present.
- The remix can be recreated from Plaits' MIT-licensed LPC speech machinery and
  newly encoded/custom word data. Do not copy or distribute the SoftVoice-derived
  tables circulated with Braids Renaissance; their provenance is unclear and the
  Renaissance source files have no individual license headers.
- A continuous-phrase bank truncated useful material and made selection vague.
  Splitting phrases into independently bounded words fixed both problems. The
  later jack-aware prototype also stays in held-frame oscillator mode only while
  TRIG is genuinely unpatched; a patched but infrequent trigger must not fall
  back to free-running behavior.
- Natural source pitch and a normalized 100 Hz pitch both sounded useful enough
  to preserve as bank-generation choices. Inverse prosody did not.
- Custom-bank previews proved that one editor can generate material for both
  engines, but the hardware assignment UI should not conflate stock Speech's
  several selectable banks with the remix's one-bank-per-model-slot design.

#### Work required to resume

1. Rebase the firmware prototype onto current `origin/master` and re-run the
   trigger-edge regression suite, including long intervals between patched
   triggers and held-frame behavior when TRIG is unpatched.
2. Decide the final control map and naming. Preserve a full-range prosody control
   and make the frozen-frame/word-playback distinction understandable from the
   model description rather than hidden lore.
3. Connect the production custom-word-bank format to the firmware generator.
   Start with one selected bank per remix model slot; multiple independent remix
   slots can carry different banks if flash permits.
4. Add the remix as its own model destination alongside Speech Sounds and LPC
   Words. Reuse the shipped custom-bank preparation flow, but keep assignment
   and engine previews distinct: Original Speech and LPC Words share several
   selectable banks, while each remix slot initially selects one bank.
5. Measure ARM flash/CPU, render public previews, audit stereo/AUX behavior, and
   complete hardware listening before catalog or Palette exposure.

#### Constraints worth preserving

- Stock Speech remains available; the remix is an additional model, not a
  replacement or a claim of exact Renaissance behavior.
- Keep SoftVoice-derived tables out of distributable firmware unless their
  provenance and permission are independently resolved.
- A patched TRIG input is authoritative even during long quiet intervals.
- Word boundaries are first-class bank data; do not return to one long phrase
  chopped only by a fixed frame budget.

### Four independent pitch CVs for chord engines

**Concept.** In chord-capable modes, reinterpret V/OCT, MODEL, HARMONICS, and
LEVEL as four independently calibrated 1 V/oct pitch inputs. Outside those
modes the three repurposed jacks must retain their ordinary control behavior.

**Decision.** Parked because it is an interesting but non-priority feature. The
calibration/storage half has a working prototype; chord-engine routing and
hardware tracking tests were intentionally not started.

**Prototype.** Branch `session/plaits-four-voct`, commit `ebed081`
(`plaits: prototype four-input pitch calibration`). It is isolated from the
shipping branch and should be treated as research, not release-ready firmware.

#### What we learned

- Plaits already keeps independent `offset`, `scale`, and normalization data
  for all eight CV ADC channels. Stock two-point pitch calibration only derives
  a pitch slope for V/OCT; at its 1 V step it treats every other CV input as a
  zero reference and updates that channel's ordinary offset.
- Consequently, feeding 1 V to MODEL, HARMONICS, and LEVEL during an unmodified
  calibration would make 1 V their new control zero and damage their normal
  response. A four-pitch procedure must skip that ordinary offset update for
  those three channels.
- Do not repurpose the ordinary control coefficients as pitch coefficients.
  Keep a separate pitch-only profile and consume it only in the intended chord
  modes. This is the main protection against making the other CV roles
  unusable.
- The persistent calibration payload has 16 bytes of reserved padding. It can
  hold a four-byte signature plus 1 V and 3 V `int16_t` ADC readings for each of
  the three additional inputs. Keeping the payload at 112 bytes with the same
  `CALI` tag preserves compatibility with existing calibration flash; older
  firmware continues to carry those bytes as padding.
- A safe procedure captures settled, low-pass-filtered 1 V readings from all
  four inputs, then settled 3 V readings. The three extra curves map their raw
  low/high points to 12/36 semitones, matching V/OCT's convention.
- Validate all four two-volt deltas before saving anything. A missing or
  malformed extra profile must be inert, and a failed attempt must leave the
  previous saved profile intact.
- Two-point calibration corrects per-input gain and offset. It cannot remove
  ADC/front-end nonlinearity, noise, reference error, or temperature drift, so
  "four identical V/OCT inputs" still requires bench testing across the useful
  voltage range.

#### Prototype evidence

- Added host tests cover blank-profile rejection, exact 1 V/3 V/midpoint
  transforms, and preservation of the previous profile after a bad calibration
  pair. The full synthesis test suite passed.
- Both the normal Palette firmware and the calibration-enabled prototype linked
  successfully in the pinned AMD64 build container.
- Enabling the prototype added 832 bytes of firmware text in that build. Its
  filter/capture fields were kept outside compile-time class-layout gates to
  avoid an ODR mismatch between translation units.

#### Work required to resume

1. Decide exactly which engines count as chord-capable and how each voice maps
   to the four input jacks. The current shared chord machinery is used by
   Chords, String Machine, Chiptune, Helix, and Wave Paraphonic; audit all five
   rather than modifying only the stock Chords engine.
2. Gate the reinterpretation on both chord mode and a valid extra calibration
   signature. Preserve the ordinary MODEL/HARMONICS/LEVEL paths everywhere
   else.
3. Add deterministic DSP tests for four independent notes, profile fallback,
   mode transitions, and unchanged non-chord control behavior.
4. Bench-test all four inputs at several voltages and temperatures with a
   precision source and tuner/frequency counter. Establish an acceptable cents
   error before exposing the feature in the hosted builder.
5. Reassess panel interaction and discoverability. The prototype calibration
   expects the existing right-button-at-power-up flow with 1 V, then 3 V,
   patched to all four inputs.

#### Constraints worth preserving

- Do not change `sizeof(PersistentData)` or its `CALI` tag.
- Do not write the extra profile unless every pitch pair passes validation.
- Do not let invalid extra data reach a divide-by-zero or alter ordinary CV
  behavior.
- Firmware audio updates preserve the settings sector, but an SWD/settings
  erase and a power failure during flash erase/write remain the same risks as
  stock calibration.
