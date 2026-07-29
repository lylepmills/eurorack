# Vowel

A port of Braids' VOWL — three formant oscillators read out of 16-step tables,
gated by a ramp that restarts on every pitch period, summed and pushed through
a tanh shaper.

The DSP is Emilie Gillet's `DigitalOscillator::RenderVowel`
(`braids/digital_oscillator.cc:469`).

## What it is next to Speech, honestly

Plaits' `speech` engine already contains a descendant of this model. Its SAM
mode is the same instrument written in float, and the resemblance is not
approximate:

* nine vowel frames and eight consonant frames on both sides
  (`kSAMNumVowels = 9`, `kSAMNumConsonants = 8`);
* the amplitude indices are **identical row for row** across all seventeen
  frames, checked entry by entry against `vowels_data` and `consonant_data`;
* the formant frequencies are Braids' bytes scaled by a median of 2.241 — 2.10
  to 2.25 over the 51 entries, the low end being rounding on the small bytes;
* SAM's `formant_amplitude_lut` is `0.03125 * exp(0.184 * a)`, which is Braids'
  `gains = exp(0.184 * arange(16))` over 32;
* SAM's formant shift is `1 + timbre * 2.5` against Braids' 200..711, a 3.5×
  range against 3.555×;
* SAM's envelope is `s *= (1 - phase_)`, which is Braids' `255 - (phase >> 24)`.

Pure SAM is reachable in `speech` at HARMONICS = 1/6, where the model blend
lands on it exactly. So this is not a sound Plaits cannot make, and saying
otherwise would be the kind of claim this port exists to avoid.

What it is, is the version before the float rewrite, and four things survive
the difference:

1. the formant oscillators are read at **16 steps a cycle**
   (`phaselet = (phase >> 24) & 0xf0`) against the 512 SAM reads out of
   `lut_sine` (`SineRaw`, `kSineLUTBits = 9`). Neither interpolates — the
   difference is 32× in resolution, and the staircase is most of the timbre;
2. the amplitude is a **4-bit index baked into the waveform table** — the row
   for amplitude 1 takes the values `{0, ±2, ±3, ±4, ±5}` — so a quiet formant
   is quantised to a handful of levels, where SAM applies a float gain;
3. the **third formant is a square wave**, not a sine;
4. the sum goes through `ws_moderate_overdrive`, `scale(tanh(2x))`, which is
   2.07× on small signals and hard compression on loud ones. SAM has no shaper.

That is a texture difference rather than a new instrument, and the two macros
Braids does not have were chosen accordingly — both are axes neither SAM nor
the naive formant mode has a control for.

## The controls

Braids has two knobs. `parameter_[0]` (TIMBRE) is the phoneme:
`vowel_index = parameter_[0] >> 12` picks the frame pair and
`balance = parameter_[0] & 0x0fff` crossfades between them.
`parameter_[1]` (COLOR) is `formant_shift = 200 + (parameter_[1] >> 6)`, one
multiplier on all three formants at once.

The port puts the phoneme on **MORPH** and the shift on **TIMBRE** — swapped
relative to Braids, on purpose. `speech` passes MORPH as the vowel and TIMBRE
as the formant shift, and `vowel-fof` already swapped to match it. Three vocal
engines in one palette with the same two axes in the same two places is worth
more than keeping Braids' knob order.

The two new ones:

* **Spread** (HARMONICS) scales formants 2 and 3 against formant 1, ×0.5 to ×2.
  Braids scales all three together, so the spacing between them is fixed by
  whichever frame you are on. This reaches vowels that are not in the table.
* **Grain** (MACRO) is the length of the per-period ramp, ×0.2 to ×3. Braids
  welds it to exactly one period. Short, the burst ends early and the formant
  rings tighter and brighter; long, the ramp is still open when the period
  restarts, and the reset becomes an edge.

Both are stock at noon, where `ApplyMacro` returns exactly 1.0 and the engine
reduces to the module.

