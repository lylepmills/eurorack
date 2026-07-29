# Granular Cloud

A port of Braids' GRANULAR_CLOUD — four sine grains under Hann windows, each
one respawning on a coin flip when its window runs out and taking a fresh
random pitch when it does.

The DSP is Emilie Gillet's `DigitalOscillator::RenderGranularCloud`.

## What it is that the Plaits granular engines are not

Plaits has two engines nearby and this is neither of them. **Granular Formant**
is two grainlet oscillators and a Z-oscillator: its grains are formant bursts
locked one-per-cycle to a carrier, so it is a synchronous formant synth and
stays pitched wherever you put it. **Particle Noise** is six random impulses
through resonant band-passes — stochastic, but the grain is a filter's impulse
response and the random variable is the filter frequency.

Granular Cloud is the asynchronous one. Grain onsets are a Bernoulli process,
so grains overlap freely because nothing is synchronising them, and the random
variable is the grain's own **pitch**. Open Scatter and the same four voices
leave pitch behind entirely. Measured at note 72 with the longest grains,
autocorrelation finds 87.2 Hz at COLOR 0 (a sixth-subharmonic lock on the
523 Hz carrier), 562 Hz at COLOR 0.5 — pulled sharp, because the scatter
reaches 1.496× up against 0.75× down — and nothing at all at COLOR 1. The port
reproduces that: 87.4, 563.0, none. Neither Plaits engine has a control that
does it.

## The controls

TIMBRE is grain length and COLOR is the pitch scatter, and both come off the
knobs directly — this model has no `INTERPOLATE_PARAMETER` macro in it, so the
suffix trap that cost `fold` a rewrite does not arise here. The mapping was
still read off the source with line numbers rather than assumed; they are in
the header comment.

`lut_granular_envelope_rate[parameter_[0] >> 7]` sets the envelope rate, so
TIMBRE is grain length: 10.7 ms at zero down to 0.67 ms at full. Short grains
die sooner and so respawn sooner, which is why the one knob moves length and
density together. `pitch_mod = Random::GetSample() * parameter_[1] >> 16` is
the scatter, applied as a linear multiple of the phase increment and
deliberately asymmetric — the `>> 8` on the downward side against `>> 7` on
the upward one gives a 0.75× to 1.496× spread.

Shape and Density are the two the port adds, both stock at noon. Shape slides
where the Hann window peaks, which Braids fixes at the middle; Density opens
the respawn probability that Braids fixes at one in four.

## Rate

`RenderGranularCloud` does not end in `size -= 2` — it is a plain
`while (size--)` writing one sample per pass, with no internal oversampling —
so it is a 96 kHz algorithm and its rate constants do not transfer. Two of them
had to move: the envelope increment doubles (Braids' `<< 3` becomes `<< 4`
against the same `1 << 24` endpoint, so grain length in seconds is unchanged),
and the respawn loop's period halves from Braids' 24-sample block at 96 kHz to
12 samples at 48 kHz — the same 250 µs slot. Plaits' block happens to be 12, so
on a stock block the port's scheduler fires exactly where Braids' does, but the
port counts samples rather than blocks so an unusual block size cannot change
the density. Verified: driven at 1, 7, 12 and 24 samples per call, the engine
measures −0.15 dB AC RMS and 0.30 dB spectrum against the reference in all four
cases.

## Why the A/B is statistical

The model draws from `Random::GetWord()` and `Random::GetSample()`, so it is
reproducible only against a generator seeded and stepped identically. The port
does carry Braids' LCG, seeded from stmlib's own initial `0x21` and consuming
exactly one word per respawn test and one more per successful respawn, and
Braids spends no randomness anywhere else in this shape — so grain onsets and
grain pitches line up with the reference. That is measured, not assumed: the
port correlates **+0.998** with the reference over the first 50 ms, and its
1 ms RMS contour correlates **+0.33 to +0.78 in every second** of a five-second
render, against **+0.01 to +0.04** for the same engine reseeded to three
arbitrary other values.

