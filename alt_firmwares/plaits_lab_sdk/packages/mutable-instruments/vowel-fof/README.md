# Vowel FOF

A port of Braids' VFOF: five very narrow state-variable filters excited by a
band-limited saw, sweeping five vowels across five vocal registers.

## Which knob is which

**TIMBRE is the VOWEL and MORPH is the REGISTER.** That is what Braids does:
`RenderVowelFof` calls `InterpolateFormantParameter(table, parameter_[1],
parameter_[0], i)`, so COLOR is the outer index, and the outer dimension of
`formant_f_data` is the register (its own `// bass` … `// soprano` block
comments; /a/ F1 rises 601 → 795 Hz across it) while the middle one is the
vowel. The tables here are vendored in that same `[register][vowel][formant]`
order and the interpolator indexes them the same way, so the port agrees with
the module exactly.

It shipped with the two labels the other way round — "Register" on TIMBRE,
"Vowel" on MORPH — because the engine header, the data header and the catalog
all described a transpose that was intended but never applied. **The labels were
corrected and the DSP was left alone.** The DSP is the faithful side, and moving
it would silently re-voice every patch already saved against this engine.
Nothing about how the engine responds to a knob has changed; only what the knob
is called.

The consequence is that `vowel-fof` and `speech` run their vocal axes opposite
ways round. That is a genuine wart, and it is Braids' own.

## The finding that justifies the engine

Confirmed in the source: `out += svf_bp[i] * amplitudes[0] >> 17` reads
`amplitudes[0]`, not `amplitudes[i]`, and column 0 of the amplitude table is
16384 in all 25 rows — so 100 of the 125 amplitude entries have been dead since
2013 and every formant is voiced flat. MACRO restores them, with the detent
reproducing hardware exactly. Those amplitudes are **linear** with 16384 as
unity, not semitone attenuations, so the tilt is a linear interpolation between
flat and true.

## What the port adds

**HARMONICS** crossfades the excitation from the bare saw toward noise so the
bank can whisper. Its Braids-equivalent position is **0.0**, not noon — it does
not go through `ApplyMacro`, so a centred HARMONICS is already half breath.

**TRIG** has no counterpart in the model. `RenderVowelFof` never reads
`strike_`, so the module ignores a strike on this shape, and there is no
envelope here either (`already_enveloped` is false). A rising edge re-inits the
bank — the five SVF states zeroed, the noise reseeded — which is a
re-articulation, not a re-triggered envelope. It costs very little against the
module: at 8 Hz the `triggered` A/B case reads +0.01 dB AC RMS and 0.42 dB
spectral difference, against +0.06 / 0.15 untriggered, because a Q = 64 bank
refills in about 40 ms.

## Rate

`RenderVowelFof` ends in `size -= 2`, so it is a 48 kHz algorithm: the saw and
the five filters all advance once per two output samples. Two of its three
half-rate adjustments are exact identities at 48 kHz and correctly drop out
here — the doubled increment, and the `+ (12 << 7)` octave offset that lets a
`lut_svf_cutoff` generated at 96 kHz address a bank running at 48.

The third, the output averager, is **not** a compensation and does not cancel.
`*buffer++ = (out + previous_sample) >> 1; *buffer++ = out;` is a 2× linear
interpolator reconstructing 96 kHz from the 48 kHz algorithm, and it is a real
lowpass: referred to the algorithm rate its response is
`(1 + cos(2π f / 96000)) / 2`, which is −0.95 dB at 10 kHz and −6 dB at 24 kHz.
The A/B reference is decimated to 48 kHz with a flat halfband, so that rolloff
survives into the comparison while the port has nothing in its place.

It is deliberately not reproduced. It belongs to the module's output stage
rather than to the algorithm, and at 48 kHz it is a half-sample fractional
delay, not a two-tap average (a two-tap average at 48 kHz is −2.0 dB at 10 kHz,
twice too much), so a faithful copy costs a fractional-delay filter on the
tightest engine in the port. On the corrected linear A/B, `stock-mid` reads
+1.72 dB at 10–20.5 kHz and +5.66 dB at 20.5–24 kHz; those bands carry 2.3%
of the energy and the energy-weighted overall spectral figure is 0.15 dB. The
averager contributes to that excess, but the old attempt to assign it an exact
share used a clipped preview WAV, so that split is no longer quoted.

## Measured against the module

`tests/ab.json` runs sixteen cases against `VOWEL_FOF`: both ends of both
Braids axes, two interior grid nodes, an off-node point, a sweep along each
axis, six octaves of pitch (notes 21 to 93), a triggered case, and the two
points that looked worst before the measurement harness was corrected. Every
case is at the stock exciter — HARMONICS 0.0, MACRO at the detent — because
neither of those axes exists in Braids.

| metric | corrected linear A/B |
| --- | --- |
| AC RMS | +0.01 to +0.11 dB |
| spectral difference | 0.10 to 0.42 dB |
| pitch | within 0.2 cents |

