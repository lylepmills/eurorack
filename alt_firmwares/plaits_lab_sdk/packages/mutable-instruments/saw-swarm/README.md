# Saw Swarm

A port of Braids' SAW SWARM — seven detuned, raw (non-band-limited)
sawtooths summed, driven through a soft clip, and carved by a
note-tracking resonant filter.

The DSP is Emilie Gillet's `DigitalOscillator::RenderSawSwarm`.

## What it has that Swarm does not

Plaits' own Swarm engine shares only the word. It is a granular cloud of 8
saw/sine voice pairs, each one gliding between randomized "grain"
endpoints under its own envelope — an evolving, never-quite-settling
texture. Saw Swarm is the opposite kind of instrument: seven voices at
fixed, stable detune offsets from one held pitch, deliberately using RAW
phase-truncated sawtooths — one of Braids' "digital" oscillators, not one
of its band-limited "analog" ones, so it aliases at the top of its range
the way Swarm never does — summed, soft-clipped, and then carved by a
resonant filter whose cutoff COLOR controls. It's a static chorus-into-filter
voice, not a cloud, and the two engines have nothing in their signal paths
in common beyond "several detuned saws."

## The controls, and the filter's story

TIMBRE sets the detune spread between the seven saws (quadratic in TIMBRE,
so it opens gently at first and widens quickly). COLOR sets the filter's
cutoff, tied to the note with two different slopes either side of a pivot
near COLOR=1/3 — below it the cutoff falls away from the note twice as
fast as it climbs above it. Saw Swarm maps those straight across to Detune
and Color; Filter and Resonance are the two new axes, both stock at noon.

Braids' filter computes lowpass, bandpass and highpass every sample and
wires out only highpass — an unreachable-by-the-module choice. Resonance
opens up the filter's own fixed damping as a control (centred on the
module's actual value); Filter sweeps the same filter's response from
lowpass, through Braids' own highpass at the centre detent, to bandpass.
AUX plays the mirrored sweep, so it's always the tap MAIN isn't favouring.

The filter topology itself isn't Braids' literal one, and that's a
measured decision, not a shortcut. Braids runs SAW SWARM's naive
(Chamberlin) state-variable filter at its native 96 kHz; the first version
of this port kept that exact filter and just re-derived its cutoff
formula for 48 kHz. The A/B caught what that missed: a naive SVF's gain,
at a fixed damping, grows sharply as the cutoff-to-samplerate ratio
climbs — and reproducing the SAME cutoff-in-Hz at half Braids' sample
rate doubles that ratio. Measured at note 48, COLOR maxed (cutoff ~5.3
kHz): the naive port ran about 2.2x hotter than the module and hard-clipped,
even though the exact same cutoff at Braids' own 96 kHz sits nowhere near
that edge. So the port uses Plaits' own zero-delay-feedback filter instead,
carrying over the identical fixed operating point (its damping term plays
the same role Braids' does) on a topology that doesn't have this
rate-relative peaking. It reproduces the module's actual sound; it isn't
the same code shape.

## Rate

`RenderSawSwarm` doesn't end in `size -= 2` and has no internal
oversampling of its own — a genuine 96 kHz algorithm, so its rate-dependent
constants need re-deriving rather than transplanting. The cutoff-to-note
mapping is a straightforward re-derivation (Plaits' own note-to-frequency
machinery does the equivalent conversion, already sample-rate corrected).
The filter coefficient is the one that turned out not to be — see above.

## Measured

Six A/B cases in `tests/ab.json` cover the detune range, both ends of the
filter's color mapping, and two octaves up where the raw sawtooths alias
hardest. All six hold to the tight ≤1.5 dB / ≤10 cents / ≤1.5 dB spectrum
bar — AC RMS +0.05 / −0.53 / +0.04 / +0.03 / −0.43 / −0.18 dB across
stock-mid, unison, wide-chorus, color-low, color-high and high-note.

An earlier revision missed by more than that on the near-unison case and
widened its tolerance to 3 dB, blaming the two filters' transient
response. That was wrong. The actual cause was this engine starting all
seven voices at phase zero, where the module always starts six of them
randomized; at near-unison detune the voices beat apart so slowly that a
three-second render never leaves that opening pile-up behind. Matching
the module's initial phases fixed it, and no case needs a widened
tolerance now.

What remains is the expected decimator residue: about +2 to +4 dB in the
top octave (20.5–24 kHz) and ~+1 dB below it, because the module's raw
sawtooths alias at 96 kHz and get filtered before the reference is
decimated, while this port's alias at 48 kHz and fold back in. The one
place the filter substitution is visible is the near-unison case's
fundamental, which at that color setting sits on the filter's steep
skirt where the two topologies differ most.

Both copyright lines are carried in `LICENSE` and in each source file;
declared deviations are listed in the header comment of
`plaits/dsp/engine2/saw_swarm_engine.h`, and `tests/ab.json` holds the
measured comparison against the module.