Carrier phase does not stay locked. Braids takes its increment from its 96 kHz
pitch table and the port from `NoteToFrequency()`, a fixed +4.61 cents off it,
and grain phase is never reset — about 1.5 cycles of drift over five seconds at
note 45. Waveform correlation measured in 50 ms slices at t = 0, 1, 2, 3, 4 s
runs +0.998, −0.50, −0.82, +0.88, +0.15: large in magnitude, rotating in sign.
Same grains, turning relative to each other.

How much of the A/B leans on that seed match was measured case by case, by
rebuilding with `kGranularCloudSeed` set to `0x12345`, `0xdeadbeef` and `0x7`
and re-running the whole suite. It splits:

* The short- and mid-grain cases converge without it. `stock-mid`,
  `unison-short`, `scatter-full` and `high-note` stay inside their declared
  tolerances at all three foreign seeds — AC RMS −0.46 to +0.14 dB, spectrum
  0.09 to 0.40 dB, against −0.15 to +0.08 dB and 0.10 to 0.30 dB seeded
  correctly. Those four are a test of the model.
* The long-grain cases do not. `unison-long`, `pitched-mid` and `pitched-high`
  sit at TIMBRE 0, where a grain is 10.7 ms and five seconds does not hold
  enough independent grain events for the level statistic to settle. Reseeded
  they swing −2.20 to +2.94 dB of AC RMS and up to 0.79 dB of spectrum, which
  would fail the declared ±0.5 dB. Their **level** tolerances therefore do
  depend on the port drawing Braids' own sequence. It does — that is itself
  part of being faithful — but it is a stricter claim than "the statistics
  converge", and should not be read as the weaker one.

The R5 rate conclusion does not depend on the seed either way: corrected pitch
on `pitched-mid` / `pitched-high` reads −0.3 / +0.1 cents seeded, and −0.3 to
+0.8 cents across all three foreign seeds.

So `tests/ab.json` compares band energies over long windows rather than
waveforms, and declares a pitch tolerance only on the two cases where an f0
exists to measure — at note 45 a grain is barely one carrier cycle long and the
onsets are random, so the autocorrelator returns its ceiling on both sides,
which would pass a tolerance for the wrong reason.

Measured across the seven cases: **AC RMS within 0.15 dB, spectrum within
0.30 dB**, pitch **+0.1 and −0.3 cents** on the two pitched cases. Every band
below 1.3 kHz carrying 1% or more of a render's energy is inside 0.22 dB. The
near-empty bands are not: at `pitched-high`, where 95% of the energy is in one
band at 320–640 Hz, the 40–160 Hz bands hold 0.1–0.2% each and read 4.5 to
5.2 dB low, and `pitched-mid` and `scatter-full` read 0.4–0.5 dB low below
160 Hz. Sparse-band statistics on a stochastic render, far too small to move
the energy-weighted figure.

The residual that carries energy is above 2.5 kHz, in bands holding 0.3–0.4%
each, where the port reads up to 24 dB low (6 to 24 dB on `stock-mid` and
`scatter-full`, 1 to 4 dB on the long-grain cases) — and it is not a decimator
difference. The reference's floor there is the same at note 12 as at note 45,
and the same whether the reference is rendered at 96 kHz or decimated to 48, so
it depends on neither pitch nor rate. It comes from the envelope: Braids reads
its 257-entry window without interpolating, and flooring the index costs 0.0118
peak and 0.0049 RMS of window amplitude — 46 dB below the window peak, roughly
constant across the whole grain-length range — which modulates the carrier into
a broadband floor. This port evaluates the window continuously and does not
generate it. Reproducing it would be one flooring of the position in
`GrainEnvelope`, and is deliberately not done: it is quantisation noise, not
part of the model.

## Tables

Neither of Braids' two tables is embedded, because neither is read in the inner
loop, and both substitutions were measured rather than asserted:
`lut_granular_envelope_rate` reproduces at **0 LSB** across all 257 entries from
`trunc(2048 * SemitonesToRatio(i * 12/64))`, and `lut_granular_envelope`
reproduces to **0.99 LSB of 32767** (max 3.03e-5, worst at index 10) from
`0.5 - 0.5 * Sine(e + 0.25)` read off Plaits' existing sine LUT.

Both copyright lines are carried in `LICENSE` and in each source file; declared
deviations are listed in the header comment of
`plaits/dsp/engine2/granular_cloud_engine.h`.
