# Cymbal

A port of Braids' CYMBAL: six comparator-summed square oscillators through a
resonant bandpass (the metallic side), crossfaded against clocked
sample-and-held noise through a resonant highpass (the raw side).

The DSP is Emilie Gillet's `DigitalOscillator::RenderCymbal`.

## Not self-enveloping

RenderCymbal never reads `sync` or checks `strike_` -- it is a continuous
texture with no envelope or trigger response of its own, the same as
PARTICLE_NOISE. The percussive, decaying character the model is known for on
the module comes entirely from Braids' own external AD envelope, applied
uniformly outside this function. This port sets `alreadyEnveloped: false`, so
Plaits' own voice-level DECAY/TRIGGER envelope is what shapes a Cymbal patch
into a struck hi-hat -- the same mechanism Fold and Particle Burst already
rely on.

## The controls

TIMBRE sets the cutoff shared by both filters; COLOR crossfades between them.
Cymbal maps those to Tone and Color, keeping the module's two axes intact.
Spread and Resonance are the two new ones: Spread scales the five detuned
partials' distance from the root (the module's own hardwired ratios sit at
Spread 0.5, unison at 0, double the spread at 1); Resonance scales both
filters' damp coefficient around the module's own fixed setting (0.5 is the
module exactly).

## Rate

RenderCymbal ends in a plain `while (size--)`, not `size -= 2` -- a genuinely
96 kHz-native algorithm, with no further internal oversampling. Its two
coefficient tables (`lut_svf_cutoff`/`lut_svf_damp`) are generated at Braids'
96 kHz, the same tables `kick_engine.h` documents needing full re-derivation
for. A first version of this port followed that same re-derivation strategy
(the cutoff coefficient recomputed at Plaits' own 48 kHz rate, with a fresh
1/8-of-48-kHz cap) and it measured badly at TIMBRE's fully-open end -- the
re-derived cap lands at HALF the absolute cutoff Braids itself reaches there,
because 12 kHz sits at a much more extreme fraction of this port's lower
Nyquist than it does of Braids' own. Unlike `kick_engine.h`'s pitch-tracked
cutoff, which rarely nears its own cap in practice, TIMBRE's cap is Cymbal's
ordinary fully-open setting, so the mismatch was not a rare edge case.
Instead, this port 2x oversamples the whole excitation-and-filter chain to
reach Braids' own 96 kHz operating rate exactly and evaluates the SAME closed
form Braids does, unmodified -- the same fix fold's folders needed, for the
same underlying reason. The two filters' resonance is hardwired in Braids to
two fixed constants the module never sweeps, so those aren't re-derived at
all -- they're read straight off Braids' own compiled damp table at the two
exact indices the module's hardcoded resonance settings land on.

## Lineage

Plaits' own analog-hi-hat is CYMBAL's refined descendant, and the two share
the six-square-comparator idea, but not much past that: different partial
ratios; a fixed 24x-root noise clock instead of a noisiness-dependent one; and,
where analog-hi-hat mixes its noise into the metallic signal after a single
shared bandpass, CYMBAL filters the two sides separately -- bandpass on the
metallic side, highpass on the raw side -- and crossfades the filtered pair, so
Color sweeps between two differently-coloured textures rather than toward plain
noise. CYMBAL also has no envelope of its own, and neither of analog-hi-hat's
two internal voices offers a resonance axis independent of tone the way this
port's Resonance macro does.

Both copyright lines are carried in `LICENSE` and in each source file;
declared deviations are listed in the header comment of
`plaits/dsp/engine2/cymbal_engine.h`, and `tests/ab.json` holds the measured
comparison against the module.
