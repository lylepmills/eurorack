# Raw FM

Braids' three FM models — FM, FBFM and WTFM — merged onto one MORPH axis.
They differ by a single line each: plain FM has no feedback, FBFM feeds the
output into the modulator's **phase**, and WTFM feeds it into the modulator's
**frequency**.

**This overlaps the shipped `two-op-fm` and needs an A/B before it ships.**
Three of four knobs are the same axes. What genuinely differs: unfiltered
full-bandwidth feedback with no one-pole on the path; no oversampling, where
`two-op-fm` runs 4×; a linear index law where `two-op-fm` squares it; WTFM's
chaotic frequency feedback, which `two-op-fm` cannot reach at all; the
modulator on AUX; and MACRO as feedback depth.

**It aliases more than `two-op-fm`, and that is the point rather than an
oversight.** Émilie's own milder engine runs 4× oversampling, a squared taming
ramp and a 0.05 one-pole on the feedback path; this runs none of them at
48 kHz. That rawness is the reason to keep it *next to* `two-op-fm` rather
than instead of it.

One deviation: Braids centres WTFM's frequency feedback on 129/256 = 0.504,
which slides the modulator a full octave across a knob sold purely as
topology. The port centres the same feedback on 1.0, so the pitch is
continuous.

Both copyright lines are carried in `LICENSE` and in each source file.
