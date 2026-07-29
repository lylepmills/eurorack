# Vowel FOF

A port of Braids' VFOF: five very narrow state-variable filters excited by a
band-limited saw, sweeping five vowels across five vocal registers.

`RenderVowelFof` ends in `size -= 2`, so it is a 48 kHz algorithm: the saw and
the five filters all advance once per two output samples. Two of its three
half-rate adjustments are exact identities at 48 kHz and correctly drop out
here — the doubled increment, and the `+ (12 << 7)` octave offset that lets a
`lut_svf_cutoff` generated at 96 kHz address a bank running at 48.

The third, the output averager, is **not** a compensation and does not cancel.
`*buffer++ = (out + previous_sample) >> 1; *buffer++ = out;` is a 2× linear
interpolator reconstructing 96 kHz from the 48 kHz algorithm, and it is a real
lowpass: referred to the algorithm rate its response is
`(1 + cos(2π f / 96000)) / 2`, which is −1 dB at 10 kHz and −6 dB at 24 kHz.
The A/B reference is decimated to 48 kHz with a flat halfband, so that rolloff
survives into the comparison while the port has nothing in its place. Measured
on `stock-mid`: applying the same interpolator to the port's render moves the
20.5–24 kHz band from +7.6 dB to +2.2 dB and the 10–20 kHz band from +9.7 dB
to +7.9 dB. Those bands carry under 2.5% of the energy and the residue is BLEP
difference, so nothing here is worth a rollout — but the port is brighter than
the module above 10 kHz, and this is why.

**The finding that justifies the engine is real**, and confirmed in the
source: `out += svf_bp[i] * amplitudes[0] >> 17` reads `amplitudes[0]`, not
`amplitudes[i]`, and column 0 of the amplitude table is 16384 in all 25 rows —
so 100 of the 125 amplitude entries have been dead since 2013 and every
formant is voiced flat. MACRO restores them, with the detent reproducing
hardware exactly.

Those amplitudes are **linear** with 16384 as unity, not semitone
attenuations, so the tilt is a linear interpolation between flat and true.

**TIMBRE and MORPH are NOT swapped relative to Braids, whatever the control
labels say.** The swap was intended — it would put register and vowel on the
same knobs as `speech` — but it was never implemented: the tables were
vendored from Braids in Braids' own `[register][vowel][formant]` order, so
passing MORPH as the outer index selects the register and TIMBRE as the middle
one selects the vowel, exactly as `parameter_[1]`/`parameter_[0]` do in
`RenderVowelFof`. So the engine agrees with the module and disagrees with
`speech`, which is the opposite of the intent, and the shipped control labels
("Register" on TIMBRE, "Vowel" on MORPH) name the wrong axes. Measured, not
assumed: `tests/ab.json` reads 3.0 dB and 2.3 dB of spectral difference at the
two far corners with TIMBRE taken as the vowel, and 5.4 dB and 8.5 dB with the
mapping swapped. Resolving this is a pending decision, since either fix moves a
digest.

HARMONICS is new: Braids' excitation is a bare saw, and this crossfades toward
noise so the bank can whisper. Its Braids-equivalent position is **0.0**, not
noon — it does not go through `ApplyMacro`, so a centred HARMONICS is already
half breath.

The tables are vendored rather than shared with `NaiveSpeechSynth`, which
holds the same grid. Its uint8 quantisation carries about half a semitone of
centre error against filters whose bandwidth is 0.24 semitones — half a
semitone of error on a quarter-semitone filter is a different formant, not a
rounding difference.

## Measured against the module

`tests/ab.json` runs ten cases against `VOWEL_FOF`, covering both ends of both
Braids axes, the interior of the grid, a sweep along each axis, and four
octaves of pitch. Pitch tracks the reference within 0.2 cents everywhere, so
the rate analysis above holds. Spectral difference is 1.4–3.0 dB.

Level does **not** meet the port's usual target: AC RMS runs 2.4–4.1 dB below
the module wherever the excitation is dense, and −0.4 dB at note 81 where it is
sparse. That level-dependence is a nonlinearity difference, and simulating both
inner loops side by side attributes it to two deviations. Braids excites the
bank with a **unipolar** 0…1 saw, so its Chamberlin lowpass state sits at half
scale and hard-clips 5–21% of samples; the port centres the saw on zero, its
state clips 1–8% of samples, and the bank rings less. Restoring the unipolar
saw in that simulation recovers 2.0–2.9 dB, and dropping the port's `SoftClip`
on the bandpass tap a further 0.6–1.5 dB. That `SoftClip` is an extra
saturator, not the port's copy of `CLIP`: the states are already constrained to
±1 inside the loop, which is what Braids clips. Neither is corrected here; both
are recorded so the next revision has the measurement to work from.

## AUX, and two claims that do not hold

Both of these are in `vowel_fof_engine.h`, which is hashed by `package_digest`
along with the two `.cc` files, so correcting them there is not a comment
change — it invalidates the shipped package. They are recorded here instead.

**The mono AUX is not level-matched to OUT.** The header justifies
`auxGain == outGain` with "measured over the parameter grid this lands the mono
AUX within a fraction of a dB of OUT". Measured across the ten A/B cases — both
grid corners on both axes, the interior, a sweep along each axis and four
octaves of pitch, all at the stock exciter — AUX runs **+0.2 to +3.4 dB above
OUT**, at or above +1.0 dB on nine of the ten and +2.8 dB at the mid grid
point. Over the HARMONICS × MACRO plane it spreads from **+7.7 dB to −4.9 dB**:
HARMONICS darkens AUX faster than OUT, and MACRO at full tilt costs OUT up to
8 dB (restoring the table's true balance attenuates formants 2–5, which is the
point of the control) while leaving AUX untouched. `kVowelFofSourceGain = 0.355f` appears to have been fitted at one
point rather than over the grid.

**In stereo, L and R are identical at the MACRO detent.** The header says the
stereo AUX re-sums the same five taps "with the formant weighting REVERSED, so
it leans on the upper formants". But the weights are
`amplitude[i] = 1.0f + (raw - 1.0f) * tilt`, and `ApplyMacro(0.0f, -0.5f, 1.0f,
0.5f)` returns exactly `0.0f`, so at the detent every `amplitude[i]` is exactly
`1.0f`; reversing five identical unit weights is the identity, and `sum` and
`sum_aux` accumulate the same five terms in the same order — bit-identical
output. `voice.cc:195-197` defaults `p.macro` to 0.5 unless the frequency pot
is locked to option 1, so the stereo mode collapses to mono for any user who
has not moved MACRO. The reversal only does anything off the detent.

Separately, the catalog names AUX "Upper-formant weighting". That describes the
stereo branch only; in the default mono aux mode (`aux_is_stereo()` is
`aux_output_option == 1`) AUX is the glottal source ahead of the bank — the saw
crossfaded toward noise by HARMONICS.

Both copyright lines are carried in `LICENSE` and in each source file.
