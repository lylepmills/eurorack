# Sub Osc

A port of Braids' two SUB models — SQUARE_SUB and SAW_SUB — merged into one
slot. They differ only in whether the main oscillator is a square or a
variable saw, so HARMONICS turns that into a continuous axis and two Braids
models fit where one used to.

The DSP is Emilie Gillet's `MacroOscillator::RenderSub`. MORPH is Braids'
COLOR verbatim, including its shape, which is worth knowing: the sub level is
a **V**. Fully down gives an equal blend with the sub two octaves below,
**noon gives no sub at all**, and fully up gives an equal blend one octave
below. The sub is loudest at both ends, and Braids never lets it exceed an
equal blend.

MACRO is new — Braids welds the sub to a plain square and this narrows its
pulse. Minimum equals stock, so the detent and everything above it are Braids.

AUX carries the sub on its own at full level rather than scaled by the blend,
which would make it silent at the centre of MORPH — the first place anyone
would look for it.

Both copyright lines are carried in `LICENSE` and in each source file.
