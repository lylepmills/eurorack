# Fold

A port of Braids' FOLD — a sine wavefolder and a triangle wavefolder run in
parallel and crossfaded, which is the model Plaits' waveshaping engine grew
out of.

The DSP is Emilie Gillet's `MacroOscillator::RenderSineTriangle` driving
`AnalogOscillator::RenderSineFold` and `RenderTriangleFold`.

## What it has that Waveshaping does not

Waveshaping runs one shaper — a folder into a wave shaper, MORPH sweeping the
shape — over a source whose amplitude is already bounded. Fold runs two fixed
curves at once, and the curves are the instrument. `tri_fold` is
`sin(pi * (3x + (2x)^3))`, which keeps adding lobes for as long as you drive
it; `sine_fold` is a windowed `sin(8*pi*x)` blended out into `atan(3x)`, so
past the window it saturates instead of folding. Crossfading between the two is
the gesture the Plaits engine has no control for.

## The controls, and the trap in reading them

TIMBRE sets the fold depth for both folders (`gain = 2048 + p1 * 30720 >> 15`),
and COLOR crossfades between them. Fold maps those to Fold and Blend, keeping
the module's two axes intact; Symmetry and Drive are the two new ones, both
stock at noon.

The trap is worth recording, because the wrong reading renders plausibly.
`RenderSineTriangle` crossfades using `BEGIN_INTERPOLATE_PARAMETER_1` and
`balance = parameter_1 << 1`. The name looks like it refers to the first
parameter; the macro actually interpolates `previous_parameter_[1]` toward
`parameter_[1]`, which is COLOR. Read the other way, the model appears to drive
depth and blend from TIMBRE together and to ignore COLOR entirely — and it
still sounds like a wavefolder, so nothing about it seems wrong.

What caught it was the A/B. Against the module, that version put the
fundamental 14.8 dB down with the third harmonic on top of it, where Braids has
the fundamental strongest and the third 3.7 dB below. Corrected, h1, h3 and h5
land within 0.4 dB. The measured cases are in `tests/ab.json`, including one at
each end of Blend, since that is the axis the mistake was on.

## Rate

`RenderSineTriangle` does not end in `size -= 2`, so it is a 96 kHz algorithm,
and each folder is internally 2x oversampled — Braids folds at 192 kHz. The
port runs 4x from 48 kHz to reach the same 192 kHz internal rate, so the
pitch-dependent depth guards transfer as written rather than needing to move.
Checking that the internal rates matched, instead of assuming the thresholds
carried, is the step whose absence cost `fluted` its slot.

The decimator is Plaits' 8-tap overlap-add `lut_4x_downsampler_fir` rather than
Braids' 2-tap box average.

Both copyright lines are carried in `LICENSE` and in each source file; declared
deviations are listed in the header comment of
`plaits/dsp/engine2/fold_engine.h`, and `tests/ab.json` holds the measured
comparison against the module.
