# Plaits Lab rounds 1 and 2 audition build

This build puts all eleven new model prototypes on one Plaits so they can be
compared on hardware. It is an audition image, not a proposed permanent bank
layout. The normal pitch, HARMONICS, TIMBRE, MORPH, TRIG, OUT, and AUX behavior
is retained.

The fourth control is the locked-frequency knob when the first menu item is
set to blinking green. It rests at its midpoint when another locked-frequency
option is selected.

## Firmware and resource report

- Audio updater: `build/plaits-lab-audition/plaits/plaits.wav`
- Recipe: `alt_firmwares/plaits_lab_builder/audition_recipe.json`
- ARM text/data: 148,832 / 48 bytes
- ARM BSS: 22,416 bytes
- Updater SHA-256:
  `a2fc008066f4fb76ecbf35357886bb5e35143f67c4503d02fe025e6b233180ac`

Install the WAV with the normal Plaits audio-update procedure. This image has
passed host and ARM builds, finite-output corner sweeps, and four-control
response checks, but it has not yet had a complete hardware listening pass.

## Bank order

The panel order is green, red, then amber. Amber keeps familiar models as
references while green and the first three red slots contain the new work.

| Slot | Green | Red | Amber |
| ---: | --- | --- | --- |
| 1 | Loopback | Tapfield | VA + VCF |
| 2 | Lockstep | Attractor | Phase Distortion |
| 3 | Phase Weave | Rulefield | Virtual Analog |
| 4 | Sideband Bank | Glisson | Waveshaping |
| 5 | Undertow | GENDY | Two-op FM |
| 6 | Reed Pipe | Scanned | Wavetable |
| 7 | Phase Flock | Pulsar | String Machine |
| 8 | Spectral Spiral | Particle Noise | Chiptune |

## New model controls

| Model | HARMONICS | TIMBRE | MORPH | Fourth | OUT / AUX | TRIG |
| --- | --- | --- | --- | --- | --- | --- |
| Loopback | Feedback ratio | Recursion depth | Delay topology | Negative through bipolar to positive symmetry | Feedback-AM voice / isolated sidebands | Clears and rephases the loop |
| Lockstep | Stepped lock ratio | Loop bandwidth | Sine through wrapped phase detector | Capture range and damping | PLL follower / acquisition and phase error | Knocks the follower out of lock |
| Tapfield | 9–24-bit register topology | Equal/binary weighting and Gray decode | Deterministic bit corruption | Output slew | Decoded bit field / raw carry stream | Restores a repeatable seed |
| Phase Weave | Asymmetric constellation and even/odd balance | Phase spread | Positive through bipolar cancellation | Relative rotation and drive | Cancellation field / rejected orthogonal field | Rephases the bank |
| Sideband Bank | Partial spacing | Geometric rolloff | Finite partial count | Upper/lower spectral bias | Upper bank / lower bank | Rephases both banks |
| Attractor | Attractor lobe density | Damped through chaotic motion | X/Y/Z coordinate scan | Coordinate-rate skew | Selected coordinate / lagging coordinate | Deterministically reseeds the flow |
| Undertow | Five interpolated registrations | Triangle through BLEP saw to pulse | Anchor/undertone balance | Contracts or expands the deep divider lattice | Registered mixture / anchorless alternating lattice | Phase-aligns all dividers |
| Reed Pipe | Bore pickup position and amount | Breath pressure | Reed stiffness and breath noise | Reflection loss and brightness | Bore pressure / reed flow | Adds a pressure tongue |
| Phase Flock | Natural-frequency spread | Mean-field coupling | One- through two-cluster attraction | Phase lag and frustration | First circular moment / second circular moment | Scatters the oscillator phases |
| Rulefield | Elementary CA rule | Cells through spatial edges | Generation rate | Stepped through linear to smooth lookup | Cell/edge wavecycle / generation activity | Restores the rule seed |
| Spectral Spiral | Source richness | Frequency-shift spacing | Complex-loop feedback | Down, stationary, or upward shift | Forward spiral / counter-rotated quadrature | Clears the feedback loop |

## Listening renders

The host renders are under `build/plaits-lab-auditions/`. Each file is 16
seconds at C3. MAIN is the left channel and AUX is the right channel. The four
segments sweep one control while the other three remain at noon:

1. 0–4 seconds: HARMONICS
2. 4–8 seconds: TIMBRE
3. 8–12 seconds: MORPH
4. 12–16 seconds: fourth control

The model receives a trigger at the start of each segment. The files are named
`01-loopback.wav` through `11-spectral-spiral.wav`.

## Deliberate edges to judge by ear

- Lockstep ratios, Tapfield topologies, Sideband partial counts, and Rulefield
  rules are intentionally stepped.
- Attractor pitch follows its integration rate rather than behaving as a
  conventional phase oscillator, especially at the top of the keyboard.
- Phase Flock can cancel strongly at OUT in a two-cluster state while AUX stays
  loud; that is the intended two-order-parameter behavior.
- Rulefield's stepped lookup can alias at high notes, while Sideband Bank
  deliberately retires out-of-band partials.
- Reed Pipe articulation, loudness, and extreme feedback are the highest-value
  hardware checks. Undertow's deepest dividers can become infrasonic at very
  low notes.

