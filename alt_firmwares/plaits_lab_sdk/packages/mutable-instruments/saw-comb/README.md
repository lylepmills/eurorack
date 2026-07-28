# Saw Comb

A port of Braids' saw-into-comb hybrid: an `AnalogOscillator` saw written into
the buffer, then `DigitalOscillator::RenderComb` filtering it in place.

`RenderComb` carries no `size -= 2`, so it is a 96 kHz algorithm — and a
4,096-tap line at 48 kHz reproduces Braids' 8,192 taps at 96 kHz exactly: the
same 85.3 ms, the same 11.72 Hz floor, and so the bottom of TIMBRE clamps
below MIDI ~70 just as it does on hardware.

What separates this from `reed-pipe` and `loopback`: the comb pitch is
decoupled ±64 semitones from the note, the feedback is genuinely **bipolar**,
and the exciter is band-limited. MORPH and MACRO are new.

**At HARMONICS noon the engine is not silent.** The write-back is `0.5*in` and
the output is `0.5*dry` plus one echo — a fully audible FIR comb. Resonance
runs from inverted, through that, to ringing.

The bright half of MACRO needed care. The in-loop shelf has an HF gain of
`1 - 0.788*tilt`, reaching 1.473 at the extreme, so a fixed pre-scale leaves
net HF loop gain above unity and the comb self-oscillates well below the
HARMONICS setting that should do it. The feedback is divided by the actual
shelf peak instead, with a block-rate bound as a backstop, and `loop-gain`
pins that corner.

Both copyright lines are carried in `LICENSE` and in each source file.
