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
| Four independent pitch CVs for chord engines | Parked | 2026-08-03 | `session/plaits-four-voct` at `ebed081` |

## Parked

### Four independent pitch CVs for chord engines

**Concept.** In chord-capable modes, reinterpret V/OCT, MODEL, HARMONICS, and
LEVEL as four independently calibrated 1 V/oct pitch inputs. Outside those
modes the three repurposed jacks must retain their ordinary control behavior.

**Decision.** Parked because it is an interesting but non-priority feature. The
calibration/storage half has a working prototype; chord-engine routing and
hardware tracking tests were intentionally not started.

**Prototype.** Local branch `session/plaits-four-voct`, commit `ebed081`
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
