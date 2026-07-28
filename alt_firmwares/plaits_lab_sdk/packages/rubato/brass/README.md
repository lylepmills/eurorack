# Brass

A lip-reed brass instrument: an outward-striking valve driving a waveguide bore,
after Cook's TBone/HosePlayer and [STK](https://github.com/thestk/stk)'s `Brass`.

**This is a redesign, not a port, and it had to be.** STK's `Brass` does not
sustain. Built standalone and measured across nine (sample rate, pitch)
combinations it sounds in exactly one — 22.05 kHz at 440 Hz — and is silent at
its own default 44.1 kHz. At 48 kHz it emits a 0.019-peak blip for 100 ms and
then *exact* silence. Its lip filter is an all-pole resonator with a DC gain
near 50, so a steady mouth pressure drives the squared lip position past the
model's clamp of 1.0, the valve pins open, the output goes constant, the DC
blocker removes it, and the bore never fills. It works only while the ADSR
attack transient is still ringing the lip.

## Why a lip is worth a slot when the catalog has a reed and a bow

`reed-pipe` drives a bore with an **inward**-striking reed, which closes as
pressure rises and therefore locks hard to the bore. A lip is
**outward**-striking: it opens as pressure rises, and it can be tuned *away*
from the bore. That difference is the whole expressive range of a brass player —
lipping a note flat, overblowing to the next partial, the unstable crack in
between — and it is not reachable from the reed engine at any setting.

## What the research found

Four earlier attempts failed. These are the things that made the difference, in
the order they mattered.

**The junction.** A waveguide junction is `p⁺ = p⁻ + Zc·u` with the total
pressure `p = p⁺ + p⁻`. Earlier attempts used STK's mixing form
(`p = area·mouth + (1−area)·bore`) and fed the valve from the *reflected wave*
rather than the total junction pressure. That is not a scattering junction, it
has no mechanism locking the valve to the bore, and it oscillated chaotically at
a frequency independent of the delay length.

**A non-inverting loop needs an in-loop DC blocker.** A physical open end
inverts, giving odd harmonics only; a real instrument's flare and mouthpiece
shift its modes into a near-complete harmonic series, which one delay line
cannot do. The adaptation is a non-inverting loop — but a non-inverting loop's
lowest mode is DC, and without the blocker the model parks there and the delay
length stops mattering at all. A passive-loop probe confirmed both: inverting
rings at fs/2L, non-inverting at fs/L, and non-inverting *without* the blocker
rings at 0 Hz.

**The nonlinearity has to be gentle or the lip cannot select anything.** With
the lips slamming shut every cycle, the model locks to the bore's *fundamental*
no matter where the lip is tuned — the hard nonlinearity generates a full
harmonic series and the fundamental is the attractor. Opening the valve's rest
position and stiffening it lets the lip resonance decide instead. That single
change is what makes HARMONICS a control rather than a decoration.

**The lock zones are narrow and the gaps between them are huge.** Swept finely,
partial *n* captures for lip/f_bore in roughly **[0.90n, 1.01n]** — about 40% of
a linear lip sweep is silent. So HARMONICS does not map to lip frequency. It
maps to a continuous **partial index**, and the lip is placed inside that
partial's zone. The knob never falls in a gap, and the small sharpening across
each segment is real lipping. The zones also widen with mouth pressure — blowing
harder makes a note easier to hold, as on the real thing — so the softest
playing sets the safe placement.

**Partial 1 is the bad one**, always sharp with an erratic zone. That is true of
real brass too, where the pedal tone is a special effect and the written
fundamental is the *second* partial. So the bore is tuned an octave below the
played note and the usable range starts at its second partial. Lipping up gives
the bugle series above the played note: unison, fifth, octave, tenth, twelfth.

**The lip pulls the pitch sharp** by an amount that fits `−4.8 + 67.2/n` cents
almost exactly, across every note measured. Only one partial sounds at a time,
so it is nulled by lengthening the bore for the selected partial. Over 312
points spanning notes 34–78, both breath extremes and the whole knob: **mean 6.3
cents, worst 25.8**, and about 1% of points crack to a neighbouring partial —
which real players also do.

**The output tap is the mouthpiece, not the bell.** Taking the radiated signal —
the part of the wave the bell does not reflect — is physically right and measured
a **30 dB** level slide across the keyboard, because a fixed bell corner
radiates low notes poorly. The bell corner here tracks the played note instead,
since a V/oct input should give the same instrument at every pitch, but even
then the radiated tap humps by 17 dB. The pressure *inside* the mouthpiece is
flat to 0.7 dB from note 40 up and carries the bore's full standing wave.

**Both taps have to be DC-blocked, and that is easy to miss.** Blowing produces
a steady flow as well as an oscillation, so the raw taps are mostly bias.
Measured before blocking, OUT read RMS 27923 against a DC component of 26548 —
and it passed the audition and control-response gates, which do not look at DC.
It would have reached a module as a fat offset with a tone buried in it.

## Controls

HARMONICS is lip tension and it is the reason the engine exists: it climbs the
bore's partials and bends each one sharp as it rises. TIMBRE is breath pressure,
and below the oscillation threshold the instrument correctly does not speak.
MORPH is bell size. MACRO is the slide, centred at 1.0 — it detunes the bore
against the lips, which is where the growl and the split note come from; it is
not a tuning control and will not stay in tune.

OUT is the mouthpiece. AUX is the valve flow, the buzz that drives it.

Host CPU sits near `reed-pipe`'s, its closest neighbour — a smoke signal only;
see `bytebeat`'s README for why engine-to-engine host ratios do not carry to the
hardware.
