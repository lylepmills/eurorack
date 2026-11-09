# Hats & Rides

Prototype hi-hats, rides and crashes aimed at the 606 and 909.

The two drums get their metal differently. The 606 builds it from square-wave
oscillators, band-passed and high-passed; the Model control's
counter-clockwise end is that recipe, with its own six-oscillator spacing
(close to, but not, the 808 set that Analog Hi-hat uses).

The 909's hats and cymbals were 6-bit recordings of real cymbals, so no
synthesis engine reproduces them exactly. The clockwise end approximates the
recipe instead: the six squares ring-modulated (XORed) against six higher
ones, plus noise, filtered, then resampled at 32 kHz with a zero-order hold
and quantized to 6 bits before the envelope, so the grain fades with the
decay as it does when a VCA follows a DAC.

Decay runs from a closed hat (a 12 ms time constant) to a crash (about 3 s), and the
strike wash grows with it. Spread is the fourth macro, neutral at noon.

In mono, AUX carries the band-passed body without the high pass or sample
stage. In stereo, the two sides have offset filters, independent noise and
staggered resampling clocks.

Not yet reviewed for Fast FM, linear TZFM or hard sync, and not registered in
the approved catalog.