The previous revision declared a −1.67 dB worst case from a 21 097-point scan.
That number was not an engine residual. The A/B harness rendered the port with
the catalog's 3.8× gain into a 16-bit WAV, clipped peaks above full scale, and
then divided the samples by 3.8 as though the operation were reversible. It
wasn't. `ab_engine.py` now renders every engine with fixed eightfold headroom
and undoes that linear gain after reading the WAV. The former “worst” point at
note 61 / TIMBRE 0.750 / MORPH 0.3125 now reads **+0.05 dB AC RMS / 0.20 dB
spectrum**; the former /u/ basin point reads **+0.03 / 0.09 dB**. Both remain
in the suite as stress coverage, but they are no longer described as a basin.

All sixteen cases now use the same tight limits: 0.3 dB AC RMS, 0.5 cents and
0.6 dB spectrum. The full-grid scan has not been re-run through the corrected
harness, so this document makes no unmeasured claim about points outside the
committed suite.

**Two deviations were removed to get there,** and both are the campaign's
recurring failure — the port using the clean value where the module uses the
quantised, clipped one.

*The excitation had its DC removed.* Braids drives the bank with a **unipolar**
0…1 saw (`next_saw_sample += phase >> 17` yields 0…32767, mean 0.5). The port
used Plaits' bipolar saw scaled to the same peak-to-peak, which discards the DC.
That is not a level choice: the Chamberlin lowpass state settles at its input's
DC, so on hardware it sits at half scale and hard-clips against ±1 on 5–21% of
samples, where the port's, centred on zero, clipped on 1–8% — and the bank rang
less. Plaits' `Oscillator` builds the same 0…1 saw internally, with the same
`0.5 t²` BLEP either side, and emits `2 · this_sample − 1`, so
`0.5f * (x + 1.0f)` recovers Braids' sample exactly. That is the fix.

*There was an extra saturator on the bandpass tap.* The engine returned
`SoftClip(svf_bp[k])` after `CONSTRAIN` had already bounded the state to ±1.
Braids' `out += svf_bp[i] * amplitudes[0] >> 17` reads the clipped state
straight into the sum with no saturator, and `stmlib::SoftClip` attenuates well
inside full scale (−0.6 dB at 0.5, −2.2 dB at 1.0). The `SoftClip` is gone; the
two `CONSTRAIN`s stay, because those *are* Braids' two `CLIP`s. The comment that
used to sit on that line claimed the port "only bounds the tap" where Braids
clips the state, which was the opposite of what the code did.

Between them these accounted for the real waveform mismatch. The old clipped
harness exaggerated the remaining level residual, but it did not create either
source-level defect: both are visible directly in the Braids arithmetic and
both fixes tighten the corrected linear A/B. The suite now holds every case to
0.3 dB AC RMS and 0.6 dB spectrum.

## AUX

**Mono AUX** is the glottal source ahead of the bank — the saw crossfaded toward
noise by HARMONICS, at one neutral makeup instead of the five per-formant ones.
It is the raw-exciter idiom (`inharmonic-string`, `modal-resonator` and
`particle-noise` all put theirs on AUX) and the natural sibling of `speech`,
whose AUX is its secondary path. It is taken from the DC-free form of the saw,
not the unipolar one the bank sees, so restoring that DC did not put an offset
on an output.

Its gain has been re-fitted. The old value was justified in the engine header
with "measured over the parameter grid this lands the mono AUX within a fraction
of a dB of OUT" — which was **false as shipped**: the quieter bank left AUX
disproportionately hot. With the bank
correct and `kVowelFofSourceGain` re-fitted to 0.387, AUX sits within **±0.68 dB**
of OUT at the eight fixed grid positions the fit used — both grid corners on
both axes, the interior, an off-node point, and notes 33 and 81 — so `aux_gain`
staying equal to `out_gain` is a defensible choice *at that operating point*
rather than a guess.

**Those are eight points, and the number does not survive the grid they sit in.**
Measured at the same stock exciter and the same MACRO detent over notes 21 to 93
against the full vowel × register grid — 2025 points — AUX against OUT runs

| span (stock exciter, MACRO detent) | AUX − OUT |
| --- | --- |
| the eight fitted positions | −0.68 … +0.68 dB |
| note 45, whole vowel × register grid | −0.73 … +0.06 dB |
| notes 33–81, whole grid | −4.15 … +1.59 dB |
| **notes 21–93, whole grid** | **−4.95 … +3.50 dB** |

worst at note 90 / TIMBRE 0.25 / MORPH 0.50 and hottest at note 21 / TIMBRE 0.50
/ MORPH 1.00. Restricting to the fit's own four octaves does not rescue it. What
±0.7 dB really describes is **mid register**: the note-45 row is the only one
that stays inside it.

**The gain was not re-fitted again to close that, because no constant can.** The
spread is 8.5 dB wide and driven by pitch — AUX is the exciter, so its level
barely moves with the note while OUT follows how many harmonics land inside the
bank — so a constant only slides it: the best-centred value still leaves ±4.2 dB
and spends the mid-register match, the one place the two really are a pair, for
nothing. 0.387 stays and the range is declared. `tests/ab.json` renders both
ends of the pitch spread: **+3.1 dB at note 21**, **−3.9 dB at note 93**.

