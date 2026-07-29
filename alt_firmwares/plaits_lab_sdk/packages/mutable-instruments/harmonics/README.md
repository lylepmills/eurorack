# Harmonics

A port of Braids' HARMONICS — twelve integer partials of the played note, with
two Lorentzian bumps sliding over the harmonic series to shape them.

The DSP is Emilie Gillet's `DigitalOscillator::RenderHarmonics`
(`braids/digital_oscillator.cc:922-987`).

## The controls

`kNumAdditiveHarmonics` is 12 (`digital_oscillator.h:53`) — a fixed count, not
a parameter. Two bumps are drawn over those twelve positions, summed, and
normalized so the twelve amplitudes total full scale. That pins the *peak*, not
the loudness: one partial and a twelve-partial stack at the same peak have very
different crest factors, and the reference renders bear it out — AC RMS runs
−3.33 dB at COLOR 0 against −13.70 dB at COLOR 0.5 for the same TIMBRE.

TIMBRE (`:932`) sets the first bump's index, from the fundamental to the
twelfth partial. COLOR does two things at once: it squares into the second
bump's height (`:934`) and it sets the width of both bumps (`:936-939`) — and
the width is a *triangle* in the knob, running 4 → 3973 → 4. So COLOR drives
the two in opposite directions across its upper half, and "wide with a strong
second bump" and "narrow with a weak one" are both off the module's map.

The second bump's position is welded too: `second_peak = (peak >> 1) + 12 * 128`
(`:933`) puts it six partials above half the first one's index, always.

Harmonics keeps those two axes as Peak and Colour, and spends its two extra
macros on exactly the welds: **Spread** moves the second bump off the +6
relation by up to six partials either way, and **Width** scales the bump width
around whatever Colour chose. Both are neutral at noon, so the module is a
point on the plane rather than a corner of it.

## Why the amplitudes are integers in this port

`amplitude[i] += (target_amplitude[i] - amplitude[i]) >> 8` (`:974`) is not a
smoother, it is a gate. An arithmetic shift floors, so a positive difference
under 256 becomes zero and the partial stops climbing — and a partial whose
target is under 256 out of 32768 (−42 dB) never leaves zero at all.
`DigitalOscillator::Init()` memsets the amplitude state
(`digital_oscillator.h:246-258`) and `Render()` calls it on every shape change
(`:111-115`), so zero is where Braids actually starts; the gate is what the
module does on every note, not a start-up transient.

Measured over the whole TIMBRE × COLOR plane at 0.05 steps (441 settings, low
enough in pitch that the pitch guard is not involved): **65% of settings render
fewer than twelve partials**, the audible count runs 1 to 12 with a median of
9, and the settled sum lands 0.17 to 0.85 dB under the normalized full scale
(mean 0.72 dB). Evaluating that slew in float would put every one of those
partials back, so the port runs the whole target computation — the truncated
width, the floored attenuation, the gate — in the same integers Braids uses.

The width truncation is the same lesson from the other side: at COLOR 0.25 the
integer chain lands on `width = 229` where the float closed form gives 259.9, a
12% difference in the skirt.

## Rate

`RenderHarmonics` ends in `size -= 2` (`:980`) and writes two output samples per
`phase += phase_increment` (`:967`), so the inner algorithm runs at **48 kHz**
and every constant transfers verbatim. There is no internal oversampling — the
`<< 1` at `:928` is what makes the inner rate half of Braids' 96 kHz output.
The port writes 48 kHz directly, and the pitch-dependent partial guard at
`:956` needs no re-derivation because it is stated against the rate the port
already runs at.

That guard is also the only anti-aliasing the model needs, and no filter is
fitted. Every partial is explicit and the guard silences each one before it
reaches fs/4; the only content beyond the twelve is the sine table's
interpolation error. Measured as non-harmonic energy against total, on a
16384-point Blackman–Harris FFT excluding ±6 bins around every harmonic, the
port reads −85.5 / −87.5 / −87.1 dB on the three note-45 A/B renders against
the module's −79.1 / −88.5 / −80.7 dB — at or below the module everywhere
measured.

