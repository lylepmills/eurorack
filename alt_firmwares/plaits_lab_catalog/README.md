# Plaits Lab package catalog

`catalog.json` is the source of truth for every approved stock and Rubato model.
It drives the firmware registry generator, SDK forking, build allowlist, and the
generated web catalog. Do not maintain independent engine lists in those
consumers.

The top-level `manuals` map holds user-facing control and trigger prose for
every engine. It is exported with a separate documentation digest, so wording
and layout improvements do not invalidate firmware package references.

## Engine capability review

Every engine added to the catalog must be considered for all three optional
pitch/reset paths before publication:

- **Fast FM:** can it consume the audio-rate exponential pitch stream, and does
  the stereo worst case pass the calibrated estimate plus the hardware probe?
- **Linear TZFM:** does it have oscillator phase whose signed increment can
  meaningfully reverse through zero, with host and hardware coverage?
- **Hard sync:** should it consume every sample-accurate reset itself, or keep
  the bounded first-edge-per-block fallback?

Both qualification and a deliberate rejection are valid outcomes. Add the
engine ID to `engineCapabilityReview.reviewed` only after recording those three
decisions in the implementation: qualified FM engines belong in the matching
`fmCapabilities` list and return the corresponding engine capability; native
hard-sync engines return `hard_sync_capable()`, while all others deliberately
use the shared fallback. `validate_catalog.py` requires the review ledger to
cover exactly the catalog engine set, so a newly published engine cannot skip
this gate.

Run `python3 alt_firmwares/plaits_lab_catalog/validate_catalog.py` after editing
the catalog. The validator checks identifiers, source files, implementation
metadata, package uniqueness, presets, and content digests.
