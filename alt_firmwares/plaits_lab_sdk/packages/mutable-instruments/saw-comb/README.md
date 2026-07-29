# Saw Comb

A port of Braids' saw-into-comb hybrid: an `AnalogOscillator` saw written into
the buffer, then `DigitalOscillator::RenderComb` filtering it in place.

`RenderComb` carries no `size -= 2`, so it is a 96 kHz algorithm — and a
4,096-tap line at 48 kHz reproduces Braids' 8,192 taps at 96 kHz: 85.33 ms
either way. So the bottom of TIMBRE clamps below MIDI ~70 just as it does on
hardware, and `comb-lowest` is the A/B case that runs both sides into that
clamp.

Braids' own block is 24 samples at 96 kHz and Plaits' `kBlockSize` is 12 at
48 kHz, so **one host block is one Braids block** and every block-rate quantity
— in particular the comb-pitch smoother — transfers 1:1.

What separates this from `reed-pipe` and `loopback`: the comb pitch is
decoupled ±64 semitones from the note, the feedback is genuinely **bipolar**,
and the exciter is band-limited. MORPH and MACRO are new.

**MORPH 0, not MORPH noon, is the module.** MACRO has a detent and 0.5 is
Braids' flat loop; MORPH has none, and Braids' saw exciter sits at MORPH 0.
Noon gives a half-square at 37.5% duty, which the module cannot make — a
measurable distance away, not a nuance (`morph-noon-not-stock` pins it at
1.82 dB spectrum and +2.21 dB level against the stock case's 0.40 dB).

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

`tests/ab.json` is the reproducible A/B — twenty-three cases, both ends of both
Braids axes, all four corners of the two, both sides of the comb-pitch ceiling,
a played note below MIDI 0, two triggered cases and two sweeps: `python3
ab_engine.py packages/mutable-instruments/saw-comb --bands`.

**Corrected 2026-07-29.** An earlier pass measured six deviations, declared
them, and deliberately changed nothing. Lyle approved fixing them, so five of
the six are now gone from the DSP and one turned out not to be a DSP problem at
all. All five fixes are the same move: **reproduce Braids' quantised arithmetic
instead of the mathematically correct value a closed form gives.**

1. **The comb-pitch smoother is Braids' integer recursion.**
   `filtered_pitch = (15 * filtered_pitch + pitch) >> 4` on an int32 in
   1/128-semitone units. `>> 4` floors, so every value in `[p-15, p]` is a
   fixed point: rising from the 0 `Init()` memsets, the filter **stalls 15 LSB
   = 11.72 cents flat** of the requested comb pitch and stays there for the
   life of the note — verified at exactly −15 LSB for every A/B case pitch. The
   port's float pole converged exactly and so ran 11.72 cents **sharp**, its
   delay 0.680% short. `stock` went 1.10 → 0.40 dB and its 5–10 kHz band
   +10.18 → −0.62 dB; `comb-low-open` went 3.57 → 0.45 dB.
2. **The comb pitch has the module's ceiling.** `ComputeDelay` opens with
   `if (midi_pitch >= kHighestNote - kOctave) midi_pitch = kHighestNote -
   kOctave` (`digital_oscillator.cc:73-76`); with `kHighestNote = 140*128` and
   `kOctave = 12*128` that pins the comb at MIDI 128 = 13.29 kHz. The port had
   no equivalent and put its comb at 21.15 kHz at note 84 / TIMBRE 1 — nearly
   an octave up. `comb-clamp-highest` 2.22 → 0.49 dB, `comb-clamp-highest-res`
   4.27 → 0.86 dB.
3. **The feedback warp is `ws_moderate_overdrive`'s own generator.** That table
   is *not* `tanh(2x)/tanh(2)`: its generator samples `tanh(2x)` at 257 points
   over [−1, 1] with the **last two points equal**, mean-centres, and scales to
   ±32766. So the module's maximum feedback is the truncated 32728/32768 =
   0.998779 and its loop **decays**, where a normalised `SoftLimit` reached
   exactly 1.0 and was marginally stable. The Padé form was also up to
   199 LSB — 0.61% of loop gain — off the table mid-curve; the generator
   reproduces it to 4.05 LSB. `res-ringing` 2.42 → 0.24 dB, `corner-low-high`
   4.43 → 0.48 dB.
4. **The output clipper is hard**, as Braids' `CLIP(out)` is. `SoftClip` is
   `SoftLimit` below ±3 and so attenuates everywhere, not only at the rail —
   `SoftLimit(1.0) = 0.778`. That was −1.25 dB of level on the stock case,
   which now reads −0.01 dB.
5. **A trigger does nothing, because it does nothing on the module.** Braids'
   `SAW_COMB` is completely inert under a strike — `Strike()` only sets
   `strike_`, and `RenderComb` never reads it. `Reset()` is now the
   engine-switch reset only (still required there: another engine's buffers
   alias this line), and the smoother is seeded to Braids' own 0 rather than a
   fixed MIDI 48, which at note 84 had forced a 36-semitone comb glide at the
   start of every note. `trigger-transient`'s 20–80 Hz and 320–640 Hz bands
   went from +22…+26 dB against a silent module to −7…−9 dB; the new
   `trigger-low` case, twelve semitones below the old seed so the glide ran the
   other way, went 1.82 → 0.20 dB.

The sixth declared deviation — the block-rate bound `|feedback| * (1 + |tilt|)`
— was correctly diagnosed as wrong and has been **removed** rather than
corrected. `H(z) = (1 - tilt) + tilt*L(z)` is exactly 1 at DC for any tilt and
`1 - 0.788*tilt` at Nyquist, so after the reciprocal HF compensation already in
the code the loop's peak gain is just `|feedback|` — which `WarpResonance` now
caps at 0.998779. The bound was therefore never a backstop, and on the damping
half it was binding: at HARMONICS 1 it cut feedback to 0.625 at MACRO 0,
removing 4.08 dB of resonance range for no stability reason. **This is the one
change on this list a user will hear away from the Braids axes**: the damping
half of MACRO is now considerably more resonant at high HARMONICS.

**A sixth clamp, found by the verification pass the same day.** Braids pins its
own `pitch_` to `[0, kHighestNote]` in `DigitalOscillator::Render`
(`digital_oscillator.cc:120-124`) *before* dispatching to `RenderComb`, so the
note the TIMBRE offset applies to is already clamped at MIDI 0 from below. The
port applied the offset to Plaits' raw note, which reaches −119
(`voice.cc:265`), so under a downward pitch CV its comb ran up to an octave
flat of the module's — at note −12 with TIMBRE 1 the module's comb sits at
MIDI 63.99 and the port's sat at MIDI 51.99. `low-note-clamp` covers it:
0.54 → 0.05 dB, with the 160–320 Hz band going from +6.09 dB to −0.09 dB.
Nothing at or above MIDI 36 moves, so no other case changed.

One code comment was also wrong and is now fixed in the source:
`delay_aux = delay * 1.5f` is a *longer* delay, so the primary aux tap is a
fifth **below**, not above; the `delay / 1.5f` fallback taken when the longer
tap would clamp is the one a fifth above. The DSP was always right — only the
comment's direction was reversed, and per the rule for that case the label was
fixed and the DSP left alone.

### Where it now sits

Twenty-one of the twenty-three cases are inside 1.2 dB spectrum and 0.6 dB
AC RMS, and **every case now carries a pitch tolerance**, including the two
that previously could not — `high-note`, where the estimator used to read a
2399-cent octave-quadrupling artefact, and `comb-clamp-highest-res`. All are
inside 0.5 cents except `low-note-clamp`, whose 5.2 is a documented estimator
artefact rather than a pitch error: below about MIDI 24 there is no resolvable
fundamental, `ab_compare` locks onto the comb period instead, and its lag
resolution there is coarser than the 4.61-cent `kCorrectedSampleRate`
transposition it is correcting for. Two cases remain outside the ~1.5 dB
spectrum target, both explained with numbers in `ab.json`:

- **`res-inverted`, 2.93 dB** (down from 3.76). HARMONICS 0 gives
  `g = -32766/32768`, a Q around 16,000, and TIMBRE noon lays the comb's null
  ladder over the exciter's own harmonics — so what the module outputs there is
  the residual of a **>30 dB cancellation**, and at that null depth a sub-LSB
  difference in the fractional-delay interpolator is worth several dB of it.
  The control is in the same file: `res-ringing` has the same `|g|` to four
  places but positive, so its peaks land *on* the harmonics rather than in the
  nulls, and it reads 0.24 dB.
- **`res-low-detuned`, 1.16 dB.** The same effect an order of magnitude weaker
  (`g = -0.918`, Q ≈ 12); everything from 160 Hz to 5 kHz is inside 0.4 dB.

Tested and rejected as an explanation for the first: tuning the comb delay to
absolute 48 kHz rather than to Plaits' `kCorrectedSampleRate`, which would put
the comb at Braids' exact Hz but 4.61 cents out of tune with the port's *own*
note. It made everything worse — `stock` 0.40 → 1.53 dB, `comb-low-open`
0.45 → 2.86 dB, `res-inverted` 2.93 → 3.76 dB — which confirms the comb's
tuning *relative to the note*, which both sides get exactly right, is what
governs these cases.

### Still declared

- `ComputeDelay`'s LUT is replaced by `1/NoteToFrequency`. Checked against the
  real `lut_oscillator_delays` at every A/B case pitch, the two agree to within
  +0.0006% (0.01 cents).
- At the very bottom of TIMBRE, Braids' `ComputeDelay` shifts by a *negative*
  count and returns 0, which its own indexing then reads as a full-line
  8,192-tap delay. That undefined behaviour is reproduced only in effect: the
  port clamps to its own full line, 4,096 taps, the same 85.33 ms.
- MORPH and MACRO are not on the module at all; only their detent positions
  (MORPH 0, MACRO 0.5) are A/B-able.
