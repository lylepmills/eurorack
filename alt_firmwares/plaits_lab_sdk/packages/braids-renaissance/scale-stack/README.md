# Scale Stack

A port of the five STACK_* models from Tom Burns'
[Braids Renaissance](https://github.com/boourns/eurorack-renaissance).

Five oscillators starting at the played note and spaced an equal number of
**scale degrees** apart, so one knob sweeps from a unison cluster out through
stacked seconds, thirds, fourths and beyond — and every rung stays in the scale,
which is what makes the sweep musical rather than an interval ramp. Four of the
five display models fold onto MORPH; the wavetable one is dropped for the reason
given in `diatonic-chord`'s README.

## Why it is not `swarm` or `triple`

Those detune in cents around a note. This quantises every voice onto a scale, so
wide spans are chords rather than clusters and the whole stack transposes
diatonically under the keyboard. At span 1 in a whole-tone scale it is an
interval stack no cent detune reaches; at span 2 in major it is a thirteenth
chord that stays a thirteenth chord wherever you play it.

## Controls

HARMONICS is the span, 1–16 scale degrees. MACRO picks the scale from the same
eight `diatonic-chord` uses — and it matters more here, because the span counts
in whatever the scale's degrees are: span 2 is a third in major and a fourth in
pentatonic.

TIMBRE detunes the voices symmetrically against each other and folds them where
MORPH is still near a sine, as in `diatonic-chord`.

## ⚠️ Upstream arithmetic is not reproduced

`RenderStack` **pre-accumulates** its own spans (`acc += span; offsets[i] = acc`)
into `span, 2*span, 3*span, 4*span` — and then `renderChord` accumulates them
**again**, so what actually reaches the DAC is `span, 3*span, 6*span, 10*span`.
At the top of the knob that puts the fourth voice 160 scale degrees up, far past
the audible range, and the model quietly loses voices as the knob rises.

The pre-accumulation is unambiguous about what was meant, and this port does the
intended thing. The same call is shared with `diatonic-chord`, whose README has
the full argument.

Voices that still land out of range at wide spans are muted rather than aliased,
and the mix does **not** compensate — a stack that runs off the top of the range
is supposed to thin out, not swell.

## Implementation notes

Shares `scale_voices.cc` with `diatonic-chord`: the sixteen scales, including
three kept in Braids' 1/128-semitone microtonal tuning; the degree quantiser
that preserves fine pitch as a residual; and the naive-plus-PolyBLEP voice
bank. The full scale list and the remaining notes live in that engine's README.

Braids' `kStackSize` was 6 but `RenderStack` only ever filled five voices; five
is what this ports.

Host CPU is close to `triple`'s -- a smoke signal, not a budget; see
`diatonic-chord`'s README.
