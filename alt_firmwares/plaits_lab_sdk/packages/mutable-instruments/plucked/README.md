# Plucked

A port of Braids' PLUK — three round-robin Karplus-Strong voices. A strike
drops a burst of noise into a resonant delay loop and moves on to the next
voice, so up to three plucks ring together while the newest one keeps
following the played pitch.

The DSP is Emilie Gillet's `DigitalOscillator::RenderPlucked`.

## What it has that inharmonic-string does not

Plaits' own inharmonic-string engine is the same three-voice round-robin
shape, built from a far more elaborate string model — a real dispersion
all-pass, an explicit brightness filter, an independent damping control.
PLUK has one thing it does not: past noon, Damping starts skipping filter
passes around the loop at random instead of smoothly attenuating them. A
skipped position keeps whatever it held from the previous lap, so different
parts of the loop drift out of phase with each other — a grainy, irregular
detuning no smooth filter combination produces. That mechanism, and the
variable-width noise burst that is PLUK's only excitation control, are what
this port keeps rather than reproducing inharmonic-string's smoother
machinery a second time.

## The controls

TIMBRE is Damping. Below noon it adds extra attenuation on top of the
string's own decay — a muted thump at the bottom, easing off toward normal
ringing at noon. Above noon the extra attenuation is gone and the stochastic
skipping takes over instead, stretching and detuning the decay toward a
metallic rattle at the top. COLOR is Pluck — how much of one string period
gets struck with fresh noise, from a narrow flick to most of the cycle, read
once at the strike.

Spread and Stretch are the two new ones, both stock at noon. Spread offsets
each round-robin voice by a fixed interval, so three strikes build a small
arpeggiated chord as they decay together — unison, exactly the module, at
noon. Stretch scales how far Damping's stochastic detuning reaches: fully off at
the bottom, easing up to the module's own ceiling at exactly noon, and
further than a real PLUK goes above it.

## Rate and delay length

`RenderPlucked` ends in `size -= 2`, a 48 kHz algorithm doubled to a 96 kHz
output stream, so the port runs once per Plaits output sample with no
oversampling. Braids addresses a fixed, small delay buffer per voice through
an NCO whose resolution is shrunk for high notes so that the loop keeps
roughly one filter update per output sample at any pitch — a fixed-point
technique for keeping a small buffer's resolution matched to the real
period, not a claim about filtering density. The port reproduces that effect
directly: a `plaits::DelayLine` addressed by the real period in samples,
fractionally interpolated, one filter update per output sample. Delay length
in samples is `1.0f / NoteToFrequency(note)`, never multiplied by
`kCorrectedSampleRate` a second time.

The buffer is 1024 samples per voice — the same *number* of slots Braids
uses, but not the same reach. Braids' 1024 slots stretch to cover one whole
period at any pitch; the port's are pinned to one sample each, so below
where a period exceeds the buffer (roughly MIDI 30, ~47 Hz) the period is
clamped and the fundamental is not reachable. That is a limitation of this
port, **not** one inherited from the module: the reference plays note 21 at
a measured 27.507 Hz, dead in tune, where the port is +925 cents sharp — see
`low-register` in `tests/ab.json`.

The same difference has a second cost across the whole range. Braids runs
its loop filter once per buffer slot the read pointer crosses, which is
`size / period` times per output sample — a ratio that sweeps (1, 2] across
each power-of-two band, hitting 2 just under 93.75 / 187.5 / 375 / 750 Hz.
The port always runs exactly one, so it under-damps by up to 2x near the top
of a band: note 45 (ratio 1.17) measures +1.55 dB / 1.18 dB, note 54 (ratio
1.97) +3.41 dB / 4.59 dB. Adopting Braids' own scheme — a `size`-slot table
per voice with a phase accumulator advancing `size / period` per sample —
would close both this and the low-register clamp.

The fractional read is Hermite, not linear — a plain 2-tap linear read, fed
back through the loop hundreds of times a second, measured 4-6 dB of extra
high-frequency loss in the sustained decay that Braids' integer-indexed loop
(which only interpolates once, on the output, never inside the feedback
path) does not have.

## A shared bug this port found and fixed

`braids/test/render_braids_model.cc`'s `MacroOscillator osc` was declared
inside `main()`. Its delay-line memory is not part of the state `Init()`
zeroes, and on the real module `osc` is a file-scope global — zeroed for
free. A stack-local copy is not, so every render carried a never-struck
voice's uninitialized stack memory straight into the (unconditionally
summed) output, which — pinned near full scale by the output clip — read as
a reference that never decayed, at any TIMBRE or note. Moving `osc` to file
scope (matching `braids.cc`'s own declaration) fixed it; verified against an
instrumented rebuild that the corrected reference decays exactly as the
TIMBRE/loss formula predicts. This likely affects every other Braids model
in this wave whose first strike leans on a zeroed delay line — worth a
blanket re-A/B once they land, not just a note here.

## Measured

Pitch is within 0.1–1.6 cents everywhere the port's own fundamental is
reachable — the period/rate math checks out. AC RMS runs −0.7 to +3.4 dB and
full-band spectrum 0.9–4.6 dB across the eight `tests/ab.json` cases;
`ladder-worst` (note 54) and `high-register` (note 69) are the worst, both
from the filter-rate difference above rather than from a level or pitch
error. `low-register` drops the `cents` check because the port cannot reach
that fundamental at all — the +925 cents there is a real gap against a
module that plays the note in tune, exempted only to keep the case's level
and spectrum figures readable, and worth closing rather than keeping. Its
level and spectrum still pass unwidened.

With nothing patched to TRIG the engine strikes once on its first render,
following the sibling stock engines' "unpatched fires once" convention —
without it, a triggered model like this one is silent by construction.

Both copyright lines are carried in `LICENSE` and in each source file;
declared deviations are listed in the header comment of
`plaits/dsp/engine2/plucked_engine.h`, and `tests/ab.json` holds the
measured comparison against the module.
