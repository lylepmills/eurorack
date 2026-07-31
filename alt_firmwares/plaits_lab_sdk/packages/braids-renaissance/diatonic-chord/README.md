# Diatonic Chord

A port of the five CHORD_* models from Tom Burns'
[Braids Renaissance](https://github.com/boourns/eurorack-renaissance).

A chord built by stacking **scale degrees** on the played note rather than by
looking up an interval table. The five display models differ only in waveform,
so sine, triangle, saw, square and WTCH collapse onto one continuous MORPH axis.

## Why it is not a second `chords`

Plaits' own chord engine reads a table of intervals in cents, and Plaits Palette
lets you author that table — so a fixed chord shape transposed under the note is
already reachable at zero marginal flash. What is not reachable is a chord whose
**quality changes with the note**. Here the third is "two scale degrees up", so
in C major, playing C gives a major seventh and playing A gives a minor seventh
— one knob position, no table entry for either, and a diatonic progression falls
out of simply playing a melody into it.

That is the entire argument for the slot. If you want a fixed voicing, use
`chords`; it is better at it and costs nothing extra.

## Controls

HARMONICS steps sixteen chord shapes (triads, sixths, sevenths, ninths,
elevenths, thirteenths, and the suspended block). MACRO picks the scale from
sixteen: major, natural minor, dorian, mixolydian, harmonic minor, melodic
minor, major pentatonic, whole tone, blues minor, Japanese, Gamelan, Gypsy,
Arabian, Bhairav, Bairagi, and Yaman. The final three retain Braids' original
1/128-semitone tuning instead of being rounded to 12-TET.

TIMBRE detunes the voices symmetrically against each other (so the chord's
centre of mass does not move — it beats rather than going out of tune), folds
them where MORPH is still near a sine, and scans the wavetable at the WTCH end.
That last role is Renaissance's original table-selection axis.

## ⚠️ Upstream arithmetic is not reproduced

Renaissance's `diatonic_chords` table reads as **absolute** scale degrees — row
4 is `{2, 6, 8}` and is labelled "9th", i.e. degrees 6 and 8, a seventh and a
ninth. Correct. But `renderChord` consumes the array **cumulatively**
(`index = index + noteOffset[i-1]`), turning that row into degrees 2, 8 and 16
and putting the top voice more than two octaves up.

Two independent things say absolute is what was meant:

1. Every label in the table only parses as absolute degrees.
2. `RenderStack` **pre-accumulates** its own offsets into absolute values before
   handing them to the same function — which the cumulative read then squares
   into `span, 3*span, 6*span, 10*span`.

This port treats the offsets as absolute. Two further upstream defects fall out
with it: `len = diatonic_chords[ext][0] + 3` reads up to index 6 of a 4-wide
row, and writes up to `offsets[7]` in a 6-element array.

Anyone who wants the shipped-Renaissance voicings rather than the intended ones
is asking for a different engine, and it should be a different engine.

## The wavetable model without a second wave bank

WTCH scans Braids' 33-entry `mini_wave_line` through `wt_waves`. The port maps
that line onto Plaits' already-shipped `wav_integrated_waves`: sixteen slots are
the same source waveform and seventeen use the measured nearest counterpart.
The source `waves.bin` is byte-identical in Braids Renaissance and Plaits, and
the listening A/B could not reliably distinguish this mapping from the true
Braids table.

The scan restores each Braids slot's relative RMS before crossfading and uses
the same linear sample interpolation as the module. A 2.5x endpoint makeup
brings WTCH in line with square while preserving the line's internal dynamics.
The large 50,688-byte Plaits table has zero marginal cost whenever Wavetable,
Chords, Wave Terrain or Wave Paraphonic already retains it; otherwise it is the
honest cold cost of choosing either scale engine.

Measured with the ARM 4.8.3 linker at `11bb41a`, the complete cold/shared
matrix is:

| Installed scale engines | Without an existing table user | With Wavetable already installed |
| --- | ---: | ---: |
| Diatonic Chord | 59,568 bytes | 6,304 bytes |
| Scale Stack | 59,408 bytes | 6,144 bytes |
| Both | 59,952 bytes | 6,672 bytes |

These are marginal costs against the same 24-slot recipe, not the size of the
engine object in isolation. The two scale engines share both the table and most
of `scale_voices.cc`, which is why adding the second costs only 384 bytes cold
or 368 bytes once Wavetable has retained the table.

## Implementation notes

The oscillators are **naive**, as Braids' were — that raw stacked sound is the
engine's identity, and six band-limited voices would not fit the CPU budget. What
the original could lean on and this cannot is its 96 kHz sample rate, so saw and
square get a PolyBLEP: a few operations per wrap, far cheaper than a full BLEP
oscillator, and enough to keep the top of the waveform axis usable an octave
further up than Braids managed.

Fine pitch survives quantisation. Braids kept `fm = pitch_ - codebook_[index]`
and added it back to every voice so bends and FM moved the whole chord instead
of being swallowed; the residual here does the same job.

**The fold's `+1.0f` phase offset is load-bearing.** `Sine()` is documented safe
"for phase >= 0.0f" and the folded signal is bipolar, so without a whole-period
offset the negative half of every waveform indexes `lut_sine` out of bounds —
a ~5.0 spike on OUT. The in-tree audition gate caught exactly that during this
port; it is the same defect the earlier review found in `z-filter`.

The calibrated Cortex-M4 sweep estimates Diatonic Chord at 96% of the audio
budget worst-case; the unsampled saw-to-square midpoint was checked separately
at 99%. Scale Stack estimates at 95%. Those are close enough that the hardware
DWT probe remains mandatory before publication.

The first literal prototype measured 141% at the square-to-table transition.
The shipping path gives the zero-mean classical and table regions their own
loops, short-circuits exact waveform anchors, and uses the matching naive square
only after leaving the PolyBLEP square anchor. Against the approved listening
prototype that transition measures 0.34 dB of spectral difference and 0.01 dB
of level difference; the WTCH scan itself is measurement-identical. Host timing
is still only a smoke signal -- see `bytebeat`'s README for why it does not
predict the module. `scale_voices.cc` is shared with `scale-stack`, so the
second engine reuses all of this code.
