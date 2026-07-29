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

It covers both ends of both Braids axes on all four shapes. 28 of its 30 cases
pass; **two fail on purpose**. `lp-integrator-corner` and `pk-integrator-corner`
record a known defect: at a low note with the cutoff high and the balance near
noon, the LP and PK models — the two that read the integrator — are up to
5.70 dB down in AC RMS and 3.49 dB across octave bands.
`bp-integrator-control` is the same note, cutoff and MORPH on a model with no
integrator and passes, which is what shows the difference is the integrator
rather than the decimator or the note. BP and HP match everywhere tested
(within the decimator allowance at the top of TIMBRE).

The cause is **not** Braids' fixed-point integrator arithmetic. It is that
Braids' sine table is not a sine: `wav_sine` is `32639 * -cos(2πx) + 127`, so
every sample carries a +0.4% DC offset, and this port reads a zero-mean
`lut_sine` instead. That offset is what drives Braids' integrator into both
rails once per half carrier cycle. Restoring it — and nothing else — in a
replica of both loops takes LP from −5.02 dB to +0.01 dB and its band spread
from 3.77 dB to 0.02 dB; restoring the integrator's arithmetic floor instead
makes LP worse. Details and the measurement are in the header comment of
`plaits/dsp/engine2/z_filter_engine.h`. Nothing in this engine's user-facing
copy should be read as claiming full fidelity on LP or PK until it is fixed —
and fixing it changes DSP, so it moves a digest and needs a builder rollout.

The source stays in `plaits/dsp/engine2` while this reference manifest proves
the package contract and supplies the render scenarios — including
`phase-wrap`, which pins the top of TIMBRE at a high note where an unwrapped
float phase would walk off the end of `lut_sine`.
