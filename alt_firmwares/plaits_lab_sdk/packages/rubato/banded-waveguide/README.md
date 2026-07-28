# Banded Waveguide

A bowed bar, built on Essl and Cook's banded waveguides from
[The Synthesis ToolKit](https://github.com/thestk/stk)'s `BandedWG`.

**This is an adaptation, not a port.** Three of upstream's four control ranges
measured as partly silent when re-exposed as Plaits knobs, one of its four
materials was inaudible under a bow, and its bow has no noise at all. All four
are fixed below. Nobody arrives at this engine with expectations from the
original, so it is tuned to be played rather than to match.

Each mode of a stiff object gets its own delay loop closed by a bandpass at that
mode's frequency. Because every mode is a travelling wave rather than a filter,
an exciter placed inside the loop interacts with all of them at once — and that
is the point. **A bow needs something to grab and release against, and a bank of
resonators has no travelling wave to grab.**

## Why it earns a slot

`modal-resonator` strikes a bank of modes. `bowed` bows a string. **Nothing bows
a bar.** Bowed vibraphone, musical saw, glass harmonica and the Tibetan prayer
bowl Essl and Cook presented at ICMC'02 are one preset knob apart here, and none
of them is reachable from a struck modal bank or a bowed string.

Four materials on HARMONICS: uniform bar (modes at 1, 2.756, 5.404, 8.933 — the
free-free bar's inharmonic series), tuned bar (a marimba bar, undercut so the
second mode lands two octaves up), glass harmonica, prayer bowl.

The bowl is the interesting one and the reason the mode count goes to six: its
modes come in near-unison **pairs** (0.9961/1.0039, 2.979/2.993, 5.704/5.704)
and the slow beating between each pair *is* the sound of a singing bowl. Four
modes would keep two pairs. Upstream ships twelve, of which the top six are a
long tail of high partials costing two delay lines each.

## The patent note upstream carries is spent

STK's header warns that waveguide models may be "subject to patents held by
Stanford University, Yamaha, and others". The Stanford waveguide patent is
US 4,984,276, filed 1989-09-27 and issued 1991-01-08. Under the pre-1995 term
rule — the later of 17 years from issue and 20 years from filing — it expired no
later than 2009-09-27.

## Memory is the constraint, not flash

Each mode needs a delay line of fs/(f0 × ratio) samples, so the bill is set by
the lowest note times the smallest mode ratio in any preset. Sized for A1
(55 Hz) against the bowl's 0.9961 and 1.0039 — two nearly-full-length lines —
which comes to about 10.5 KB of the voice's 16,384-byte arena. Notes below A1
fold up by octaves rather than allocating for a register no bar or bowl occupies.

## Four things were fixed against measurement

Upstream's control mappings assume a MIDI controller, a sustain pedal and a
player. Swept as four Plaits knobs they leave large dead regions, all three found
by sweeping the engine rather than by ear:

**Bow speed (MORPH).** The bow table's grip falls off as the *fourth power* of
the difference between bow and bar velocity, so there is a genuine minimum speed
below which a ringing bar is damped rather than driven — a real bow has one too.
It sits near 0.055; upstream's floor of 0.03 measured as **silence over the
bottom fifth of the knob**. The range now starts at the threshold. There is a
best speed in the middle, which is also true of the real thing.

**Bow force (TIMBRE).** Upstream runs the friction slope to 1.0, which measures
as silence from about 0.6 of the knob up — a broad friction curve simply cannot
hold the bar. Stopped at 3.0, where the character has fully changed and the bar
still speaks, with velocity compensated for force the way a player trades them.

**Materials (HARMONICS).** The uniform bar's mode gains are pow(0.9, i+1), so
its loop gain is 0.899 against the other presets' 0.999 — under a bow it
measured **600× quieter** than its neighbours. Upstream never hits this because
a uniform bar defaults to being *struck*, and a struck bar wants exactly that
damping. Presets are now normalised so the fundamental always sees the full loop
gain and only the relative damping between modes is the preset's.

**Bow noise (new).** Upstream's bow is perfectly smooth, which is the main
reason a waveguide bow sounds synthetic — a real bow's grip is granular, rosin
catching and releasing thousands of times a second. Noise is applied to the bow
*velocity* rather than added to the output, so it modulates the friction curve
and colours the attack instead of sitting on top as hiss.

Upstream also makes bow speed zero mean "pluck instead of bow", on a sustain
pedal. Here the pluck moves to the trigger, where a struck bar belongs anyway,
so the knob stays continuously alive.

MACRO is the loop gain — how long it rings once the bow lifts. Capped short of
1.0 because the bandpasses are peak-normalised and the delays lossless, so at
unity the bank self-oscillates whether or not it is being bowed.

OUT is the instrument. AUX is the fundamental band alone — the pitch under the
inharmonic bank, which is exactly what a bar or bowl makes hard to hear.

Host CPU sits between `triple` and the stock `modal-resonator`, both of which
ship -- a smoke signal only; see `bytebeat`'s README for why engine-to-engine
host ratios do not carry to the hardware.
