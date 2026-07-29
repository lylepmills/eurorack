# Noise Bank

A port of Braids' three noise models — NOIS (filtered noise), TWNQ (twin peaks)
and CLKN (clocked noise) — sharing one slot on a model axis, the way `z-filter`
merges Braids' four digital filters.

The DSP is Emilie Gillet's `DigitalOscillator::RenderFilteredNoise`,
`RenderTwinPeaksNoise` and `RenderClockedNoise`.

## What it has that Plaits' filtered-noise does not, and where it does not

Plaits already ships a noise engine that runs two clocked noise sources through
a state-variable filter, so on paper it covers all three of these. Three things
it does not cover, and one it does:

**Clocked noise loops.** Braids' CLKN reseeds its random generator on a period
TIMBRE sets, so the sequence *repeats* and the model stops being noise: it
becomes a pitched buzz whose timbre is a frozen slice of randomness. Plaits'
`ClockedNoise` is free-running and nothing in its path loops. Braids also
quantizes each held sample to between 2 and 32 levels off COLOR, where Plaits'
is full resolution.

**Twin peaks is not two band-pass filters.** It is two raw 2-pole integer
recursions reading `lut_resonator_coefficient`, and that table stores
`2*cos(w)` in Q15 — which runs out of angular resolution as the note falls.
Read back out of the committed table, the resonance sits 25.9 cents sharp at
MIDI 45, 192 cents sharp at MIDI 36 and 1641 cents sharp at MIDI 12, and it
cannot go below 42.20 Hz at all. Meanwhile the excitation is scaled by an
*integer* from `lut_resonator_scale`: eight distinct levels at MIDI 45, three
at MIDI 36, and below MIDI 32 the shift floors every sample to zero and the
model is digitally silent. That is the sound, and an accurate filter
reproduces none of it. This port therefore runs twin peaks in integers, exactly
as the module does, and lands at 0.00 dB against it.

**Both filtered models saturate.** NOIS and TWNQ end in
`Interpolate88(ws_moderate_overdrive, …)`, a `tanh(2x)` shaper. Plaits' noise
engine has no saturation stage at all, which is where the spare macro went.

**Filtered noise, though, is a re-skin.** White noise through one Chamberlin SVF
with a lowpass-to-bandpass-to-highpass morph is what Plaits' filtered-noise
already is, with a better noise source and a resonance control on top. All NOIS
has that the neighbour lacks is that overdrive stage and Braids' particular
damp curve. It is here because the slot holds three models and dropping one
would break the set, not because it does something new.

## The controls

Braids gives each model two knobs, and they do different jobs in each:

| | TIMBRE | COLOR |
|---|---|---|
| NOIS | resonance, and the gain correction that follows it | lowpass → bandpass → highpass |
| TWNQ | resonance up *and* output gain down, one knob doing both | the second peak, ±64 semitones |
| CLKN | the loop length of the random sequence | 2 to 32 quantization levels |

HARMONICS selects the model. TIMBRE and MORPH carry Braids' two axes. MACRO is
the only added one: Drive, the gain into that overdrive shaper. NOIS and TWNQ
are already inside it at unity, so their detent is the module exactly and the
knob works in both directions; CLKN has no shaper in Braids, so its detent
bypasses it and only the upper half does anything.

## Rate

The three functions disagree, which is the whole reason the engine runs a 2x
internal loop at Braids' 96 kHz and lets each model choose its own update rate.
`RenderFilteredNoise` and `RenderClockedNoise` have no `size -= 2` and are
native 96 kHz algorithms, so they run every sub-sample.
`RenderTwinPeaksNoise` ends in `size -= 2` and writes each sample twice — a
48 kHz algorithm zero-order-held up to 96 kHz — so the port updates it on the
even sub-sample and holds through the odd one, reproducing the hold rather than
just the rate. Nothing has internal oversampling on top of that, and because
the internal rate matches the module's, every rate constant transfers verbatim.

Clocked noise is the one place the rate is directly audible: Braids snaps its
clock onto an exact divisor of the sample rate, so at MIDI 45 the clock can only
be 880.7 Hz or 872.7 Hz and nothing between. The module lands on one and the
port — deriving frequency from Plaits' corrected sample rate — lands on the
other, measured 12.7 cents apart. The grid is coarser than the correction, so
that is not fixable, and by MIDI 60 the module's own step is already 38 cents.

## Measured

`tests/ab.json` holds eighteen cases, every model at both ends of both Braids
axes, all rendered against `braids/test/render_braids_model`. Twin peaks agrees
to 0.00 dB of AC RMS and 0.00–0.03 dB across octave bands. Filtered noise is
within 0.07 dB and 0.11 dB except in full highpass, where it reads −0.31 dB and
0.34 dB with 57% of its energy in the top two bands — that is the decimator, not
the filter. Clocked noise is within 0.06 dB and 0.13 dB everywhere except a
deliberately short loop, whose level is the statistics of seven random values.

Two of those numbers were bugs before they were numbers, and both are worth
recording because neither is visible by reading the code. The second peak's
offset is `(parameter_[1] - 16384) >> 1` over a pitch in 128ths of a semitone;
taken as a float fraction of the knob instead, it lands *on* the note at the
detent rather than one 128th below it, which is inaudible but crosses a step in
the integer scale table and cost 1.64 dB at MIDI 36. And running the resonators
in float rather than int32 cost 1.15 dB at the same note, because the
recursion's own truncation noise is the same order as a three-level excitation.
Both read as perfectly plausible renders.

Every table this engine embeds — `ws_moderate_overdrive`,
`lut_resonator_coefficient` and `lut_resonator_scale` — was reproduced from its
generator in `braids/resources/` and agrees with `braids/resources.cc` at 0 LSB.
Both copyright lines are carried in `LICENSE` and in each source file; the
declared deviations are listed in the header comment of
`plaits/dsp/engine2/noise_bank_engine.h`.
