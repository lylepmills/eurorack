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
| Renaissance-style Speech remix | Parked | 2026-08-11 | `codex/renaissance-sam-prototype` at `a815337` |
| Four independent pitch CVs for chord engines | Parked | 2026-08-11 | `session/plaits-four-voct` at `ebed081` |
| Simplified three-position frequency-range selector | Planned | 2026-08-22 | this entry; no prototype yet |

## Planned

### Simplified three-position frequency-range selector

**Concept.** An advanced firmware option that reduces the FREQUENCY range
selector from twelve positions to the three most clockwise ones: octave
switching, fine tuning, and the full-range coarse sweep. Lyle's workflow --
and, we suspect, the common one -- is set a coarse tune, fine tune it, lock it
in. The nine positions counter-clockwise of octave switching (the eight
+/-7-semitone ranges and the LFO range) are never entered.

**Naming correction to carry into any implementation.** `PITCH_RANGE_HIGH`
(index 11) is not a high-frequency range: it is `60 + transposition * 48`, so it
sweeps 12..108 -- the full musical range on one knob. It is the position players
reach for as COARSE, and it is the far-clockwise one. The current name misleads;
prefer "coarse" in user-facing copy, and consider renaming in code.

#### Why the dropped ranges are redundant here

`transposition_` is [-1, +1] and a wide range is `transposition * 7 + range * 12`,
so a single wide range already spans 14 semitones -- wider than an octave. The
eight of them are centred an octave apart (12..96), which is another way of
choosing an octave. Octave switching does the same job over root +/-4 octaves
(nine positions, `12 * (octave - 4)`), covering 12..108 from the default root --
the same ground -- and does it better for this workflow, because it is saved,
lockable, and quantised.

Two further gains beyond decluttering:

- **Selection gets ~4x less fiddly.** The selector is a hidden parameter spread
  across the HARMONICS sweep, so each range holds 1/12 of the travel today and
  would hold 1/3.
- **It removes the trigger for a whole bug class.** The 2026-08-21 octave-root
  bug existed because reaching octave switching dragged the selection across
  eight ranges that each rewrote the sounding note. With three positions that
  sweep barely exists. (The `OctaveRootSnapshot` fix is still required for full
  builds and must not be reverted.)

#### What is actually lost

- **The middle resolution tier.** Coarse spans 96 semitones and fine spans +/-1
  (`anchor + transposition`), with the +/-7-semitone ranges previously bridging
  them. Landing coarse within a semitone means hitting ~1% of knob travel. This
  is already how Lyle works, so it is proven in practice; if it bites, widen fine
  tuning in simplified builds rather than restoring the ranges.
- **The LFO range** (-48..+12), a genuinely separate use of the module rather
  than part of the tuning workflow. Anyone using Plaits as an LFO should not
  build with this option.
- **One-gesture octave jumps** become two steps: enter octave switching, set it.

#### Work required to resume

1. Gate it as a recipe preference in the `syncInput` mould (schema 24, catalog
   qualification, website control, docs), not a hardcoded build variant.
2. **Remap the selector; do NOT renumber `PitchRange`.** Keep the enum values
   and map the three knob thirds onto `PITCH_RANGE_OCTAVES`, `_PRECISION`, and
   `_HIGH`. Renumbering would silently break two things that use range indices as
   a vocabulary rather than as positions: the `UI_MODE_DISPLAY_OCTAVE` LED
   patterns, and the octave shortcut's `locked_octave_ == 8 ? PITCH_RANGE_HIGH`
   sentinel. Confining the change to `PitchRangeFromControl` also leaves the
   saved `tuned_root_q8` / `locked_octave` state and the octave-root snapshot
   untouched.
3. Check the LED feedback reads sensibly with only three modes. Each survivor
   already has its own pattern (octaves alternating, coarse all-yellow), so this
   is likely a no-op, but confirm on hardware.
4. Extend `pitch_range_test.cc` to cover the simplified mapping in both build
   configurations, including that a saved root survives a reflash between them.
5. Full rollout: image, staging gate, hardware audition.

#### Constraints worth preserving

- Saved settings must survive moving between a full and a simplified build in
  both directions; both configurations have a tuned root and octave switching,
  so only the selector mapping differs.
- The option is additive. The default build keeps all twelve positions.

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
