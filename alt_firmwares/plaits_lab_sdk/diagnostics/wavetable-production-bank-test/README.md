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
