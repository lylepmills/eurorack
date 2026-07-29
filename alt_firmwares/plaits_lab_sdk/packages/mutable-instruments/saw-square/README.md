# Saw Square

A port of Braids' SAW_SQUARE — a variable saw and a variable square, driven
from one shared TIMBRE knob and crossfaded by COLOR.

The DSP is Emilie Gillet's `MacroOscillator::RenderSawSquare` driving two
`AnalogOscillator` instances (`OSC_SHAPE_VARIABLE_SAW`, `OSC_SHAPE_SQUARE`).

## The controls, and the trap in reading them

Both oscillators take the SAME parameter — TIMBRE — as their shape control,
in opposite directions: TIMBRE opens the saw's pulse width as it narrows the
square's. COLOR crossfades between them. Fold maps those straight across:
TIMBRE → Shape, COLOR → Blend.

The trap is the same one `fold` hit. `RenderSawSquare` crossfades using
`BEGIN_INTERPOLATE_PARAMETER_1` and `balance = parameter_1 << 1`. The name
looks like "the first parameter"; the macro actually interpolates
`previous_parameter_[1]` toward `parameter_[1]`, which is COLOR. Misread, the
model looks like it drives shape and blend from TIMBRE alone and never
touches COLOR — and it would still render a plausible-looking saw/square mix,
so nothing about it looks wrong from the code. `tests/ab.json`'s
`blend-saw`/`blend-square` pair exists specifically to catch this: get the
crossfade parameter wrong and those two cases land on the wrong waveform
entirely.

## What it has that Virtual Analog does not

Plaits' own Virtual Analog engine (the compiled `VA_VARIANT 2`) already mixes
a variable square and a variable saw — but as two INDEPENDENT oscillators:
TIMBRE sets the square's pulse width, MORPH sets the saw's on an unrelated
tent-shaped curve, and MACRO balances them, with hard sync and a HARMONICS
detune layered on top. Saw Square is the smaller, coupled instrument Braids
actually built: ONE knob morphing a complementary pulse-width pair — open the
saw as the square closes, or the reverse — which Virtual Analog's independent
controls do not reproduce as a single gesture. It also carries the specific
148/256 = 0.578125 fixed balance Braids hand-picked between the two waveforms,
which Virtual Analog has no equivalent of.

## The two new controls

MORPH sets a static phase offset between the saw and the square. Braids' two
`AnalogOscillator` instances share the same `phase_increment_` stream and
never diverge — the module has no way to move them out of lock. At MORPH's
centre the offset is zero (exactly Braids); off centre, the two waveforms'
discontinuities land at different points of the cycle, which detuning (what
Virtual Analog offers instead) does not produce.

MACRO reveals the 148/256 attenuation as a range, from silent to unattenuated,
stock at Braids' fixed ratio. The module bakes that balance in and never lets
the player move it.

## Rate

`RenderSawSquare` has no `size -= 2`, and neither `RenderVariableSaw` nor
`RenderSquare` oversamples internally — each advances phase once per output
sample, unlike the triangle/sine folders in `fold`, which run at an internal
2x. A polyBLEP correction is expressed as a fraction of one sample period, so
it carries from Braids' 96 kHz hardware to this 48 kHz port with no rate
conversion at all — the same non-oversampled family `csaw` already ported.

Both copyright lines are carried in `LICENSE` and in each source file;
declared deviations are listed in the header comment of
`plaits/dsp/engine2/saw_square_engine.h`, and `tests/ab.json` holds the
measured comparison against the module.
