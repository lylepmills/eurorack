# Wave Paraphonic

A port of Braids' WAVE PARAPHONIC — four wavetable voices reading one scan
line, tuned by Plaits' active chord table.

The DSP is Emilie Gillet's `DigitalOscillator::RenderWaveParaphonic`.

## What it has that Chords does not

Chords already runs five wavetable voices over a curated wave line — that is
what the upper half of its waveform morph turns into — and its inversion
control already fans voices by octaves. So this port does not repeat either.
What Braids brings is a different instrument built from the same parts:

- **A 33-step scan line over a different selection of waves**, crossfaded
  rather than stepped.
- **No registration or per-voice level shaping at all.** Four equal voices.

And two controls the module has no knob for:

- **Fan** offsets each voice further along the scan line, so the four read
  different waves. It is signed around noon: below noon the root keeps the
  brighter wave, above it the top of the chord does.
- **Spread** scales every loaded chord interval continuously, from a unison at
  zero through the table verbatim at noon to double width at maximum. Chords
  can transpose a voice by an octave; it cannot widen a fifth into a ninth.

The original Braids 17-row list remains available as the **Braids Wave
Paraphonic** chord table. Its 1/128-semitone offsets are rounded to the nearest
cent because that is the shared table format; the two near-unison rows become
`[0, 2, 3, 5]` and `[0, 13, 25, 38]`.

## The controls

Wave is Braids' TIMBRE. Chord deliberately diverges from Braids' COLOR:
`ChordBank::set_chord` quantizes HARMONICS to one position in the active table,
using the same firmware option as Chords, String Machine, Chiptune and Helix.
All four stored offsets sound; `arpLength` remains an arpeggiator hint and is
ignored here, as it is by String Machine.

Fan and Spread are the two added axes. Fan is neutral at noon. Spread is 1.0
at noon, where it reproduces the selected table; below and above noon it scales
the table's ratios in log-pitch space.

## Rate

`RenderWaveParaphonic` ends in `size -= 2`, which looks like oversampling and
is not: each of the two writes gets its own phase advance and its own table
reads, so it is a plain 2× unrolled loop and the algorithm runs at Braids' full
96 kHz. (`RenderWavetables`, two hundred lines earlier, halves the increment
and takes two sub-steps per output — *that* one is 2× oversampled.)

Nothing in the function is rate-dependent. There is no filter, no envelope and
no time constant: the detunes are pitch, the scan is a table index, and the
only quantity that moves with the sample rate is the phase increment, which
Plaits derives itself. So the model transfers to 48 kHz verbatim.

## Aliasing

Neither side band-limits anything, and the port is the worse of the two.

Braids point-samples a 129-byte wave with linear interpolation at 96 kHz. The
port does the same at 48 kHz. A 128-point table carries to the 64th harmonic
and there is no mipmap, so the readout is clean below 375 Hz (MIDI 66.2) where
Braids is clean below 750 Hz (MIDI 78.2). Above MIDI 66 this port aliases more
than the module: the `high-note` A/B case, at MIDI 69, measures +2.1 dB at
5–10 kHz, +4.2 dB at 10–20 kHz and +9.1 dB above 20 kHz.

An earlier design for this engine claimed the opposite — that Plaits'
f0-tracking readout filter suppressed aliasing better than the hardware does at
double the rate. It does not. That filter's cutoff is `min(128 * f0, 1.0)`,
which saturates at 1.0 for every f0 above 375 Hz, so across the whole range
where a wavetable oscillator needs help it is pass-through. Below 375 Hz it is
not neutral either — at MIDI 45 the pair of one-poles is 4.5 dB down by the
20th harmonic. Braids has no such filter, so this engine has none, and it makes
no anti-aliasing claim.

## The waves, and what the port does about them

Braids' wave data is not in the Plaits firmware, and vendoring the 33 waves
this model scans would cost about 4.3 KB — on a build with a few hundred bytes
of headroom. So the port substitutes from `wav_integrated_waves`, which is
already linked, for 66 bytes of index and gain table.

