# Triple

Braids' four TRIPLE models — square, saw, triangle and sine ×3 — merged into
one slot. They differ only in the base waveform, so MORPH turns that into a
continuous axis and four models fit where one used to: the best ratio in the
port after `z-filter`.

**What this overlaps, so the A/B is fair.** `virtual-analog`'s first control is
literally a detune across musical intervals, `swarm` is a detuned cloud, and
the `chords` table is builder-configurable in cents with entries like
`{0, 1, 1199, 1200}` already present. Four voices at arbitrary cent intervals
are reachable today at zero marginal flash. What is *not* reachable is the
gesture: two interval knobs you sweep, rather than a table you author.

**Unison at noon is load-bearing.** What makes it reachable is index 32 with a
zero crossfade: every parameter from 16384 to 16703 lands on that rung and
detunes by exactly nothing. The 255/256 crossfade one step below it does not —
Braids ends its crossfade with an integer shift, which floors that position to
−0.781 cents rather than the −0.012 cents exact arithmetic would give. The
index and crossfade arithmetic is reproduced rather than tidied, because a
cleaner re-parameterisation breaks the plateau silently.

**Fixed, 2026-07: the ladder now floors the way the module does.** Wave 1
shipped `LadderDetune` evaluating that crossfade in float and never flooring it,
so the port's detune ran up to 0.769 cents sharp of the module's — worst at
exactly the parameter a centred knob produces, and on both upper voices at once.
That turned the module's slow chorus at noon into a hard unison. The crossfade
now runs in Braids' own int16 1/128-semitone units and shifts where Braids
shifts. Swept exhaustively, all 32768 parameter values agree with the module
exactly, and the zero-detune plateau lines up at 16384–16703 on both sides
(wave 1's stopped at 16640 — the same cause, second symptom, now also gone).
`square-high` went from failing at +1.24 dB AC RMS and 1.49 dB spectrum to
−0.01 dB and 0.50 dB, matching its `square-high-exact` control; `saw-unison`
went from +0.41 dB and a +1.68 dB excess in the 320–640 Hz band to −0.01 dB and
−0.03 dB. The `-exact` pairs that isolated the defect stay in `tests/ab.json` as
the regression guard: if the floor is ever dropped again, each knob-centre case
separates from its twin immediately.

**Fixed, 2026-07: the output filter is gone, because `RenderTriple` has none.**
Wave 1 ran both outputs through a one-pole DC blocker at 0.999 — corner ≈7.6 Hz
— justified in the header as sitting "well below anything three detuned voices
produce". That was false. The ladder's bottom rung is −24 semitones, so an A0
root puts a voice at 6.875 Hz, essentially on the corner: energy below 20 Hz
fell from 12.73 % of the module's total to 8.48 % of the port's, −2.9 dB
absolute, and `saw-low-dc` failed its guide tolerance at −1.16 dB AC RMS. It now
reads −0.63 dB and passes. What the blocker was actually for — the DC a narrow
pulse carries at the top of MACRO — is now subtracted in closed form inside the
voice loop, where it is exactly `square_amount × (1 − 2·pw)`. That term is
bit-exactly zero at `pw = 0.5`, which is the whole lower half of MACRO and so
everything Braids can reach, and zero again through saw, triangle and sine. The
A/B is byte-identical with the correction present and with no correction at all;
the health gate's narrow-pulse scenario goes from 0.2662 DC unfiltered to
−0.00086.

MACRO's range is minimum-equals-stock, not bidirectional: a pulse of duty *d*
and one of duty *1−d* have identical harmonic magnitudes, so a bidirectional
range would be a knob whose halves are spectral mirror images at half the
resolution. It is inert in the sine region, stated rather than papered over.

**How an existing patch changes.** Both fixes ship together and both are
audible; the first is loudest at the setting people actually leave the knobs on.

* **Unison at noon now beats.** With both interval knobs near centre the two
  upper voices sit 0.781 cents flat instead of 0.012 — roughly one beat every
  20 s at A2, one every 2.5 s at A5. That is the module's sound and the reason
  the model is worth having, but a patch built on wave 1's dead-still unison
  will now drift and thicken. The dead-still unison is not lost: it is the
  plateau just above centre, from knob 0.5005 up, which is where it is on the
  module too.
* **Interval sweeps step instead of gliding.** Everywhere else on the ladder the
  detune moves by at most 0.77 cents, downward, and snaps to a 1/128-semitone
  grid. Inaudible as pitch on one voice; audible as beat rate against the root.
* **Low notes get their bottom back.** A root at or below A0, or any patch using
  the −24-semitone rungs under a low root, regains the sub-20 Hz energy the
  blocker was removing: +0.53 dB of AC RMS on the A0 case, essentially all of it
  beneath 20 Hz. That is the measured size of the change, not the full −1.16 dB
  that case used to read — 0.63 dB of that headline was the octave-coincidence
  residual in point 2 below and was never the filter. The AUX root voice loses
  the blocker as well, worth +0.3 dB at an A0 root and +0.9 dB at a C0, and
  nothing above A1. Expect more cone movement, and more DC into whatever follows
  the module.
* **Narrow-pulse MACRO no longer thumps.** Above the detent the sound is
  unchanged, but the offset is now removed exactly rather than chased by a
  7.6 Hz filter, so a fast MACRO move no longer produces a settling thump.

**One open deviation remains, and one known residual.**

1. **TRIGGER is a port addition, not a ported behaviour.**
   `MacroOscillator::Strike` (`braids/macro_oscillator.h:80-81`) forwards only
   to `digital_oscillator_`, and `RenderTriple` runs entirely on
   `analog_oscillator_[0..2]`, so the module ignores the trigger here
   completely. The port's `Reset()` realigns all three phases, which the
   catalog manual states, and also reseeds `slave_frequency_` to 0.01 through
   `VariableShapeOscillator::Init()`, which the manual does not — so each
   trigger re-applies the first-block frequency ramp worth +39.0° on a 27.5 Hz
   voice against +31.9° on a 110 Hz one. Cost at two triggers a second:
   +5.75 dB in the 20–40 Hz band against +0.04 dB untriggered, on 0.1 % of the
   energy. The `saw-trigger` case measures it and passes. (Wave 1 read +6.04 dB
   here and charged part of it to the DC blocker's step response; with the
   blocker gone the band barely moved, so the phase discontinuity is the whole
   story.)
2. **Exact octave detunes read quiet, and it is not in this engine.** At exact
   octave ratios the voices' harmonics coincide, so the sum depends on the phase
   they settle into — and Plaits' stock `VariableShapeOscillator::Init` seeds
   `slave_frequency_ = 0.01f`, whose first-block ramp leaves a permanent offset
   that differs per voice frequency, while Braids ramps its own
   `phase_increment_` from a different seed. `saw-wide` reads −1.12 dB with
   matching spectral shape (0.85 dB overall) and keeps a 1.5 dB level tolerance
   for it; `saw-low-dc`'s residual −0.63 dB is the same effect on a smaller
   span. Fixing it means changing a shared oscillator every other engine uses,
   so it is reported here rather than patched from this package.

The interval table itself lives in `plaits/dsp/engine2/triple_engine_data.h`,
which is **not** covered by `package_digest` — the catalog schema accepts only
`.cc` files in `source.files` (`plaits_lab.py:489`), so all 65 tuning values
could change without moving this engine's digest. `vowel-fof` avoids this by
keeping its tables in a declared `vowel_fof_data.cc` behind an extern-only
header. Fixing it here is itself a digest move, so it is reported, not made.

Both copyright lines are carried in `LICENSE` and in each source file.
