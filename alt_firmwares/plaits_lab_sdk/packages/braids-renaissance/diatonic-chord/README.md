# Diatonic Chord

A port of the five CHORD_* models from Tom Burns'
[Braids Renaissance](https://github.com/boourns/eurorack-renaissance).

A chord built by stacking **scale degrees** on the played note rather than by
looking up an interval table. Four of the five display models differ only in
waveform, so they collapse onto MORPH the way `triple`'s four do; the fifth is
dropped (below).

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
eight: major, natural minor, dorian, mixolydian, harmonic minor, melodic minor,
major pentatonic, whole tone. The last two are the interesting ones — in a
pentatonic or whole-tone scale a "third" is not a third, and every chord shape
bends with it.

TIMBRE detunes the voices symmetrically against each other (so the chord's
centre of mass does not move — it beats rather than going out of tune) and folds
them where MORPH is still near a sine, which is exactly where Renaissance put
its `ws_sine_fold` drive.

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

## The wavetable model is dropped

Renaissance's fifth model scans Braids' `mini_wave_line` through `wt_waves`.
Plaits ships its own `wavetable` engine over the same ground, Palette can load
custom wavetables into it, and carrying Braids' wave data here would cost more
flash than the other four models put together.

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

Host CPU is close to `triple`'s, which is a smoke signal and not a budget --
see `bytebeat`'s README for why engine-to-engine host ratios do not carry to
the hardware. `scale_voices.cc` is shared with `scale-stack`, so the second of
the two costs materially less than the first.
