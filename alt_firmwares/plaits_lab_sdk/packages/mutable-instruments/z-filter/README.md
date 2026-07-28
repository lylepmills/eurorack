# Z Filter

A port of Braids' four "digital filter" models — ZLPF, ZPKF, ZBPF and ZHPF —
into a single Plaits engine, where HARMONICS selects the model and the fourth
macro bends the burst envelope Braids welded to a linear ramp.

The DSP is Emilie Gillet's `DigitalOscillator::RenderDigitalFilter`. That
function carries no `size -= 2`, so it is a native 96 kHz algorithm; this port
runs the same loop at twice the 48 kHz output rate and decimates, which is why
every rate constant transfers verbatim. Both copyright lines are carried in
`LICENSE` and in each source file.

Declared deviations from Braids are listed in the header comment of
`plaits/dsp/engine2/z_filter_engine.h`.

The source stays in `plaits/dsp/engine2` while this reference manifest proves
the package contract and supplies the render scenarios — including
`phase-wrap`, which pins the top of TIMBRE at a high note where an unwrapped
float phase would walk off the end of `lut_sine`.
