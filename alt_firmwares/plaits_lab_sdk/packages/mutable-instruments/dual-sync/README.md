# Dual Sync

A port of Braids' SQUARE_SYNC and SAW_SYNC — a master oscillator at the played
pitch hard-syncing a second one that sits a swept interval above it, with a
knob crossfading between the two.

The DSP is Emilie Gillet's `MacroOscillator::RenderDualSync` driving two
`AnalogOscillator` instances through `RenderSquare` and `RenderSaw`.

## Two models, one engine

The only thing separating the module's two entries is the shape handed to both
oscillators (`macro_oscillator.cc:261-262`). So Shape carries the choice: 0.0
is SQUARE_SYNC, 1.0 is SAW_SYNC, and the positions between are a blend the
module cannot reach, since there the two are separate menu items. Both models
are measured against their own reference in `tests/ab.json`.

## What it has that Virtual Analog does not

Plaits' virtual analog engine already has a sync square in VA_VARIANT 2, so the
question is what is actually left. Reading
`plaits/dsp/engine/virtual_analog_engine.cc:210-309`: the sync ratio there is
`(timbre - 0.5)^2 * 4 * 48`, which is flat zero for the whole lower half of the
knob and tops out at 48 semitones; the same knob also sets the square's pulse
width and its gain; the saw partner is not synced at all; and nothing anywhere
crossfades master against slave — MACRO there balances the square against a
variable saw.

Dual Sync gives the interval its own knob across the full 63.99 semitones, holds
the pulse width at the fixed 0.5 both Braids models use, syncs the saw as well
as the square, and puts the master/slave balance on a knob. The gesture of
sweeping the sync interval while moving the balance is not reachable in the
neighbour.

## The controls, and the trap in reading them

TIMBRE sets the slave's pitch above the master —
`set_pitch(pitch_ + (parameter_[0] >> 2))` at `macro_oscillator.cc:269`, which
in Braids' 1/128-semitone pitch unit is 0 to 63.99 semitones. COLOR sets the
balance.

The trap is the same one that cost `fold` a rewrite, and it is worth naming
again because this model is even easier to misread: `RenderDualSync` crossfades
with `BEGIN_INTERPOLATE_PARAMETER_1` and `balance = parameter_1 << 1`
(`:274-279`). The macro interpolates `previous_parameter_[1]` toward
`parameter_[1]`, which is COLOR — not TIMBRE. Read as if the suffix meant "the
first parameter", the model looks like TIMBRE sweeps the interval and the
balance together while COLOR does nothing, and that renders as a perfectly
believable sync sweep. `tests/ab.json` measures both ends of the balance on
both shapes for exactly that reason.

MACRO is the one axis the module has no concept of: it moves where in its own
cycle the slave restarts on each sync pulse. Braids always restarts it at zero
(`analog_oscillator.cc:262-264`, `:325-327`), and `ApplyMacro` at noon returns
that value exactly, so every A/B case leaves it there.

## Rate and aliasing

Neither `RenderSquare` (`analog_oscillator.cc:188-272`) nor `RenderSaw`
(`:274-335`) ends in `size -= 2`, and neither has an internal oversampling
step: one output sample per iteration, one `phase_ += phase_increment`. They
are 96 kHz algorithms at Braids' native rate.

That makes the pitch mapping transfer unchanged, but not the anti-aliasing.
A 2-sample polyBLEP leaves a residue whose position depends on the rate it runs
at: at 96 kHz most of it sits above 24 kHz, where the reference renderer's
decimator removes it, and at 48 kHz the same residue folds back into the
audible band. So the port runs 2x to reach the module's 96 kHz internal rate
and decimates through a 47-tap Kaiser halfband.

Measured as non-harmonic energy against total, the port sits within 0.3 dB of
the module at every setting measured (-46.4 dB against -46.7 dB at the stock
centre). The same engine built without the 2x stage measures -38.1 dB there and
-25.6 dB at the corner where the slave is highest and the balance is entirely
on it, so the oversampling is worth between 8 and 21 dB depending on where the
knobs are. The full table, and the accepted residue between 24 and 26 kHz where
this filter is shallower than the reference's 127-tap one, are in the header
comment of `plaits/dsp/engine2/dual_sync_engine.h`.

The filter length is set by the hardware budget, not guesswork. The earlier
95-tap version measured 113% of the calibrated CPU budget and the 63-tap
version's uncertainty band still crossed the deadline. This one measures 76%;
the worst committed A/B case remains only 0.26 dB apart over the whole spectrum.

## The one Braids behaviour this port leaves out

`AnalogOscillator::Render` re-runs `Init()` whenever the shape it is handed
changes, and `Init()` resets the pitch to MIDI 60 after `RenderDualSync` has
already set the played note. So the first time SQUARE_SYNC is selected, both
oscillators render one 24-sample block at MIDI 60 and carry the resulting phase
error for the rest of the note. That is the module, not the test harness, and
the port does not reproduce it.

It costs nothing measurable here, which is worth saying with numbers rather
than assuming. SAW_SYNC never triggers it at all — its shape is the one the
module's zero-initialised oscillator already holds, so nothing changes and
`Init()` never runs. SQUARE_SYNC does trigger it, on both oscillators, but the
slave is hard-synced to the master, so the master's error shifts the whole
output in time and the slave's is erased at the next sync pulse. Against a
reference build with only that line removed, SAW_SYNC comes out bit-identical
and SQUARE_SYNC differs by 0.0011 dB of level and 0.0022 dB of spectrum. No
tolerance in `tests/ab.json` is widened for it.

## Level

Braids pins its output at 0.75 of full scale in int16; the port applies the
same 0.75 but cannot pin its peak there, because polyBLEP overshoot and the
decimator's ringing sit on top of the naive waveform. The peak is a narrow
function of Interval, so it has to be swept finely — at 0.005 steps it reaches
1.015 with MACRO at noon, already past full scale on the plane Braids itself
can reach, and 1.237 once Reset moves off noon. So the engine declares negative
gains and takes the limiter path.

Both copyright lines are carried in `LICENSE` and in each source file; the
declared deviations are listed in that header, and `tests/ab.json` holds the
measured comparison against both modules.

## Hardware validation

Lyle auditioned this engine on Plaits hardware on 2026-07-29 in a dedicated
six-model CPU-risk firmware alongside Harmonics, Vowel FOF, Ring Mod, Snare,
and Z Filter. All six played correctly with no audible real-time overruns.
This was a listening/soak check, not a DWT cycle measurement, so the 76% figure
above remains the calibrated performance estimate.
