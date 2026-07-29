# Z Filter

A port of Braids' four "digital filter" models — ZLPF, ZPKF, ZBPF and ZHPF —
into a single Plaits engine, where HARMONICS selects the model and the fourth
macro bends the burst envelope Braids welded to a straight ramp — a descending
saw below the middle of COLOR, a triangle above it.

The DSP is Emilie Gillet's `DigitalOscillator::RenderDigitalFilter`. That
function carries no `size -= 2`, so it is a native 96 kHz algorithm; this port
runs the same loop at twice the 48 kHz output rate and decimates, which is why
every rate constant transfers verbatim. Both copyright lines are carried in
`LICENSE` and in each source file.

Declared deviations from Braids are listed in the header comment of
`plaits/dsp/engine2/z_filter_engine.h`.

`tests/ab.json` is the reproducible A/B behind that list. Run it with

```
python3 ab_engine.py packages/mutable-instruments/z-filter --bands
```

It covers both ends of both Braids axes on all four shapes, and **all 30 cases
are within tolerance**. Below the cutoff clamp, AC RMS is within 0.11 dB and
the octave bands within 0.34 dB. At the clamp every model reads quiet — 0.6 dB
on LP, 1.2 dB on PK, 1.6 dB on BP and 1.6 dB on HP, up to 1.7 dB on the highest
note tested — with up to 0.96 dB of band spread; that is this port's 3-tap
decimator
against the harness's 127-tap sinc, and running the same loop at 96 kHz against
a native 96 kHz reference collapses it to 0.00 dB.

`lp-integrator-corner` and `pk-integrator-corner` used to fail on purpose. They
recorded a real defect that shipped in wave 1: at a low note with the cutoff
high and the balance near noon, LP and PK — the two models that read the pulse
integrator — were 5.70 dB and 4.82 dB down in AC RMS with 3.49 dB and 4.44 dB
of band spread. **That is fixed.** Those two cases now read −0.20 dB / 0.74 dB
and −0.52 dB / 0.96 dB, and no tolerance was widened to get there.

The cause was that Braids' sine table is not a sine. `wav_sine` is
`32639 * -cos(2πx) + 127`, so every sample carries a +0.4% DC offset, and the
port read a zero-mean `lut_sine` and accumulated in float. That offset is what
drives Braids' integrator into both rails once per half carrier cycle; without
it the port's integrator never reached a rail at all. The fix reproduces
Braids' arithmetic rather than its ideal value: the table's gain and offset,
the same downward bias as `Interpolate824`'s arithmetic shift, the `uint16`
pulse ramp and integrator gain, both flooring `>> 16`s, and an `int32`
accumulator rectified by `CLIP`. Details and the measurements are in the header
comment of `plaits/dsp/engine2/z_filter_engine.h`.

**This changes how existing patches sound**, wherever the integrator is in the
mix. Those models gain body: the integrator now saturates instead of hovering
near zero, so their low end goes from thin to the hard-limited buzz the module
makes. Measured old loop against new loop directly, at 96 kHz so the decimator
is out of it, with TIMBRE at the top and MORPH at the balance peak:

| output | note 24 | note 36 | note 48 | note 60 |
|--------|---------|---------|---------|---------|
| LP, OUT | +5.03 dB | +2.89 dB | +1.28 dB | +0.56 dB |
| PK, OUT | +3.48 dB | +1.23 dB | +0.65 dB | +0.21 dB |
| HP selected, AUX (= LP) | +7.41 dB | +3.51 dB | +2.62 dB | +1.56 dB |
| BP selected, AUX (= PK) | +2.61 dB | +1.36 dB | +0.63 dB | +0.23 dB |

A bass patch mixed to sit under something else will now be too loud and want
its level pulled back by a few dB, and its timbre is squarer, not just louder.
The effect fades with MORPH — at note 24 it is +0.69 dB at MORPH 0.1, +1.05 dB
at 0.9 and −0.01 dB at either end, where the integrator is out of the mix.

Two things this does **not** do. It does not spare BP and HP patches: their OUT
is unchanged, but AUX runs the complementary model, so an HP selection's AUX is
LP and a BP selection's AUX is PK — the last two rows above, and the HP row is
the largest change this fix makes anywhere. And it does not vanish with the
cutoff down: at TIMBRE 0 the LP *level* barely moves (+0.18 / +0.30 / +0.51 dB
at notes 24 / 48 / 72, PK under +0.1 dB) but the *waveform* does, because
Braids' integrator rails there too — LP's octave-band agreement with Braids
goes from 0.83 dB to 0.02 dB at note 24 and 1.14 dB to 0.07 dB at note 48.

Every model, on both outputs, is 0.034 dB quieter, because the resonator now
carries `wav_sine`'s true amplitude rather than a unit one.

The source stays in `plaits/dsp/engine2` while this reference manifest proves
the package contract and supplies the render scenarios — including
`phase-wrap`, which pins the top of TIMBRE at a high note where an unwrapped
float phase would walk off the end of `lut_sine`.
