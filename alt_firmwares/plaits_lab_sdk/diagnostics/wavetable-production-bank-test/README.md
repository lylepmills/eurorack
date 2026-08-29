# Production wavetable bank hardware gate

This gate builds the normal recipe-generated `WavetableEngine` with one
test-only parameter sequencer compiled into that engine. It therefore covers
the generated resource, factory/sample/native dispatch, bank crossfades,
mirrored and one-way HARMONICS transport, TIMBRE, MORPH, MACRO, Voice, and the
hardware DAC path. The older transport diagnostic remains useful for isolated
CPU/path experiments, but it does not substitute for this integration gate.

Build both audio-installable images from the firmware repository root:

```sh
python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-production-bank-test/build_autonomous.py \
  --output-dir /tmp/wavetable-production-test
```

Each image repeats a 50-second cycle. Capture AUX directly through Core Audio
as mono 16-bit PCM at 48 kHz for at least 100 seconds, then run:

```sh
python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-production-bank-test/decode_capture.py \
  --mode mirrored mirrored-capture.wav
python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-production-bank-test/decode_capture.py \
  --mode one-way one-way-capture.wav
```

The mirrored image uses the full eight-entry limit and mixes all three Mutable
banks with sampled and native equation banks. The one-way image uses the full
16-entry limit, alternates sampled/native representations, and deliberately
contains no factory entry; its linker map must not retain the legacy Mutable
wave pool.

## Hardware validation

Both images passed on physical Plaits hardware on 2026-08-28, captured from AUX
through ES-8 input 2 at 48 kHz. The all-slot Wavetable fixtures were used so a
model slot saved in the module's settings could not bypass the gate.

- Mirrored mixed 8: all four profiles traversed 1.0--2.3 kHz of crossing-rate
  range without clipping. Start/end harmonic-spectrum similarity was
  0.866--0.999 and crossing-rate drift was 4.5--8.4%, confirming the return
  path at four notes and four TIMBRE/MORPH/MACRO combinations.
- One-way custom 16: all four profiles traversed 1.4--4.4 kHz without clipping.
  The neutral scan's direction correlation was +0.994; its start/end spectrum
  similarity was 0.105 with 80.6% crossing-rate separation, confirming that
  all 16 banks are traversed rather than folded back.
- The one-way link map discarded the 50,688-byte `wav_integrated_waves` pool.

The tested application SHA-256 values were
`a41e3452b44aac7c8255661cf250328dc9b7e3d650e082644a3757abe451100f`
(mirrored) and
`66b164a31f849499d615731eec51b81ba586b8610652959e5b21842eecf3206d`
(one-way).

## Production flash matrix

`build_flash_matrix.py` performs ordinary production links with the autonomous
sequencer disabled. It measures the legacy stock engine, explicit schema-25
factory banks, sampled-only, native-only, and mixed banks:

```sh
python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-production-bank-test/build_flash_matrix.py \
  --output-dir /tmp/wavetable-flash-matrix
```

The 2026-08-29 ARM 4.8.3 matrix measured:

| Recipe | Application bytes | Marginal bank cost |
| --- | ---: | ---: |
| Legacy stock | 81,376 | baseline |
| Explicit factory 3 | 84,512 | +3,136 |
| Sampled 1 / 2 / 8 / 16 | 40,992 / 49,200 / 98,400 / 164,016 | +10,304 / +18,512 / +67,712 / +133,328 over the 30,688-byte pool-free baseline |
| Native 1 / 2 / 3 / 8 / 16 | 33,248 / 33,520 / 33,920 / 35,840 / 39,104 | +2,560 / +2,832 / +3,232 / +5,152 / +8,416 over the pool-free baseline |
| Mixed 8 (3 factory, 3 native, 2 sampled) | 102,032 | +20,656 over legacy stock |

The fifteen calibrated native equation functions linked at 256--468 bytes
each (284, 260, 388, 256, 352, 456, 404, 412, 352, 348, 360, 304, 296, 320,
468). The remaining delta is shared bank dispatch, entry arrays, alignment, and
the math helper linked by `atan`. These links are the source for the website's
per-equation figures and its rounded-up shared-runtime terms; sampled payloads
remain exactly 8,192 bytes per bank.
