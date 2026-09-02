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

## Shared-wave Chords route

Schema 28 also routes an editor-defined 15-wave line into the production
Chords engine. Build its autonomous, audio-installable gate with:

```sh
python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-production-bank-test/build_shared_chords.py \
  --output-dir /tmp/shared-wave-chords-test
```

Every physical slot contains Chords, so a model selection saved in hardware
cannot bypass the test. The four sounding windows use different roots, chord
positions, and inversions while MORPH sweeps a deliberately ordered line of
harmonics 1 through 15. Capture MAIN for at least 100 seconds at 48 kHz as mono
16-bit PCM. Its timing is compatible with `decode_capture.py --mode one-way`.

This image passed on physical Plaits hardware on 2026-09-02 with TRIG
unpatched. We captured MAIN through ES-8 input 2. Its four windows were
unclipped (0.439--0.585 peak), traversed 281--10,231 Hz of crossing-rate range,
and the first three ordered scans had
direction correlations of +0.977, +0.998, and +0.999. The fourth, highest-note
profile entered the expected aliasing/rolloff region but still traversed
7,091 Hz. Corresponding contours across two complete cycles correlated at
0.969--1.000. The tested application SHA-256 was
`61169d2d98b3d4cb6cf1c81d832383a05a717eb64304d801ce19a1e297098cdc`.

The companion Wave Paraphonic gate exercises the 33-wave Braids line through
the production four-voice renderer. It uses 33 ordered harmonic frames spread
across the full 16-entry library and sweeps TIMBRE through the line:

```sh
python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-production-bank-test/build_shared_wave_paraphonic.py \
  --output-dir /tmp/shared-wave-paraphonic-test
```

As with the Chords image, leave TRIG unpatched and capture MAIN. The same
50-second timing and `decode_capture.py --mode one-way` decoder apply.

The Wave Paraphonic image passed on physical Plaits hardware on 2026-09-02.
AUX was captured through ES-8 input 1 and MAIN through input 2 in one
synchronized pass. All four profiles traversed the 33-wave line without
clipping: AUX peaked at 0.306 with direction correlation +1.000 throughout;
MAIN peaked at 0.475--0.583 with direction correlation +0.921--1.000. Across
two complete cycles, corresponding contours correlated at 0.9995--0.9998 on
AUX and 0.9837--0.9994 on MAIN. The tested application SHA-256 was
`84cf8d8fe975402385d046d388ae8c17b4705c820d3c5b6b9f492d2c319db080`.

## Production flash matrix

`build_flash_matrix.py` performs ordinary production links with the autonomous
sequencer disabled. Alongside the legacy schema-26 cases it measures schema
28's shared library with Wavetable, Chords, and the Braids-derived wave line:

```sh
python3 alt_firmwares/plaits_lab_sdk/diagnostics/wavetable-production-bank-test/build_flash_matrix.py \
  --output-dir /tmp/wavetable-flash-matrix
```

The schema-28 ARM 4.8.3 matrix measured on September 1, 2026:

| Consumer / resource | Application bytes | Factory banks retained |
| --- | ---: | ---: |
| Wavetable · shared factory 3 | 83,856 | 1 + 2 + 3 |
| Wavetable · shared factory 2 | 67,008 | 1 + 2 |
| Wavetable · shared factory 1 | 49,936 | 1 |
| Wavetable · shared sampled 1 | 40,064 | none |
| Wavetable · legacy factory 3 | 83,984 | 1 + 2 + 3 |
| Wavetable · legacy sampled 1 | 40,096 | none |
| Chords · stock route | 66,960 | 1 + 3 |
| Chords · shared factory 3 | 83,872 | 1 + 2 + 3 |
| Chords · shared factory 1 | 50,080 | 1 |
| Chords · shared sampled 1 | 37,136 | none |
| Braids line · stock route | 79,248 | 1 + 2 + 3 |
| Braids line · shared factory 3 | 79,168 | 1 + 2 + 3 |
| Braids line · shared sampled 1 | 37,184 | none |

Each split factory section is exactly 16,896 bytes. Observed removal deltas are
16,848--17,072 bytes because the linker also changes the small pointer/switch
path around the removed section. A custom-only Chords route stores its 15
selected integrated cycles (3,960 bytes), not an 8 KB bank; a custom-only
Braids route stores 33 cycles (8,712 bytes). That distinction drives the
editor's content-aware estimate. Full sampled banks remain exactly 8,192 bytes
when the Wavetable engine itself consumes them, while a Chords/Braids-only
palette pays only for the selected 264-byte cycles plus compact route metadata.
