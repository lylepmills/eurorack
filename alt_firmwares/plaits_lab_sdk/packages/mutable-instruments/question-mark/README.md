# Question Mark

A port of Braids' `????` — the model the module keeps hidden. It is a Morse-code
transmitter: a packed symbol table keys a sine on and off over a drifting noise
bed, and the sum runs through a squared-distortion stage.

The DSP is Emilie Gillet's `DigitalOscillator::RenderQuestionMark`.

## The model the module hides

`????` is not on the shape list. The firmware selects it only while the MARQUEE
settings page is on screen and the marquee text has been set to `49`
(`braids.cc:193-194`, gated by `ui.h:89-93` and `settings.cc:116-118`, where
`paques_` — French for Easter — is set by a string compare against `"49"`).
Leave that page and the module goes back to the shape you had selected, so on
hardware it is something you visit rather than something you patch.

Here it is an ordinary engine. It is selectable, a trigger restarts the
transmission from the beginning, and it keeps running.

The message is the "Scope" bar passage from Thomas Pynchon's *The Crying of Lot
49*, which is what the `49` unlock refers to. It ships here as the module's own
1064-byte table, unchanged and untruncated, and is not written out as text
anywhere in the port.

## The controls

The module has two knobs and one of them does two jobs at once.

TIMBRE is the keying speed: `dit_duration = 3600 + ((32767 - p1) >> 2)` counted
in 96 kHz samples, so a dit runs from 122.8 ms down to 37.5 ms. That is Speed.

COLOR sets both the noise bed's floor (`1024 + (p2 >> 3)`) and how much of the
squared distortion is fed back in (`distorted * p2 >> 15`). One knob, two
effects, welded. That is Static, kept intact so the module's own sound is one
knob away.

The two macros Plaits adds pull that pair apart, which is the thing the module
cannot do: **Bed** is the noise level on its own, and **Grit** is the distortion
on its own. Both are exactly the module at noon. At Bed 0 you get a bare keyed
tone — unreachable on hardware, where the floor is 1024 even at COLOR 0 and the
bed's random walk climbs away from it regardless.

## Rate

`RenderQuestionMark` ends in a plain `while (size--)`, not `size -= 2`, so it is
a 96 kHz algorithm — and an unusual number of its constants are rate-bearing:
the dit duration, the step size of the random walk that drives the noise bed,
the bandwidth of the noise itself, and the phase increment. Re-deriving four
constants for 48 kHz independently is four chances to get one wrong, so the port
runs the whole state machine and signal path 2x oversampled at Braids' own
96 kHz instead, and all four transfer verbatim. The only quantity chosen for the
port is the decimator, a 2-tap box average.

The carrier keeps the module's own pitch ceiling. Braids' `ComputePhaseIncrement`
saturates at MIDI 127.99, so the transmission stops rising at 13.28 kHz however
far the pitch CV goes, and this port clamps to the same note rather than to the
higher one Plaits' `NoteToFrequency` happens to stop at.

## Tables

Two are embedded, both extracted programmatically and both verified. The symbol
table is byte-identical to the literal its generator carries (0 of 1064 bytes
differ), repacking the decoded stream reproduces every byte, and all 4252
symbols decode to well-formed International Morse — 939 characters in 158 words,
zero unrecognised patterns. Braids' sine table reproduces from its generator's
closed form to 1.68 LSB, which is its dither; it is embedded rather than
computed because one of the two reads of it is truncated and unsmoothed, and
because a plain `-cos` is 254.6 LSB away from it.

## What was measured

Six A/B cases against the module, 8 seconds each, both Braids axes at both ends
plus a high register. Worst AC RMS −0.47 dB and worst energy-weighted spectrum
0.71 dB, both at COLOR 1; the best case reads −0.00 dB and 0.10 dB. Everything
between 80 Hz and 20.5 kHz — 81–94% of the energy, depending on the case — is
within 0.8 dB, and four of the six hold every band there inside 0.55 dB.

A seventh case sits at MIDI 135, past the module's pitch ceiling. It is a guard
rather than a sound: it reads −0.83 dB and 0.56 dB with the ceiling in place and
−1.96 dB / 1.17 dB — a double failure — without it.

No pitch tolerance is declared. A sine gated on and off with long silences over
a broadband bed has no continuous f0, and the estimator duly returns −0.8, −4.6,
−2.1, +10.6 and +455.0 cents and "no f0" across the seven cases, which is the
shape of a reading that is not measuring anything. What that metric exists to catch — a
rate constant transferred from the wrong side of the 96/48 kHz line — is checked
directly instead, and more sharply: the keying grid is predicted straight from
the symbol table (57 gate edges in the first 7.5 seconds), all 57 are found on
both sides to the same 2.0 ms detector offset, and the two carrier envelopes
correlate 0.9913–0.9995 at zero lag with no drift between the first and last
quarter of the window. A mis-transferred tick length would be an octave out and
would fail that on the first symbol.

Two residues are declared rather than hidden. The 20–80 Hz bands run −0.9 to
−4.6 dB in five cases and +0.31 dB in the sixth — the one where COLOR 0 switches
the distortion off, so there is no DC for the engine's DC blocker to remove.
That is the blocker, and the sixth case is the control that proves it. The
20.5–24 kHz band runs +0.6 to +1.2 dB, which is the box decimator folding back
what the clip and the squared distortion put above 24 kHz.

Both copyright lines are carried in `LICENSE` and in each source file; declared
deviations are listed in the header comment of
`plaits/dsp/engine2/question_mark_engine.h`, and `tests/ab.json` holds the
measured comparison against the module.
