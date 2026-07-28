# Bowed

A port of Braids' BOWD: a bowed-string waveguide driven by a stick-slip
friction exciter. Nothing in the stock palette is a continuous friction voice.

The DSP is Emilie Gillet's `DigitalOscillator::RenderBowed`, which ends in
`size -= 2` and is therefore a 48 kHz algorithm writing a 96 kHz stream
through a 2× linear interpolator. Every rate constant, the bridge filter and
the body biquad transfer verbatim; only the output stage is re-derived,
because that interpolator is an upsampler rather than a filter and copying it
literally would land 1.6 dB darker than the hardware.

MORPH and MACRO are new — Braids welds the nut reflection to −1.0 and the body
resonance to one constant.

The delay lines stay int8 at Braids' own lengths, 1024 + 4096. Braids
quantizes every write to int8 regardless, so float storage would spend four
times the memory on values that have already been rounded — and keeping the
lengths keeps Braids' 11.4 Hz octave-fold floor rather than the 17.2 Hz a
halved float line would force.

`bridge-underflow` and `low-fold` pin the two corners the in-tree extremes
sweep would miss: it renders at note 60, and the bridge tap goes degenerate
from about MIDI 85 upward at HARMONICS 0.

Both copyright lines are carried in `LICENSE` and in each source file.