The twelve integer harmonics are generated from one cosine-table read per
sample with the standard harmonic recurrence, restarted each sample. This
reduced the calibrated hardware estimate from 113% to 74% of the CPU budget;
all seven reference A/B cases remain within their declared tolerances.

## What the A/B measures

Six of the seven cases in `tests/ab.json` — both ends of TIMBRE, three points
of COLOR, and both knobs at noon — land at 0.00–0.01 dB AC RMS, 0.0–0.2 cents,
and 0.03–0.15 dB energy-weighted spectrum.

The seventh is a high note (93) where the guard strips partials 7–12. It reads
+0.81 dB AC RMS and 0.31 dB spectrum, and that is a single declared deviation
showing up rather than an unexplained residue: Braids' `(out + previous) >> 1`
at `:977` is a 2× linear interpolator up to 96 kHz, which is a gentle lowpass
on the 48 kHz stream with magnitude `cos²(πf/96000)`. Summing this engine's
settled amplitudes through that curve predicts 0.81 dB; the measurement is
0.81 dB. Per partial it predicts 1.02 dB of tilt between h1 and h6 and the
renders show 1.03 dB. The port does not apply it, so it is very slightly
brighter than the module at the top of the range.

That case declares **no cents tolerance**, deliberately. Once the guard has
stripped the stack there is no material `ab_compare`'s 0.25 s autocorrelation
can pitch — six partials with the fundamental 15 dB below the loudest of them.
It reports 440.1 Hz for Braids and 352.9 Hz for the port, both
sub-multiples of the true 1760 Hz, and it fails the same way at note 84 and
note 78. A Goertzel probe at each render's own f0 shows both carry exactly
partials 1–6 with 7 and above below −100 dB, so the pitch and the guard agree
and the estimator does not. The spectrum tolerance covers the case instead.

The parameter mapping is proved, not read. `RenderHarmonics` uses no
`INTERPOLATE_PARAMETER` macro, so fold's index-versus-ordinal trap has no
surface here — but re-running the suite with the engine's TIMBRE and HARMONICS
exchanged takes `peak-low` to 58.10 dB of spectral difference and +9.83 dB of
AC RMS, and `peak-high` to 25.62 dB and +5.13 dB, so the harness would catch a
swap regardless.

## Against Plaits' harmonic engine

`AdditiveEngine` is the descendant. It runs 24 integer partials
(`additive_engine.h:45`) against Braids' 12; its bump is a rectified triangle
raised to the eighth power (`additive_engine.cc:81-91`), so it has compact
support and is exactly zero past `1/slope` partials, where Braids' Lorentzian
skirt falls as `1/d²`; its slew is a float `ONE_POLE` (`:103`) with no gate;
its ripple control can place many peaks across the series (`:141`) where Braids
has exactly two at a fixed relation; and its MACRO is an odd/even balance
(`:149-154`).

Rendered side by side at a pairing stated in full — note 45, 3 s, both at
TIMBRE 0.5 and MACRO 0.5, each with its bump at its widest (Harmonics at Colour
0.5, the neighbour at MORPH 0.0 and HARMONICS 0.0 so its ripple is off), then
level-matched — the two differ by 4.65 dB energy-weighted. The difference is
not in the formant: the 640–1280 Hz octave carrying 70% of the energy differs
by 3.9 dB, while the bands above the twelfth partial (12 × 110 Hz = 1.3 kHz)
differ by 24.1 dB and 59.9 dB, which is the neighbour's partials 13–24
continuing where this engine stops. So on that measurement the partial count
dominates and the skirt-shape difference does not separate out; the skirt claim
rests on the two formulas, not on that number. It is reported as a difference
between two engines, not as a fidelity figure for either.

## Outputs

MAIN is the model. Mono AUX is the same stack with Peak reflected
(`1 - TIMBRE`), so the two hold opposite ends of the series and coincide when
Peak is centred. In stereo, MAIN/AUX become L/R and take small opposing Peak
offsets instead of the full reflection.

Both copyright lines are carried in `LICENSE` and in each source file; the
declared deviations are listed in the header comment of
`plaits/dsp/engine2/harmonics_engine.h`, and `tests/ab.json` holds the measured
comparison against the module.
