# CSaw

A port of Braids' CSAW — the sawtooth with a notch cut into the start of every
cycle, and the model the module is most recognised for.

The DSP is Emilie Gillet's `AnalogOscillator::RenderCSaw` together with the DC
shift and 13/8 make-up that `MacroOscillator::RenderCSaw` wraps around it. That
shift is applied BEFORE the make-up, so the DC term is `1.625 * 2047 / 32768`;
re-deriving it as `2047/16384 * 0.5` lands 4.2 dB low and makes the HARMONICS
sweep pump.

MORPH and MACRO are new. MACRO tilts the notch plateau, and at its top the
value step at the notch edge cancels entirely — leaving a pure slope
discontinuity, which is why the engine carries integrated BLEP as well as
value BLEP. The `bend-high` scenario pins that corner at a high note.

Both copyright lines are carried in `LICENSE` and in each source file;
declared deviations are listed in the header comment of
`plaits/dsp/engine2/csaw_engine.h`.
