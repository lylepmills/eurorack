# Shakers

Sixteen instruments in one slot, built on Perry Cook's **PhISEM** — Physically
Informed Stochastic Event Modeling — from
[The Synthesis ToolKit](https://github.com/thestk/stk).

**This is an adaptation, not a port.** STK's resonators are unnormalised and its
sixteen instruments span 46 dB, because STK expects each to be used on its own
behind its own fader. Under one selector knob that is a hazard rather than a
feature, so the resonators are normalised and every instrument carries a
measured makeup gain. Nobody arrives at this engine with expectations from the
original, so it is tuned to be played rather than to match.

PhISEM does not model an instrument's geometry. It models the *statistics* of
many small objects colliding inside a container: a shake energy that decays
exponentially, a per-sample collision probability proportional to that energy
and to the object count, and a handful of fixed resonances measured off the real
object. A maraca is one pole at 3200 Hz and twenty-five beans. A coke can is a
Helmholtz mode at 370 Hz plus four metal modes and forty-eight parts. That is
the whole model, and it is why sixteen recognisable instruments fit where one
usually goes.

Maraca · Cabasa · Sekere · Tambourine · Sleigh bells · Bamboo chimes · Angklung ·
Coke can · Sticks · Crunch · Big rocks · Little rocks · Coins in a mug · Water
drops · Guiro · Wrench.

## Why it earns a slot

The catalog has three analog drum circuits and **no acoustic percussion at
all** — no shaker, no tambourine, no chime.

`particle-noise` is the nearest thing and it is not the same thing: it fires
sparse events into resonators whose frequencies are randomly *spread*, which
makes a cloud. It is a texture generator. PhISEM's resonances are measured off
real objects and its collision statistics are what make a shaken gourd read as a
shaken gourd. You can play a maraca part with this; you cannot with
`particle-noise`.

It is also the cheapest engine in the catalog on the host bench -- a
"not pathologically expensive" signal only, since engine-to-engine host ratios
do not carry to the hardware (see `bytebeat`'s README).

## Three mechanisms, not one

Most instruments are **shake**: energy decays, collisions inject noise. Two are
**ratchets** (guiro, wrench) — a sawtooth energy ramp that resets per tooth,
which is why they rasp rhythmically instead of decaying. One is **water drops**,
whose three resonators are individually retuned and swept upward as each drop is
born. Upstream special-cases all three inside `tick`, and so does this.

## Sample-rate correction is not optional here

Every decay constant, filter radius and collision probability in STK is a raw
per-sample number tuned at 44.1 kHz. Carried unchanged to Plaits' 48 kHz they
would all be ~8.8% wrong in the same direction — everything rings 8.8% shorter
in real time and shakes 8.8% denser. Decays and radii are raised to the power
44100/48000 and the object count is scaled by the same ratio. The frequencies
were always in Hz and need none.

## The resonators are normalised, and the instruments level-matched

Upstream's resonators are all-pole with a numerator of 1, so their peak gain is
about 1/(1−r²) — from 1.6 for a sekere's r=0.6 to **125** for an angklung's
r=0.996. Cook's per-instrument gains only partly offset that; the product still
spans 6 to 62, because STK expects a master gain downstream. His own
commented-out debug line in `tick` is a check for the output exceeding 1.0.

The audition gate here caught it as a 4.09 spike on sleigh bells. Each resonator
is normalised to unit peak instead, which leaves each instrument's gain meaning
what it reads as — its intended loudness.

That alone does not level the selector. Measured across the shake/decay/object
space, the sixteen still spanned **46 dB** — a cabasa 12.7 dB above the mean, a
water drop 33.5 dB below it. Each preset now carries a measured makeup gain
toward a common target, clamped to [0.15, 20] so the two sparsest are lifted as
far as their crest factor allows rather than slammed. **Worst remaining
deviation: 6.5 dB, and thirteen of the sixteen land inside 0.7 dB.**

## Controls

HARMONICS picks the instrument. TIMBRE is how hard it is being shaken (injected
continuously, so it plays without a trigger; a rising edge is one hard shake).
MORPH and MACRO are upstream's own decay and object-count controls, and both of
his mappings already put the measured value at the detent — `nObjects = 2 *
norm * baseObjects + 1.1` is exactly `baseObjects` at 0.5.

A maraca has no pitch, but a Plaits user has a note input and expects it to do
something: the note transposes every resonance around MIDI 60. On the tuned
instruments that is real (angklung is a bamboo scale); on the rest it is a size
control — a small gourd at the top, an oil drum at the bottom.

OUT is the instrument. AUX is the bare collision train before any resonance —
the impact layer, useful under a different body or as a trigger source.
