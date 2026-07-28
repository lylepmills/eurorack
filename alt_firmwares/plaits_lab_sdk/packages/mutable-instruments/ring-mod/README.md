# Ring Mod

A port of Braids' RING: a carrier ring-modulated by two independently detuned
sines, through a saturating shaper.

The DSP is Emilie Gillet's `DigitalOscillator::RenderTripleRingMod`. MORPH and
MACRO are new — MORPH fades the two modulators in from a bare carrier, MACRO
drives the shaper, and at the MACRO detent the drive is 1.0 and the shaper is
Braids'.

Two substitutions, both measured rather than asserted. Braids'
`ws_moderate_overdrive` table turns out to be `tanh(2x)/tanh(2)` to within a
thousandth, and `stmlib::SoftLimit` is the Padé form of tanh, so the 514-byte
table reduces to two multiplies and a divide with a worst-case error of 0.0061
over the range Braids actually drives. And where Braids sends its 24–48 kHz
content out through the DAC, the port has to fold it, so it decimates through
a 15-tap halfband whose measured response is in the header comment.

Both copyright lines are carried in `LICENSE` and in each source file.
