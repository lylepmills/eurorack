# Particle Burst

A port of Braids' PRTC — three resonant filters, fixed at intervals above the
played note, each re-struck by a shared decaying noise burst that fires at a
random, density-controlled rate.

The algorithm is Emilie Gillet's `DigitalOscillator::RenderParticleNoise`.

## What it has that Plaits' particle-noise does not

Plaits' own particle-noise engine is the refined descendant of this model,
and the two are structurally different, not just differently tuned.
particle-noise runs six particles whose pitches are each independently
randomised around the played note — a scattered cloud centred on one pitch.
PRTC runs exactly three filters at fixed intervals above the note — +12,
+15.125 and +19 semitones — which is a fixed chord voicing, not a cloud;
COLOR only jitters each filter a little around its own interval, it never
lets them collide or reorder. particle-noise also carries a diffuser and a
post low-pass PRTC has neither of, and exposes an explicit Q control where
PRTC's resonance is a hardwired constant the module never let you touch. So
the two don't overlap: particle-noise is a controllable cloud with variable
resonance and a diffuser; PRTC is a fixed chord of three hard resonators
with a burst-driven decay — and that chord shape is exactly what this port's
MORPH spends its axis on, because particle-noise has no equivalent: it
cannot produce a fixed intervallic voicing at all, only a spread cluster.

## The controls

TIMBRE sets Density — how often a burst fires. COLOR sets Scatter — how far
each filter jitters around its own fixed interval, by a different amount per
filter (filter 1 the least, filters 2 and 3 the most), because that's what
Braids' three different jitter formulas actually do; the asymmetry is kept,
not smoothed over. MORPH is new: Chord width scales the three fixed
intervals from unison, through the module's own voicing at noon, to double
the spread. MACRO is new: Decay reshapes the burst envelope's fixed rate
from a short tick, through the module's own length at noon, to a long
bloom.

## A trap that didn't bite here, but is worth recording

`BEGIN_INTERPOLATE_PARAMETER_1`/`_0` interpolate on the parameter's INDEX,
not its position — reading the suffix as "first parameter" instead of
"parameter 0" is what cost Fold a rewrite (see its own README). For this
model, `parameter_[0]` (TIMBRE) genuinely does drive density and
`parameter_[1]` (COLOR) genuinely does drive the jitter, so reading it either
way happens to land in the same place here. It's flagged in the header
anyway, for the next model that isn't so forgiving.

## Rate

`RenderParticleNoise` ends in `size -= 2`, writing the same sample twice
rather than interpolating between two — so its inner algorithm, RNG draw
included, runs once per two 96 kHz output samples, i.e. at 48 kHz. Braids'
resonator table generator computes its cutoff against a denominator of
48000 (half its own `sample_rate = 96000` constant), so the table is already
normalised for a 48 kHz filter, not the nominal 96 kHz its own file-level
constant might suggest. Both facts point the same way: the port runs the
recursion once per *output* sample at Plaits' native 48 kHz, with no
oversampling, and both of Braids' resonator tables carry over verbatim —
reproduced from the generator and checked against the embedded `uint16`
tables at 0 LSB.

What the port does *not* reproduce is the duplicate write itself. Repeating
each sample twice at 96 kHz is a 2x zero-order hold, and the reference
renderer's halfband decimator only removes the images that creates — it
does not undo the hold's in-band tilt (−1 dB at 14.5 kHz, −3 dB at 24 kHz).
So the port reads brighter than the module in the top two octaves by
exactly that amount, and nowhere else. It's a declared deviation, not a
decimator artefact; those two bands carry 0.6% of the signal's energy.

## Fixed point where fixed point is the sound

Braids' resonator coefficient table stores `2·cos θ` as a 16-bit integer, so
near θ = 0 it sits just under 65536 and a single integer step is a large
step in *pitch*. The module's own low resonators are therefore badly
mistuned, and audibly so: the +12 filter over a note-45 root rings at
227.5 Hz, not 220. Re-deriving that coefficient in floating point — the
obvious "cleaner" choice, and what an earlier revision of this engine did —
tuned the port 75 cents flat of the module at that note. Both tables are
therefore embedded at Braids' own `uint16` width and read back through
Braids' own integer interpolation, along with its truncating
`c * kResonanceFactor >> 15`. The gain table matters for the same reason at
the level end: it holds 1 where the exact value is 1.96, and using the exact
value made the port read a consistent +0.6 dB loud in every register except
the one where that table saturates.

## On matching this one against the module

PRTC is a stochastic burst generator, not a deterministic oscillator — the
same caveat SPEC gives granular-cloud applies here. `tests/ab.json` compares
long-window band statistics (RMS, spectrum) rather than sample-for-sample
waveforms, and the sparsest-density case runs for ten seconds so its
measurement has enough bursts in it to mean something. The `scatter-none`
case (COLOR at 0) is the one exception: with no jitter, the three filters
sit at fixed, known frequencies and the comparison is close to
deterministic.

No case declares a pitch tolerance — a single-lag autocorrelator has no
reliable fundamental to lock onto in three inharmonic resonant peaks excited
by independently-seeded bursts. That isn't a guess: retuning the port's
resonators by up to 75 cents (the fixed-point fix above) left the tool's
reported cents figure identical to the hundredth of a cent at every
moderate note. Pitch here is checked by the spectral centroid of the
unjittered chord instead, which puts the port +5.1 cents from the module at
note 45 and +5.3 at note 82 — the +4.6 cents of `kCorrectedSampleRate`
compensation every ported engine carries, and nothing else.

The sparsest case also carries a widened spectrum tolerance, for a 20-80 Hz
excess that is the shot noise of burst *timing* (about six bursts a second,
from two independent random streams) rather than any tone — moving the
played note one semitone swings that figure by 5 dB, and a longer window
does not converge it. The band that actually carries the fundamental matches
to within 0.3 dB there too. All measured findings; see the header's A/B
NOTES for the numbers.

Declared deviations are listed in the header comment of
`plaits/dsp/engine2/particle_burst_engine.h`.
