# Ring Mod

A port of Braids' RING: a carrier ring-modulated by two independently detuned
sines, through a saturating shaper.

The DSP is Emilie Gillet's `DigitalOscillator::RenderTripleRingMod`. HARMONICS
detunes the first modulator and TIMBRE the second, which is Braids' TIMBRE and
COLOR in that order. MORPH and MACRO are new — MORPH fades the two modulators
in from a bare carrier, MACRO drives the shaper. MACRO has a detent and the
drive is 1.0 there, so it is Braids' shaper at noon; MORPH has no detent, and
the module's sound is at the **top** of MORPH, not its middle — at MORPH 0.5
the modulators are running at half depth.

Two substitutions, both measured rather than asserted. Braids'
`ws_moderate_overdrive` table turns out to be `tanh(2x)/tanh(2)` to within a
thousandth, and `stmlib::SoftLimit` is the Padé form of tanh, so the 514-byte
table reduces to two multiplies and a divide with a worst-case error of 0.0061
over the range Braids actually drives. And where Braids sends its 24–48 kHz
content out through the DAC, the port has to fold it, so it decimates through
a 15-tap halfband whose measured response is in the header comment.

One deviation that is inherited rather than chosen: Plaits' `lut_sine` is a
unit-amplitude sine, while Braids' `wav_sine` fits `127.0 − 32639·cos` — 0.4%
short of full scale, with a small DC offset. Three of those multiply together
here, so the port's ring product reaches the shaper 0.10 dB hot, which happens
to cancel most of the 0.17 dB the Padé shaper gives away. Both figures, and
the A/B they come from, are in the header comment.

`tests/ab.json` is the reproducible A/B against the module: eleven cases,
both ends of both Braids axes. It measures AC RMS within 0.05 dB and spectrum
within 0.25 dB wherever the output stays under Nyquist, and pitch within
0.2 cents on the cases whose f0 the harness can track. Run it with
`python3 ab_engine.py packages/mutable-instruments/ring-mod --bands`.

Both copyright lines are carried in `LICENSE` and in each source file.

## Hardware validation

Lyle auditioned this engine on Plaits hardware on 2026-07-29 in a dedicated
six-model CPU-risk firmware alongside Dual Sync, Harmonics, Vowel FOF, Snare,
and Z Filter. The firmware exercised Ring Mod's gated true-stereo path, and
all six played correctly with no audible real-time overruns. This was a
listening/soak check, not a DWT cycle measurement, so the calibrated CPU
estimate remains the performance figure.
