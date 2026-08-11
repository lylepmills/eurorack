# Terrain Equation Bench

A diagnostic, not a musical model. It answers the question behind the Wave
Terrain editor's native-versus-prebaked decision: which formulas are cheaper
than a 4 KB sampled grid and still leave enough real-time CPU headroom on a
72 MHz Plaits?

Mutable's first five stock terrains are live C++ functions. The last three read
integrated wavetable data. The custom editor currently turns every equation into
a 64 × 64 × 8-bit grid. This engine deliberately compares both approaches with
the production scan topology: two path oversamples and two terrain evaluations
per output sample. Firmware-incompatible `expf`/`logf`/`powf` calls are not
smuggled into the comparison: those pull in bare-metal `__errno` and do not link.
The diagnostic targets the same bounded polynomial, LUT, reciprocal-square-root
and fast-angle primitives already used by shipping Plaits engines.

## Cases

HARMONICS selects one of 19 equal-width regions. The percentage below is the
centre of the region and is the exact value used by the QEMU sweep.

| # | HARMONICS | case | main operations |
|---:|---:|---|---|
| 1 | 2.6% | Original terrain 1 | two fast sine lookups, divide, abs |
| 2 | 7.9% | 4 KB sampled grid | four-point bilinear lookup |
| 3 | 13.2% | Soft rings | sqrt, fast sine |
| 4 | 18.4% | Lone island | sqrt, max |
| 5 | 23.7% | Tilted terraces | round/floor |
| 6 | 28.9% | River bend | abs, polynomial |
| 7 | 34.2% | Rippled saddle | sqrt, fast sine, polynomial |
| 8 | 39.5% | Four chambers | sqrt, sign |
| 9 | 44.7% | Spiral current | sqrt, atan2, fast sine |
| 10 | 50.0% | Twin pulses | two exp, two divides |
| 11 | 55.3% | Log crater | sqrt, log, fast sine |
| 12 | 60.5% | Pinched diamond | two fractional pow |
| 13 | 65.8% | Saturated saddle | atan |
| 14 | 71.1% | Warped fault | tan, fast sine |
| 15 | 76.3% | Four-sine stress | four fast sine/cosine lookups |
| 16 | 81.6% | Eight-sine stress | eight fast sine/cosine lookups |
| 17 | 86.8% | Terraces + crater | floor, sqrt, log, fast sine |
| 18 | 92.1% | Spiral + pulses | sqrt, atan2, fast sine, two exp |
| 19 | 97.4% | theta/mu field | atan2, abs, divide, fast sine |

## Calibrated pre-flight results (2026-08-10)

These are Cortex-M4 instruction counts passed through the model calibrated
against 16 physical-module measurements. The percentage is a midpoint followed
by the empirical 10–90% band. It is not a substitute for the probe firmware.

| case | instructions/sample | estimated budget |
|---|---:|---:|
| Original terrain 1 | 227.9 | 44% (38–56%) |
| 4 KB sampled grid | 217.9 | 42% (36–54%) |
| Soft rings | 243.9 | 47% (41–60%) |
| Lone island | 219.2 | 42% (37–54%) |
| Tilted terraces | 207.6 | 40% (35–51%) |
| River bend | 147.9 | 28% (25–37%) |
| Rippled saddle | 253.9 | 49% (42–63%) |
| Four chambers | 224.0 | 43% (37–55%) |
| Spiral current | 303.9 | 59% (51–75%) |
| Twin pulses | 253.9 | 49% (42–63%) |
| Log crater | 285.9 | 55% (48–71%) |
| Pinched diamond | 295.9 | 57% (49–73%) |
| Saturated saddle | 195.9 | 38% (33–48%) |
| Warped fault | 253.9 | 49% (42–63%) |
| Four-sine stress | 303.9 | 59% (51–75%) |
| Eight-sine stress | 477.9 | 92% (80–118%) |
| Terraces + crater | 364.6 | 70% (61–90%) |
| Spiral + pulses | 423.7 | 82% (71–105%) |
| theta/mu field | 241.6 | 47% (40–60%) |

The result is not “transcendentals always fail.” With firmware-native
approximations, every individual editor recipe is a plausible native candidate.
The eight-sine stress case is already too close to the deadline, while the
combined cases show that AST depth still matters: Terraces + crater reaches the
90% caution line and Spiral + pulses crosses 100% at the top of the estimate
band. Physical readings are still required before setting the production
threshold.

The flash control is already definitive: paired fixed-case ARM firmware builds
used 31,908 bytes for Original terrain 1 and 35,924 bytes for the otherwise
identical sampled-grid case. The grid costs 4,016 additional bytes. Mutable's
existing stock Wave Terrain hardware calibration is 990 cycles/sample (66% of
the budget), which is consistent with this model's band and anchors the probe.

## Estimate first

The calibrated Cortex-M4 sweep includes every case because
`qemu/estimate.py` registers this diagnostic's HARMONICS transition points:

```sh
python3 alt_firmwares/plaits_lab_sdk/qemu/estimate.py \
  alt_firmwares/plaits_lab_sdk/diagnostics/terrain-equation-bench --sweep
```

The estimate is a pre-flight screen, not the answer. Native eligibility should
use the worse of the calibrated band and the physical probe.

## Multiplexed hardware probe

Build one firmware that selects all cases with HARMONICS:

```sh
python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py build \
  alt_firmwares/plaits_lab_sdk/diagnostics/terrain-equation-bench \
  --hardware --cpu-probe-aux --output /tmp/terrain-equation-bench.wav
```

MAIN remains the terrain audio. AUX's probe tone reports CPU usage precisely;
the LED meter remains the overload-safe reading. Set TIMBRE, MORPH and MACRO to
noon, note to 48, then visit the HARMONICS centres above. Repeat the worst cases
at path and pitch extremes.

## Per-case flash and firmware matrix

`build_matrix.py` makes fixed-case probe firmwares. In a fixed build the
compiler can remove the other formulas and their math-library dependencies, so
the printed flash size is meaningful for the native-versus-4 KB comparison.
Generated WAVs remain outside Git.

```sh
python3 alt_firmwares/plaits_lab_sdk/diagnostics/terrain-equation-bench/build_matrix.py \
  --output-dir /tmp/terrain-equation-firmwares
```

Use `--case 0 --case 8` to build only selected zero-based cases, and
`--led-only` to preserve the engine's AUX output.

## Decision gate

A formula is a native candidate only when all of these hold:

1. Its fixed-case firmware is smaller than the sampled-grid control by enough
   to justify another code path.
2. Its worst physical reading stays below 75% of the 1,500-cycle/sample budget.
3. No calibrated estimate band crosses 90%, and no parameter corner produces a
   discontinuous timing spike.
4. The generated C++ uses only the reviewed expression compiler's bounded
   operators and cannot produce a non-finite value over x/y = −1…1.

Anything else keeps the current 4 KB grid. The thresholds are deliberately
conservative because the probe brackets synthesis, while UI/ADC work still has
to fit in the same interrupt.
