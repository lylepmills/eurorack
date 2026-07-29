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
oversight.** Émilie's own milder engine runs 4× oversampling and a 0.05
one-pole on the feedback path; this runs neither, at 48 kHz. Both engines do
tame the index with a note-72 ramp — `two-op-fm` squares its version, this one
carries Braids' own linear one — so the taming is not part of the difference.
That rawness is the reason to keep it *next to* `two-op-fm` rather than
instead of it.

Two pieces of the module's behaviour are pitch-dependent and easy to miss on
the bench, so they are spelled out here. In the **feedback-FM** half of MORPH
the modulation index is scaled by a ramp that is unity up to note 72 at a 1:1
ratio and then falls 1.5625% per semitone of *modulator* pitch above it,
reaching silence 64 semitones up: a feedback-FM patch gets progressively
gentler as you play up the keyboard or turn RATIO clockwise, and that softening
is Braids', not a limiter. And in **all three** shapes the modulator itself
stops rising once carrier note + ratio reaches 116 semitones, saturating near
6.6 kHz — so the top of the RATIO axis is a plateau, not a ramp, for high
notes.

Braids centres WTFM's frequency feedback on 129/256 = 0.504, so its modulator
slides an octave down as MORPH crosses into the chaotic region. The port keeps
that centre — it ramps in with the feedback, so plain FM at noon is unaffected
and MORPH 1 is the module — because the octave is part of the WTFM sound
rather than an artefact to design out. The move is named on the MORPH control
instead. The feedback is read through the module's own arithmetic-shift floor,
a 128-step staircase rather than a continuous ramp, so the chaotic trajectory
matches. Measured against the module, the six CHAOTIC_FEEDBACK_FM A/B cases
agree to 0.03–0.14 dB, and every case in `tests/ab.json` is inside tolerance.

Both copyright lines are carried in `LICENSE` and in each source file.
