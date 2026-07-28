# Bytebeat

The first engine in the catalog from someone else's Braids firmware.

A port of the four BYTEBEAT models Tim Churches added to Braids in
[Bees-in-the-Trees](https://github.com/timchurches/Mutated-Mutables). A bytebeat
is an integer expression over a free-running counter `t` whose low byte is read
straight out as the sample — no oscillator, no table, no filter. The four
expressions are carried over character for character, with their original
attributions in `LICENSE`.

The four models differ only in which expression runs, so they fold onto MORPH
and four Braids models fit in one slot.

## Why it earns a slot

Nothing else in the catalog makes this sound. `chiptune` is a clean pulse/noise
console voice; this is aliased integer arithmetic with long non-repeating
structure — a whole pattern rather than a waveform, and it is the only engine
here where the note you play sets the *clock* of a generative process instead of
the pitch of a tone.

It is also the cheapest engine in the port by a wide margin: **0.29× `triple`**
and **0.05× `two-op-fm`** on the host bench.

## Three deliberate divergences from upstream

Each is a defect in the original rather than a matter of taste.

**Pitch.** Braids computed `bytepitch = (16384 - pitch_) >> 11` and advanced `t`
once every `bytepitch` samples. `pitch_` spans 0–16383 in 1/128ths of a
semitone, so that integer takes **nine distinct values across the entire
keyboard** — the mapping is inverse-linear rather than exponential, and at the
top of the range it reaches zero, making `phase_ % bytepitch` a division by
zero. Here `t` advances by a fractional increment of `f0 * 256` per sample, so
the engine tracks 1V/oct. Below one tick per sample that is the same zero-order
hold Braids performed; above it, ticks are skipped, which is the same decimation
Braids did at `bytepitch == 1` and is where the harsher high register comes
from.

**`t % p1` in expression 3.** `p1` was `parameter_[1] >> 8`, so zero is
reachable — a modulo by zero at the bottom of the COLOR knob. Clamped to 1.

**The trigger restarts the stream.** Churches deliberately left `t` running
across a model change ("Don't reset the bytebeat counter to allow continuity
when switch models"), which suits a Braids model with no gate input. In Plaits a
trigger opens the LPG and the attack has to be repeatable, so a rising edge
restarts `t` — at 1024, not 0, because three of the four expressions gate on
`t >> 10` and are exactly silent below it. That offset is read off the
expressions, not chosen.

## MACRO is the mask width

Every expression ends in `& 0xFF`, read as a signed byte. That 8 is the only
arbitrary constant in a bytebeat, so MACRO opens it: 2 bits at the bottom (a
squared-off two-level stream), 12 at the top (higher bits of the expression leak
in and it turns wilder and drops in register). Stock 8 sits exactly at the
detent.

The signed read is reproduced rather than approximated. `(expr & 0xFF) << 8`
stored into an `int16` wraps everything from 128 up into the negative half, so
the byte is effectively two's complement — `(byte - 128) / 128` would be a
different waveform, not a DC-shifted one.

## Outputs

OUT is the stream. AUX is the same expression at half the tick rate — a
sub-octave that stays locked to it. Pattern A: no separate stereo render path,
no flash cost.

DC-blocked at both outputs; a signed-byte bytebeat carries a large and
content-dependent DC term that the audio-health gate rejects.
