# Wave Scan

A port of Braids' three wavetable models — WTBL (`WAVETABLES`), WMAP
(`WAVE_MAP`) and WLIN (`WAVE_LINE`) — sharing one slot on a model axis, the way
`z-filter` merges Braids' four digital filters and `noise-bank` merges its three
noise models.

The DSP is Emilie Gillet's `DigitalOscillator::RenderWavetables`,
`RenderWaveMap` and `RenderWaveLine`. All three read the same 256-wave, 8-bit
bank; what differs is how the two knobs address it.

## Which bank, and why it costs 33 KB

Plaits already ships a wavetable engine, and it draws on the same source data —
`plaits/resources/waves.bin` is byte-identical to `braids/data/waves.bin`. That
makes substitution look free, and it is not.

Plaits' compiled table holds 192 waves, of which 63 distinct ones come from the
Braids bank. Only 16 of those 63 are the Braids waveform. The rest reach it
through `make_braids_family(indices)`, which keeps the magnitude spectrum and
forces every harmonic to a phase of −π/2 before resynthesising. Measured wave
for wave, mean removed and both normalised to unit peak, the difference signal
is **louder than the wave itself**: median +4.5 dB across the 48 phase-fixed
entries, worst +7.0 dB, best −46.1 dB. The 16 in the
`make_braids_family(…, False)` families do round-trip, better than −271 dB.

So the neighbour's bank covers 63 of the 256 waves these models need, and 47 of
those only as a different waveform. `WAVE_LINE` alone names 45 waves that are
not in it at any phase. There is no substitution to make, and Plaits'
`wav_integrated_waves` is 50,688 B — larger than the bank it would be replacing.

This engine therefore vendors Braids' own data: 32,768 B of waves, 256 B of
`wt_map`, 64 B of `wave_line` and 360 B of bank definitions, **33,448 B**. One
byte per wave was saved honestly: Braids stores 129 samples and reads index 128
as a wrap guard, and that byte equals the wave's own sample 0 in all 256 waves,
so the port stores 128 and wraps the index instead. All 256 waves are reachable
— the 20 bank definitions between them name every one — so there is no unused
subset to drop, and that is the whole saving available.

## What it has that Plaits' wavetable engine does not

**Twenty named banks that are not uniform grids.** `wavetable_definitions` gives
each bank its own length and its own repeats: `cello` holds one wave for its top
nine steps, `piano` and `drone 2` are 8 steps long, `bell` is 4, and both
`organ` and `digital` open on wave 176. Scanning a bank is therefore not a
uniform morph, and which bank you are in is a knob rather than a build-time
layout.

**A 16×16 interpolated map.** `wt_map` is a genuinely 2-D gesture over all 256
waves, bilinear in both axes and addressed by the two knobs alone. The plane
the neighbour's two knobs reach is 8×8 — a quarter of the cells — over the
phase-fixed rewrites measured above. Its third axis is not a finer map but a
fourth dimension on HARMONICS, running across four banks and back, and it is
interpolated rather than switched (`mix = xyz0 + (xyz1 - xyz0) * z_fractional`),
hardening toward stepping only over the top half of the travel.

**A phase-domain crush.** WLIN's COLOR walks four zones and ends by mixing a
64-step sample-and-hold of the wave against a 16-step one — the table read at
half and at an eighth of its resolution with the interpolation switched off.
Plaits' wavetable AUX is a 5-bit *amplitude* bitcrush of the output, which is a
different artefact entirely.

## The controls

Braids gives each model two knobs and they do different jobs in each:

| | TIMBRE | COLOR |
|---|---|---|
| WTBL | position within the bank, at that bank's own step count | which of the 20 banks, behind a ±64-count hysteresis |
| WMAP | the map's X axis, driving the outer mix | the map's Y axis, driving the inner crossfade |
| WLIN | position along a curated 64-wave line, smoothed and de-zippered | four crush zones, made by an integer overflow |

HARMONICS selects the model. TIMBRE and MORPH carry Braids' two axes.

