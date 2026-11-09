# Swept Kick

Prototype 909-style kick. Plaits' synthetic bass drum lands near this sound
by accident; this engine models the drum's parts on purpose, so each can be
set without moving the others:

- a VCO, a triangle waveshaped toward a sine, restarted at every strike;
- a pitch envelope that sweeps the VCO down onto the note (Tune);
- a VCA that holds for 1.5 ms and then decays (Decay);
- an attack layer: a decaying 1.8 kHz click plus a band-limited noise burst
  (Attack).

Shape is the fourth macro. Noon is the reference waveshaped triangle;
counter-clockwise cleans it to a sine and clockwise drives it into soft
saturation.

In mono, AUX carries the attack layer alone so it can be processed apart from
the body. In stereo the body stays centred and each side gets its own noise.

Not yet reviewed for Fast FM, linear TZFM or hard sync, and not registered in
the approved catalog.
