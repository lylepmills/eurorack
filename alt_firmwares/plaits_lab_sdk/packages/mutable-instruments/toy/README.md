# Toy

A port of Braids' TOY* model: an 8-bit phase ramp put through a bitwise
mangler, held by a sample-and-hold clock, oversampled 4x and decimated.

The DSP is Emilie Gillet's `DigitalOscillator::RenderToy`. Note that Braids'
TIMBRE knob sets the DECIMATION COUNT, not a bit depth — the held sample is a
uint8 at every setting. The fourth macro reparameterises the mangler's
trailing bias term, which Braids welds to `x >> 1`.

MORPH is new: it crossfades the hold clock from Braids' free-running rate to
one that tracks the played note, so the crush artefacts lock to the pitch
instead of beating against it. The two coincide exactly at MIDI 60.

The two added macros do not share one Braids-reproducing position. MACRO sits
at the usual 0.5 detent, but MORPH has to be at **0.0**, not 0.5 — Braids'
hold clock never tracks the note, and only morph 0 collapses the crossfade
onto it. `tests/ab.json` holds them there.

The AUX output is the unfiltered, aliased stream only in mono. In stereo,
OUT/AUX carry the left and right channels and both are reconstruction
filtered, with the right channel's hold clock running 2.93% fast.

Both copyright lines are carried in `LICENSE` and in each source file.
Declared deviations from Braids — and the four known defects the 2026-07-29
A/B audit found and deliberately left unfixed, since changing the DSP moves
the package digest — are listed in the header comment of
`plaits/dsp/engine2/toy_engine.h` and reproduced case by case in
`tests/ab.json`.
