# Virtual Analog Variant

This built-in reference package unifies Emilie Gillet's two alternate
virtual-analog designs preserved in the Plaits source (`VA_VARIANT 0` and
`VA_VARIANT 1`, formerly the separate Virtual Analog Dual and Virtual Analog
Crossfade models) into one Plaits Palette model.

MORPH is Crossfade's trajectory -- detuned oscillator below noon, primary alone
at noon, hard sync above -- and TWIST spreads the two waveshapes apart around
TIMBRE, so Dual's independent secondary shape is still reachable. With TWIST at
noon the main output is Crossfade's, sample for sample; Dual's outputs are
reproduced to within a float rounding step at the matching settings
(`alt_firmwares/research/twist_tuning_range/test_va_variant.cc`).
