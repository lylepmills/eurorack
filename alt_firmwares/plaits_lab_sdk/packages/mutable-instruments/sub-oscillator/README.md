# Sub Osc

A port of Braids' two SUB models — SQUARE_SUB and SAW_SUB — merged into one
slot. They differ only in whether the main oscillator is a square or a
variable saw, so HARMONICS turns that into a continuous axis and two Braids
models fit where one used to.

The DSP is Emilie Gillet's `MacroOscillator::RenderSub`. MORPH is Braids'
COLOR verbatim, including its shape, which is worth knowing: the sub level is
a **V**. Fully down gives an equal blend with the sub two octaves below,
**noon gives no sub at all**, and fully up gives an equal blend one octave
below. The sub is loudest at both ends, and Braids never lets it exceed an
equal blend.

MACRO is new — Braids welds the sub to a plain square and this narrows its
pulse. The detent and everything above it are Braids (ApplyMacro's maximum
equals its stock value); turning MACRO down from noon narrows the sub toward
12% pulse width, which Braids cannot do.

AUX carries the sub on its own at full level rather than scaled by the blend,
which would make it silent at the centre of MORPH — the first place anyone
would look for it.

Both copyright lines are carried in `LICENSE` and in each source file.

## Declared deviations

Measured by `tests/ab.json` / `ab_engine.py` — reproducible, not asserted.
10 of the 18 cases fail; all of it is these three, and all three are DSP-level,
reported rather than fixed.

- **The TIMBRE → pulse-width law is wrong across the whole top of the axis.**
  Braids' low fraction is `(32768 - min(p1*32767, 32000)) / 65536` — a slope of
  about 0.5 down to a clamped floor of 0.0117 that it reaches at TIMBRE 0.977.
  This port uses `0.5 - 0.48*timbre`, a 0.48 slope and a 0.02 floor. The two
  agree at the symmetric end and diverge multiplicatively as the pulse narrows,
  so the error grows through the upper quarter of the travel and peaks at the
  clamp knee rather than at the top: **+1.11 dB / 1.51 dB at TIMBRE 0.88,
  +4.44 dB / 5.13 dB at 0.977, +2.56 dB / 2.98 dB at 1.0** (AC RMS / spectrum,
  note 45). Braids' clamp is *not* the cause — at TIMBRE 0.95, below the clamp
  threshold, this port is already +2.53 dB out; the 0.48 coefficient is.
- **That floor also moves with pitch here, and does not in Braids.** The
  oscillator this port uses re-clamps pulse width to `[2f, 1-2f]`; Braids'
  square has no frequency term. From around MIDI 61 upward this port cannot
  reach Braids' narrowest pulse at all, and from around MIDI 71 the frequency,
  not TIMBRE, sets the width. At note 84 with TIMBRE at maximum: **+8.35 dB AC
  RMS / 5.04 dB spectrum**, with f0 estimated at 1045 Hz for Braids against
  262 Hz here. The same note at TIMBRE 0.5 is clean (+0.18 dB / 0.40 dB).
- **SAW_SUB: TIMBRE has no audible effect at any setting.** At HARMONICS=1.0
  the underlying oscillator sits at its pure-saw point, where the pulse-width
  parameter TIMBRE feeds is mathematically inert — Braids' own SAW_SUB is a
  pair of ramps whose spacing tracks it throughout. Confirmed directly: this
  engine's TIMBRE=0.0 and TIMBRE=1.0 renders differ by a mean absolute sample
  value of 5.9e-6, and by at most 1 LSB after the first ~2700 samples, while
  the equivalent Braids renders differ by +6.01 dB and a full octave of
  apparent pitch. It is wrong at *both* ends, not only the top: Braids floors
  its parameter at 1024, so even at TIMBRE 0 it carries a comb null on harmonic
  32 that this port lacks — **+10.94 dB in the 2560–5120 Hz band**, which the
  1.28 dB aggregate spectrum figure very nearly buries.

Two smaller ones, both from the DC blocker this port adds and Braids does not
have:

- Its corner is 7.6 Hz, which is **not** an order of magnitude below the lowest
  sub the engine reaches. The two-octave sub crosses the corner at note 21 and
  clears it by only 3.6× at note 45. At note 24 — an ordinary bass note — the
  sub sits at 8.18 Hz and loses 2.7 dB and 43°: **−1.11 dB AC RMS** against
  Braids.
- `Render()` calls `Reset()` on a trigger rising edge, which zeroes the
  blocker's state. Every triggered note therefore re-arms it from scratch: at
  TIMBRE 1.0 that is a **~0.54 full-scale DC step decaying over ~20 ms on every
  note** — the thump the blocker exists to remove. Not covered by `ab.json`
  (its cases render one sustained note); measured with `--trigger-hz 2`.