## Rate

`RenderVowel` is a plain `while (size--)` loop with no `size -= 2` and no
internal oversampling, so it is a 96 kHz algorithm. Rather than re-derive its
constants for 48 kHz, the port runs its inner loop 2× — at Braids' own 96 kHz —
and decimates. The formant increments, the 3840-sample consonant and the
per-sample draw on Braids' LCG then all transfer verbatim.

That is not bookkeeping. The staircase and the square third formant are not
band-limited: at COLOR = 1 the loudest audible third formant sits at 7160 Hz
(the vowel frames; the highest increment Braids can reach is 7876 Hz, but that
frame carries amplitude index 0 on formant 3, so it is silent). The 11th
harmonic of a 7160 Hz square is 78.8 kHz, which folds to 17.2 kHz at 96 kHz
sampling, and the 13th folds to 2.9 kHz. That folded content is part of what
the module sounds like.

Measured, by building the 48 kHz variant and running the same seven cases: the
spectral difference goes from 0.05–0.21 dB to 0.38–0.89 dB, and the error sits
in 5–24 kHz (+1.7 to +3.3 dB in the 10–20 kHz band, up to +8.2 dB above
20 kHz) — exactly where the misplaced fold products land.

The decimator is a 31-tap Blackman-Harris halfband, 17 non-zero taps folded to
9 multiplies: −0.36 dB at 18 kHz, −6.02 dB at 24 kHz, −17.9 dB at 28 kHz,
−41.7 dB at 32 kHz. The reference renderer uses a 127-tap sinc below −90 dB, so
the port keeps alias residue in the top octave that the reference does not.

## Tables

Three of Braids' tables are embedded, and all three were regenerated from the
python in `braids/resources/` and compared entry by entry rather than trusted:

| table | entries | deviation |
|---|---|---|
| `wav_formant_sine` | 256 | 0 LSB |
| `wav_formant_square` | 256 | 0 LSB, and exactly factorisable to 16 gains × a sign |
| `ws_moderate_overdrive` | 257 | 0 LSB |

The formant sine is held as `int8` rather than `int16` — its measured range is
−63..63, so no value changes and it costs half the flash. The square table is
carried as its 16 gains, because `amps = round(gains)` with the sign flipped
above half a cycle reproduces all 256 entries exactly.

The amplitude interpolation keeps Braids' `>> 12` **truncation**: the amplitude
is an integer index into a 16-step ladder baked into the waveform table, not a
gain, so the crossfade between two frames steps rather than glides. Smoothing
it would be a different instrument.

## Measured

`tests/ab.json`, seven cases, against `render_braids_model --shape VOWEL --rate
48000`: AC RMS within 0.04 dB, pitch within 0.2 cents, spectral difference
0.05–0.21 dB. Every band from 80 Hz to 20.5 kHz stays inside 0.5 dB. The
20.5–24 kHz band runs −0.3 to −2.6 dB, which is the decimator; the two bands
below 80 Hz can run wider (up to 18 dB on the vowel sweep) because the port has
a DC blocker at about 19 Hz and the module has none. Where they run wide they
are empty — the vowel sweep's −18.0 dB at 20–40 Hz is 0.2% of the energy and
its −5.7 dB at 40–80 Hz is 0.3% — and where they carry real energy they are
close, shift-low putting 3.0% of its energy in 40–80 Hz and differing there by
0.31 dB.

One number worth recording because it was measured rather than assumed:
reproducing Braids' LCG so the port picks the *same* consonant frames is a
nicety, not a necessity. Seeded `0x22` instead of `0x21` the port chooses a
different consonant on every strike, and the 4 Hz triggered case moves from
0.21 dB to 0.27 dB.

Both copyright lines are carried in `LICENSE` and in each source file; declared
deviations are listed in the header comment of
`plaits/dsp/engine2/vowel_engine.h`.