That is possible because Plaits' third wave bank is *generated from the same
data*: `plaits/resources/wavetables.py` reads `waves.bin`, which is
byte-identical to Braids' own `wt_waves` — all 33,024 bytes, checked rather
than assumed. 16 of the 33 scan slots therefore have an exact counterpart in
the 192-wave bank. The other 17 do not, and take the nearest wave in the bank
instead: shortlisted by an offline symmetric per-octave-band comparison, then
chosen by measuring each shortlisted candidate through the A/B itself.

Before the chord-table routing change, the wave path was parked on each slot
in turn for 8 s at MIDI 45 on Braids' stock chord, against the module:

| | spectrum | AC RMS |
| --- | --- | --- |
| the 16 exact slots | 0.05 – 0.29 dB, median 0.10 | −0.22 – +0.09 dB |
| the 17 substitutes | 0.57 – 2.24 dB, median 1.12 | −0.24 – +0.09 dB |

Pitch is within 0.5 cents on all 33. The worst slot is 31 at 2.24 dB; the far
end of Wave, and the run just above the middle, are exact.

Two things "exact" does not mean. `wavetables.py` replaces each imported wave's
*phase* spectrum with a uniform −90°, keeping only its harmonic magnitudes, so
15 of the 16 exact slots carry the Braids wave's harmonic levels precisely
while being a different waveform with a much higher crest factor. Only the last
slot comes from the family the generator imports unmodified. That shows up when
waves are crossfaded: a single exact wave A/Bs at 0.37 dB, while a 40%
crossfade between two exact waves reads 0.65 dB. And `wavetables.py`
peak-normalises everything, which Braids' 8-bit waves are not — hence the
33-entry gain table, spanning 0.734 to 1.703 in 1/128 steps.

Those gains multiply each of the two waves *before* the crossfade, not the
crossfaded result. It is the same distinction as `Crossfade` on the module,
which mixes the two waves at whatever level its own 8-bit data has, and it
matters most where neighbouring slots differ sharply — slot 10 is 1.703 and
slot 11 is 0.945. `wave-gain-step` parks the scan midway between exactly those
two, both of them exact wave matches, so the case measures the crossfade
arithmetic with nothing else in it.

## The strike, and the core file that is not edited

Braids randomises all four phases on a strike, which is what stops the
near-unison chord rows opening as one comb-filtered impulse. The design this
engine comes from reached that by adding a `set_phase()` to the shared
`wavetable_oscillator.h` — an edit no package digest covers. This engine needs
no such edit, because it does not drive a `WavetableOscillator`: it owns four
float phases and a small readout of its own, so randomising them is ordinary
engine code.

It draws from the same `stmlib::Random` and arms the strike as a flag consumed
inside `Render`, exactly as the module does, so four words are drawn once
regardless of how often `Init`/`Reset` ran first. A probe on `Random::state()`
around the first render leaves both the reference renderer and this engine at
the same value, four steps from the seed — so from a cold start the port's
opening phases are the module's own.

## Reading the measurements

In that historical measurement, the stock chord's octave and twelfth sit 2.3
and 3.9 cents off exact ratios, so
their coincident harmonics beat at a fraction of a hertz and a short render
catches an arbitrary point of it. One exact slot measured over 2 / 4 / 8 / 16 s
reads 1.49 / 0.29 / 0.23 / 0.17 dB, and on the near-unison row — where the four
voices share harmonic indices — 0.05 dB at 2 s. The per-slot figures above are
at 8 s for that reason.

The committed cases run at 3 s, which is short by that argument, so it was
checked rather than assumed: `stock-mid` reads 0.65 / 0.62 / 0.64 dB at 3 / 8 /
16 s and `wave-gain-step` 0.44 / 0.50 dB at 3 / 8 s. Both are flat across the
window, so the 3 s figures are not a beat artefact.

No case declares a pitch tolerance except `chord-low`. `ab_compare`'s
autocorrelation f0 is not reliable on a four-voice chord: `high-note`, whose
chord contains a near-octave, returns a clean −1200-cent octave error while
every band below 320 Hz carries under 0.1% of the energy. The near-unison row
is the one chord where the estimator has a single unambiguous peak, and there
it reads 0.0 cents.

Both copyright lines are carried in `LICENSE` and in each source file; the
declared deviations are listed in the header comment of
`plaits/dsp/engine2/wave_paraphonic_engine.h`, and `tests/ab.json` holds the
measured comparison against the module.
