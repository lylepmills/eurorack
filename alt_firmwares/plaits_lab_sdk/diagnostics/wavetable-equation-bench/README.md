# Wavetable Equation Bench

A diagnostic, not a musical model. It answers whether an equation-generated
wavetable bank can be kept as native firmware code instead of occupying one
4 KB Mutable Instruments user-data region.

The legacy format is compact because an 8 × 8 bank is a **map**, not 64 wholly
new waveforms. Its meaningful payload is:

- 64 bytes selecting factory or custom waves for the 8 × 8 cells;
- 15 custom waves, each stored as 132 signed 16-bit integrated samples;
- 72 unused/tag bytes at the end of the 4,096-byte transfer region.

That is `64 + 15 × 132 × 2 = 4,024` meaningful bytes. A hypothetical bank of
64 wholly unique integrated waves would be 16,896 bytes, but that is not the
stock upload contract.

## What this diagnostic compares

Case 0 follows the existing mapped integrated-wave lookup and differentiation
path. Cases 1–16 evaluate the editor's equation directly at the current phase,
column and row. Every native case is evaluated twice per output sample to model
the two equation banks read while HARMONICS crossfades between adjacent banks.

The first six native cases are the editor's starting recipes. Cases 7–14 apply
its transforms to Glass FM. Case 15 stacks three transforms. Case 16 is an
eight-sine stress test.

Native eligibility needs three independent gates:

1. marginal flash must be below 4 KB;
2. physical CPU must remain below the same 75% gate used by Wave Terrain;
3. rendered-audio checks must reject equations whose direct form aliases or
   departs materially from the sampled/integrated result.

Imported wavetables have no equation and always use sampled storage.

## First calibrated results (2026-08-11)

The Cortex-M4 instruction sweep uses the same hardware-calibrated 10–90% band
as the Wave Terrain diagnostic. These are nominal note-48, centred-control
results; the physical probe remains the authority.

| case | instructions/sample | estimated budget |
|---|---:|---:|
| 4 KB sampled bank | 394.2 | 75.9% (65.8–97.5%) |
| Mutable FM | 265.2 | 51.1% (44.3–65.6%) |
| Glass FM | 215.2 | 41.4% (35.9–53.2%) |
| Harmonic grid | 359.7 | 69.2% (60.0–89.0%) |
| Phase warp | 211.2 | 40.7% (35.2–52.2%) |
| Pulse matrix | 282.7 | 54.4% (47.2–69.9%) |
| Odd / even weave | 291.9 | 56.2% (48.7–72.2%) |
| Glass + upper partial | 316.5 | 60.9% (52.8–78.3%) |
| Glass + row motion | 262.8 | 50.6% (43.9–65.0%) |
| Glass folded | 249.2 | 48.0% (41.6–61.6%) |
| Glass FM-wrapped | 257.2 | 49.5% (42.9–63.6%) |
| Glass ring grid | 313.2 | 60.3% (52.3–77.5%) |
| Glass terraced | 275.6 | 53.1% (46.0–68.2%) |
| Glass soft-clipped | 271.2 | 52.2% (45.3–67.1%) |
| Glass hard-clipped | 230.6 | 44.4% (38.5–57.0%) |
| Three-transform stack | 357.1 | 68.7% (59.6–88.3%) |
| Eight-sine stress | 461.2 | 88.8% (77.0–114.1%) |

The important result is not merely that native code is smaller. For five of the
six editor starters it is also materially cheaper at render time than the full
eight-corner sampled-bank read. Harmonic grid and the transform stack need
physical confirmation; eight-sine stress is already a sampled-fallback case.

Two linker-pruned ARM builds pin the flash range:

| fixed case | total flash | difference from sampled |
|---|---:|---:|
| 4 KB sampled bank | 35,492 bytes | — |
| Glass FM native | 31,188 bytes | −4,304 bytes |
| Eight-sine native | 32,196 bytes | −3,296 bytes |

These are standalone diagnostic totals, not yet the marginal cost of adding a
second native bank to a real multi-bank engine. Shared helpers should make that
marginal cost smaller; the production linker matrix must measure it directly.

`analyze_fidelity.py` also compares each direct equation with the legacy 15-wave
map approximation over a 17 × 17 control grid. The legacy representation can be
very coarse: relative cycle RMSE ranges from 6.0% for Odd / even weave to 87.4%
for the three-transform stack. That divergence usually favors native code—it is
the equation the user actually wrote—but folded, FM-wrapped and stacked cases
also carry substantial energy above the C6 Nyquist limit. Those need a rendered
audio/listening gate even when their CPU and flash costs pass.

## Pre-flight

```sh
python3 alt_firmwares/plaits_lab_sdk/qemu/estimate.py \
  alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-equation-bench --sweep

python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-equation-bench/build_matrix.py \
  --output-dir /tmp/wavetable-equation-matrix
```

The QEMU estimate is a screen, not the final answer. The Wave Terrain work found
that calibrated instruction bands must still be checked on physical hardware.

## Autonomous hardware probe

```sh
python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-equation-bench/build_autosweep.py \
  --output /tmp/wavetable-equation-autosweep.wav
```

The probe ignores panel and CV controls, measures all 17 cases at note 48 and
centred TIMBRE/MORPH/MACRO, and repeats every 105.5 seconds. Record at least
211 seconds from AUX to guarantee one complete pass.

Decode a mono 16-bit PCM capture with:

```sh
python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-equation-bench/decode_autosweep.py \
  wavetable-autosweep-capture.wav
```
