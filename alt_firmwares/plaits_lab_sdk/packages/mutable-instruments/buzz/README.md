# Buzz

A port of Braids' BUZZ — two unison-tuned band-limited pulse-train
oscillators, summed. The harmonic content both oscillators share comes from
a wavetable lookup, not a live additive sum.

The DSP is Emilie Gillet's `MacroOscillator::RenderBuzz` driving
`AnalogOscillator::RenderBuzz` twice, once per voice.

## What it has that Harmonic does not

Plaits' own additive engine, Harmonic, runs 24 live sine partials and shapes
each one's amplitude with a continuous centroid/slope/bump envelope — a
smoothly weighted spectrum. Buzz's spectrum is a boxcar: every harmonic up to
a cutoff at equal weight, then silence, because that is what the underlying
table is — a periodic Dirichlet kernel, `sin(pi*i*m)/(m*sin(pi*i))`, carrying
`(m - 1) / 2` harmonics at unit weight and nothing above. That is the classic
"buzz" edge Harmonic's smooth envelope cannot make no matter how its knobs are
set. This
is a real difference in the generating law, not only in the implementation.

## The controls, and the trap in reading them

TIMBRE picks how many harmonics BOTH oscillators read (Braids'
`analog_oscillator_[0].set_parameter(parameter_[0])` and
`analog_oscillator_[1].set_parameter(parameter_[0])` — the same parameter,
twice). COLOR only ever detunes the second oscillator's pitch
(`analog_oscillator_[1].set_pitch(pitch_ + (parameter_[1] >> 8))`), by under
one semitone at full travel. It never touches harmonic content.

That is worth stating carefully because the wrong reading is not a macro-name
trap like FOLD's — it is a plausibility trap. A port that gave oscillator 2
its own COLOR-driven harmonic axis, making BUZZ into a two-band FOLD, would
render fine in isolation. It just is not what the module does. The `ab.json`
case `color-half-detune` exercises exactly this axis: COLOR at mid-travel with
TIMBRE fixed, so a harmonic-content leak into the second oscillator would show
up as a spectral mismatch even though the note and register stay put.

## Rate

`AnalogOscillator::RenderBuzz` has no `size -= 2` and no oversampling of its
own — one wavetable read per loop iteration — so by SPEC R5 it is a 96 kHz
algorithm whose rate-dependent constant has to be re-derived rather than
transplanted. That constant is the wavetable: each of the 15
`WAV_BANDLIMITED_COMB_*` zones is generated for a 96 kHz sample rate, with its
harmonic count chosen so the top harmonic sits just under a 48 kHz Nyquist at
that zone's own reference pitch.

What that costs at 48 kHz is not the zones' own harmonics — the zone index
tracks the played note, so the stack actually read tops out around 17–27 kHz
whatever you play. It is the image of the 256-point table read, which sits
around `256 × f0`. Measured on the reference renderer at its native 96 kHz:
at note 48 with TIMBRE at 1, `256 × 130.8 Hz` = 33.5 kHz and 0.49% of the
render's energy sits above 24 kHz, all of which a 48 kHz table read would fold
into the audible band. (At a high note there is nothing up there to fold at
all: note 84 with TIMBRE at 1 measures 0.0003% above 24 kHz, its own image
having already landed near 20 kHz in Braids' 96 kHz render.) The port runs
both oscillators' zone lookup and phase accumulation at 2x and decimates
through the same
`[0.25, 0.5, 0.25]` halfband `z-filter` uses for the identical
no-internal-oversampling situation.

## Tables

All 15 zone tables (257 entries each) reproduce at 0 LSB of deviation from
`braids/resources.cc` — every entry checked programmatically against the
closed form in `waveforms.py`, not sampled. Embedded rather than evaluated per
sample: the closed form is a ratio of two sines with a singularity at phase 0
that needs
guarding, and table reads are the cheapest operation this hardware has —
paying a sin-over-sin per sample, eight times over between two oscillators and
2x oversampling, would not be.

## The fourth macro

MACRO widens HARMONICS' detune reach past the module's own sub-semitone
ceiling, with a detent at noon that reproduces the ceiling to within a third
of a cent (the module's own offset is a 128-step staircase ending at 99.22
cents; the port's is continuous and ends at 100.00 — see the declared
deviations). MORPH
gives the second oscillator the independent harmonic-content axis the trap
above almost invented by accident — zero at noon (both voices read the same
TIMBRE-chosen zone, exactly Braids), a bipolar zone offset away from it.
Braids' single TIMBRE knob cannot separate the two voices' harmonic content at
all; MORPH is that missing control, built deliberately instead of by
misreading two lines of source.

Both copyright lines are carried in `LICENSE` and in each source file;
declared deviations are listed in the header comment of
`plaits/dsp/engine2/buzz_engine.h`, and `tests/ab.json` holds the measured
comparison against the module.