MACRO is the only added one: **Snap**, the curve of the scan crossfade. At the
detent it is the identity and the engine is the module exactly, which is what
keeps all 24 A/B cases comparable. Turned up, the crossfade is pulled to
whichever end it is nearer, so the scan steps from whole wave to whole wave the
way a PPG does rather than morphing. Turned down it is pulled to 0.5, so
adjacent waves sum instead of trading and the scan blurs — in WMAP that means
all four corners of the cell at once. In WLIN, Braids' own bottom quarter of
COLOR already locks the scan to a single wave, so Snap has nothing to do down
there and progressively more above it.

Nothing in Braids reaches this axis, and neither does Plaits' wavetable engine:
both interpolate the scan, and only interpolate it.

AUX is always the complementary blend of whichever pair OUT is crossfading
between — the adjacent wave in WTBL, the opposite corner of the cell in WMAP,
the other end of the crush in WLIN — so it costs no extra table reads. In stereo
OUT and AUX become L and R and take a small opposing offset of that same blend.

## Rate

All three functions loop `while (size--)` with no `size -= 2`, so all three are
96 kHz algorithms — and all three are internally 2× oversampled on top of that,
by the same idiom: the phase increment is halved, added twice per output sample
with a wave read between the two, and the pair box-averaged. Braids reads the
table at 192 kHz.

The port runs 4× from 48 kHz to reach the same 192 kHz, box-averages the pairs
exactly as Braids does to get a 96 kHz stream, and takes that down to 48 kHz
with a 19-tap Hamming halfband — which is Braids' own chain with the A/B
harness's final decimation folded into the engine. Because the internal rate
matches, every rate constant transfers verbatim.

Two constants are expressed against the *block* rather than the sample rate:
WLIN's scan smoother runs once per render block, and its de-zipper crossfade
ramps by `32768 / size` per internal sub-sample and restarts each block. Those
also transfer unchanged, but only because Braids' 24-sample block at 96 kHz and
Plaits' 12-sample block at 48 kHz are both 4000 blocks per second. On any other
block rate the scan would lag differently.

## Measured

`tests/ab.json` holds 24 cases against `braids/test/render_braids_model`, every
model at both ends of both Braids axes plus three sweeps for the block-rate
machinery. **AC RMS agrees within 0.02 dB on every case.** The spectral
difference is within 0.12 dB on 23 of the 24 and 0.24 dB on the last. Pitch
reads within 0.6 cents on every case that carries a cents tolerance.

Every band difference above 1 dB that carries as much as 1% of the energy is in
the 20.5–24 kHz octave, where the halfband rolls off: −1.1 to −2.1 dB there on
the six WLIN cases, which are the ones with real energy that high because the
crush is a sample-and-hold. Nothing else exceeds 0.7 dB in any band holding 1%
of the energy.

Two cases carry no cents tolerance, and in both the estimator is at fault rather
than the engine. `wlin-high-note` is too harmonically rich for `ab_compare`'s
autocorrelation, which takes the peak at lag 2T on one side and lag T on the
other and so reports an exact octave; re-run with its lag range floored above
the sub-octave it gives +0.86 cents, and the 261.6 Hz component is more than
74 dB below the fundamental on both sides. `wmap-origin` has the opposite
problem — `wt_map[0]` is wave 176, whose even harmonics are exactly zero and
whose third is 59.2 dB down, so it is a sine, and locating a 435-sample-wide
autocorrelation peak with a three-point parabola is ill-conditioned. Measured
instead on the peak of a 65536-point spectrum it reads −0.25 cents. Neither
tolerance was widened; both were dropped and the reason written down.

The vendored data was verified rather than asserted: the 33,024-byte `wt_waves`
array in `braids/resources.cc` is byte-identical to `braids/data/waves.bin`,
which is what `waveforms.py` reads, at 0 LSB across all 33,024 bytes, and
`wt_map` likewise at 0 LSB across 256. The halfband's six coefficients were
re-derived from their windowed-sinc definition and agree with the printed
constants to 2.4e−09.

The engine carries no DC blocker and does not need one — the worst mean offset
over all 256 waves is 0.0042 of full scale and the median is 0.0001 — but it
does register negative gains, because the halfband has negative taps and an L1
norm of 1.360, so the output cannot be pinned at the readout's ±1 bound
analytically (SPEC R1). Measured peak across the seven scenarios is 1.0000 and
worst DC 0.0038.

Both copyright lines are carried in `LICENSE` and in each source file; the
declared deviations are listed in the header comment of
`plaits/dsp/engine2/wave_scan_engine.h`.
