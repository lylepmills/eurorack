# Saw Comb

A port of Braids' saw-into-comb hybrid: an `AnalogOscillator` saw written into
the buffer, then `DigitalOscillator::RenderComb` filtering it in place.

`RenderComb` carries no `size -= 2`, so it is a 96 kHz algorithm — and a
4,096-tap line at 48 kHz reproduces Braids' 8,192 taps at 96 kHz: 85.29 ms
against 85.33 ms, an 11.73 Hz floor against 11.72 Hz, the difference being the
two taps the interpolation guard reserves. So the bottom of TIMBRE clamps below
MIDI ~70 just as it does on hardware, and `comb-lowest` — the A/B case that
runs both sides into that clamp — is the tightest case in the file.

What separates this from `reed-pipe` and `loopback`: the comb pitch is
decoupled ±64 semitones from the note, the feedback is genuinely **bipolar**,
and the exciter is band-limited. MORPH and MACRO are new.

**MORPH 0, not MORPH noon, is the module.** MACRO has a detent and 0.5 is
Braids' flat loop; MORPH has none, and Braids' saw exciter sits at MORPH 0.
Noon gives a half-square at 37.5% duty, which the module cannot make — a
measurable distance away, not a nuance (`morph-noon-not-stock` pins it at
1.86 dB spectrum against the stock case's 1.10 dB).

**At HARMONICS noon the engine is not silent.** The write-back is `0.5*in` and
the output is `0.5*dry` plus one echo — a fully audible FIR comb. Resonance
runs from inverted, through that, to ringing.

The bright half of MACRO needed care. The in-loop shelf has an HF gain of
`1 - 0.788*tilt`, reaching 1.473 at the extreme, so a fixed pre-scale leaves
net HF loop gain above unity and the comb self-oscillates well below the
HARMONICS setting that should do it. The feedback is divided by the actual
shelf peak instead, and the `loop-gain` scenario pins that corner.

Both copyright lines are carried in `LICENSE` and in each source file.

## Measured against the module

`tests/ab.json` is the reproducible A/B — fifteen cases, both ends of both
Braids axes, `python3 ab_engine.py packages/mutable-instruments/saw-comb
--bands`. It passes at its declared tolerances, but several of those
tolerances are looser than the ~1 dB AC RMS / ~1.5 dB spectrum target, and the
file names why rather than absorbing it. Pitch is inside 0.7 cents everywhere
the estimator is usable. Six deviations are known, **all reported and none
fixed** — each is a digest move. The first three are declared in the engine
header; **4, 5 and 6 were found by the independent audit pass on 2026-07-29 and
are recorded here rather than in the header, because `saw_comb_engine.h` is
hashed into `package_digest` and writing to it again would move this shipped
engine's digest a second time**:

1. **The comb sits 11.72 cents sharp of the module.** Braids' comb-pitch
   smoother is an integer recursion whose `>> 4` floors, so it stalls 15 LSB
   flat of its target and stays there; the port's float pole converges
   exactly. The module has a comb null the port does not: +10.18 dB in
   5–10 kHz on the stock case, and the dominant residual in every other case.
2. **The output clipper is soft where Braids' is hard.** `SoftClip` attenuates
   below the rail as well as at it, costing −1.25 dB AC RMS on the stock case.
   Reverting only 1 and 2 in a scratch build takes the stock case to −0.00 dB
   AC RMS and 0.39 dB spectrum.
3. **The block-rate loop bound is not a backstop.** `|feedback| * (1 + |tilt|)`
   overstates the shelf's peak gain on the damping half, where it is exactly
   1.0 at DC. At HARMONICS 1 the bound cuts feedback to 0.625 at MACRO 0,
   removing 4.08 dB of resonance range for no stability reason. It is inert
   only at the MACRO detent, which is why no A/B case can see it.
4. **The module's comb pitch has a ceiling the port does not.** Braids'
   `ComputeDelay` opens with `if (midi_pitch >= kHighestNote - kOctave)
   midi_pitch = kHighestNote - kOctave` (`digital_oscillator.cc:74-76`), and
   for the `DigitalOscillator` those constants are `140*128` and `12*128`, so
   the comb pitch saturates at MIDI 128 — 7.2236 taps at 96 kHz, 13.29 kHz.
   `saw_comb_engine.cc` has no equivalent: `CONSTRAIN(delay, 2.0f, …)` at
   line 95 does not bind until roughly MIDI 150. Any played note above MIDI 64
   with TIMBRE near its top therefore puts the port's comb up to an octave
   above the module's — at note 84 / TIMBRE 1 it is 21.15 kHz against
   13.29 kHz. Measured by the two `comb-clamp-highest*` cases: 2.22 dB
   spectrum at HARMONICS noon, 4.27 dB with the comb resonant, where the
   10.24–20.48 kHz band reads −13.19 dB while carrying 21.7% of the
   reference's energy. This is the largest deviation in the engine, and the
   original A/B could not see it — every TIMBRE-1 case was at note 45, where
   `comb_note = 109` sits below the ceiling.
5. **The feedback ceiling is unity where the module's is 0.99878.** Braids
   warps COLOR through `Interpolate88(ws_moderate_overdrive, …)`, and that
   table's last two entries are `32728, 32728` (`braids/resources.cc`), so the
   maximum warped resonance is 32728/32768 and the module's loop *decays*. The
   port's `WarpResonance(1.0)` is `1.016129 * SoftLimit(2.0)` = 1.0000
   exactly, so the port's loop is marginally stable. Same class of defect as
   the comb-pitch floor — a truncated table entry reproduced as its closed
   form. It is the mechanism behind the +14 dB HF bands on `res-ringing`. Note
   also that the `SoftLimit` Padé substitution is not exact against the table
   away from the ends: it differs by up to 199/32768 ≈ 0.61% of loop gain
   around COLOR 0.62.
6. **The per-note reset has no counterpart in the module, and it is audible.**
   Braids' `SAW_COMB` is completely inert under a strike — `Strike()` only sets
   `strike_`, and `RenderComb` never reads it — so everything
   `saw_comb_engine.cc:79-81` does on `TRIGGER_RISING_EDGE` is the port's own.
   Clearing the delay line is deliberate and defended in `Reset()`. Seeding
   `comb_pitch_ = 48.0f` is not: at note 84 that forces a 36-semitone comb
   glide through a 4 ms pole at the start of every note. The
   `trigger-transient` case measures it at +22 to +26 dB in the 20–80 Hz and
   320–640 Hz bands against a module that has nothing there. **Read that
   case's bands, not its headline** — the energy-weighted figure is 0.48 dB
   only because those bands carry 0.0% of the reference's energy.

One code comment is also wrong and is corrected here rather than in the
source, for the same digest reason: `saw_comb_engine.cc:97` says "AUX taps a
fifth above". `delay_aux = delay * 1.5f` is a *longer* delay, so the primary
aux tap is a fifth **below**; the `delay / 1.5f` fallback taken when the
longer tap would clamp is the one a fifth above. The behaviour is fine — only
the comment's direction is reversed.
