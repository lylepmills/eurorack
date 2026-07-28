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

Both copyright lines are carried in `LICENSE` and in each source file;
declared deviations are listed in the header comment of
`plaits/dsp/engine2/toy_engine.h`.