Two further axes widen it, both measured over the same note × grid. **HARMONICS**
darkens AUX faster than OUT: at full breath AUX runs −6.45 … −3.60 dB against
OUT, always under it. **MACRO** at full tilt costs OUT several dB — attenuating
formants 2–5 is the point of the control — while leaving AUX untouched, so at
MACRO 1.0 AUX is hotter *everywhere*: +1.18 dB at its closest, **+20.7 dB** at
note 93 / TIMBRE 1.00 / MORPH 0.00. Over the whole HARMONICS × MACRO × note ×
grid plane the two span **−13.2 dB to +20.8 dB**.

**Stereo AUX** re-sums the same five taps under the table's weighting reversed,
so it leans on the upper formants. As shipped this was **bit-identical to OUT at
the MACRO detent** — not merely close: the weights were `1 + (raw − 1) · tilt`,
`ApplyMacro(0.0f, −0.5f, 1.0f, 0.5f)` returns exactly `0.0f`, so every weight
was exactly `1.0f`, and reversing five identical unit weights is the identity.
`voice.cc` defaults MACRO to 0.5, so the stereo mode collapsed to mono for any
user who had not moved that knob.

The reversal is now applied at full strength and **does not track MACRO**, with
a fixed makeup (`kVowelFofStereoMakeup`) because reversing the table puts its F5
entry on the F1 tap and F1 carries most of the bank's energy. Measured at the
detent over the same eight fitted positions: L/R within **±1.66 dB** at a
channel correlation of **0.45–0.83**, where it was correlation 1.000000 before.
Half-strength reversal was measured as the alternative — correlation 0.94–0.98
at ±0.8 dB — and rejected: a stereo mode whose channels are 96% the same signal
is not worth the aux output.

**±1.7 dB at 0.45–0.83 is those eight positions, not the grid, and the
difference runs both ways.** R is the bank under a *fixed* weighting while L
follows MACRO and the note, so:

| span | L/R balance | channel correlation |
| --- | --- | --- |
| the eight fitted positions, MACRO detent | −1.64 … +1.66 dB | 0.45–0.83 |
| notes 21–93 × whole grid, MACRO detent | **−4.90 … +4.01 dB** | **0.32–0.996** |
| notes 21–93 × whole grid, MACRO 1.0 | **−0.83 … +24.0 dB** | −0.40 … +0.93 |
| the whole HARMONICS × MACRO × note × grid plane | **−8.1 … +25.2 dB** | −0.40 … 0.996 |

At the detent the extremes are note 21 / TIMBRE 1.00 / MORPH 1.00 (−4.90 dB) and
note 90 / TIMBRE 0.00 / MORPH 1.00 (+4.01 dB) — a wider image than ±1.7 dB
suggests, but still a pair. The **top** of the correlation range is the part
neither document used to mention: near note 93 at TIMBRE 0.75 / MORPH 0.375 the
two weightings pick out very nearly the same signal (0.996) and the width
quietly goes away. It is not the bit-identical mono of the pre-fix version, but
it is mono to the ear, and it is reachable at the detent.

**Off the detent it stops being a pair at all.** At MACRO 1.0 full tilt leaves
OUT almost nothing while R is untouched, so R runs hotter than L over 98% of the
grid — median **+5.4 dB**, 28% of it past +10 dB, and only 41 of 2025 points on
the other side of level at all (worst −0.83 dB). It reaches **+24.0 dB** at note
93 / TIMBRE 1.00 / MORPH 0.50, or **+25.2 dB** with HARMONICS in play (note 86,
HARMONICS 0.25, TIMBRE 0.56, MORPH 0.50). Correlation goes **negative** up there
too, −0.40 at note 93 / TIMBRE 0.125 / MORPH 0.75 where the imbalance is
+13.8 dB, so the channels are partly out of phase as well as lopsided. A stereo
AUX at full MACRO is a hard-panned image by construction. Turning MACRO up is a
formant-balance control on OUT and a **panner** on the stereo pair, and that is
worth knowing before it is discovered by surprise.

Note that `render_model.cc` leaves `EngineParameters::stereo` at its `false`
default, so **no A/B case reaches the stereo branch**. Those figures come from a
separate harness driving the engine directly; `tests/ab.json` records that gap.

## Loudness

`outGain` is unchanged at −3.8. The bank is materially hotter than the old
bipolar-exciter/saturated-tap version, but the old 2.4–4.1 dB figure came from
the superseded clipped A/B harness and is deliberately not repeated here.
The gain is not a bug compensation: the engine is intrinsically quiet (five
very narrow bandpasses at an eighth gain each). Re-fitting it is a palette-wide
loudness question, not a fidelity one.

## Tables

The tables are vendored rather than shared with `NaiveSpeechSynth`, which holds
the same grid. Its uint8 quantisation carries about half a semitone of centre
error against filters whose bandwidth is 0.24 semitones — half a semitone of
error on a quarter-semitone filter is a different formant, not a rounding
difference.

Both copyright lines are carried in `LICENSE` and in each source file.
