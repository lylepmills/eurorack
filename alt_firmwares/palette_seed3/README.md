# Palette on a Daisy Seed3 — firmware track 1

The Plaits Palette engine catalog running on the STM32H750 (Electrosmith Daisy
Seed3, libDaisy `seed3-updates`), built as Daisy-bootloader apps. Purpose: the
plan's track-1 measurements (`research/plaits/plaits-palette-hardware-plan.md`
§22, §27, §38 in the rubato-audio repo) — does the whole catalog fit as a
`BOOT_QSPI` XIP image, and what does each engine cost per voice on the H7.

## Layout

```
libDaisy/            submodule, electro-smith/libDaisy branch seed3-updates @ e1f740a
                     (Seed3 = PH6 board detection, TAC5242 in hardware-default mode)
CMakeLists.txt       four targets, see below
src/main.cc          bench + play app (48 kHz, 48-frame callbacks, up to 4 voices)
src/fw_voice.h       one engine + registered gain/limiter + LPG per voice, static RAM
src/user_data.cc     the DX7 banks and the default wave terrain for the four engines that need them
tools/gen_engine_table.py   catalog.json -> generated/<set>/{engine_table.h,sources.cmake}
tools/make_lds.py    libDaisy core/*.lds -> ld/palette_{qspi,qspi_notables,sram}.lds
tools/capture_bench.sh, tools/bench_table.py   record the CSV, render the plan table
examples/blink, examples/passthru   libDaisy's Seed3 examples, Makefile flow (goal 1)
flash.sh             dfu-util recipes
```

| target | engines | app type | placement |
|---|---|---|---|
| `va_seed3` | Virtual Analog | BOOT_SRAM | the single-engine bring-up image |
| `palette96_qspi` | all 96 | BOOT_QSPI | engine code XIP from QSPI, all plaits/stmlib tables copied to AXI SRAM, hot shared code in ITCM, voices in DTCM |
| `palette96_qspi_notables` | all 96 | BOOT_QSPI | control: tables stay in QSPI |
| `palette24_sram` | stock 24 | BOOT_SRAM | code + tables in AXI SRAM; the XIP-vs-SRAM comparison |

Each image boots, runs the CPU bench (every engine, 1 voice then 4, DWT cycle
counter around `Engine::Render` and around the whole callback, 256 measured
callbacks per row after 32 warm-up), prints one CSV line per row over USB
serial, then plays `PALETTE_PLAY_ENGINE_ID` (default `virtual-analog`) as a slow
auto-arpeggio on OUT L/R so audio can be checked with a cable.

## Build

Arm GNU Toolchain (15.3.rel1 used; `~/local_deps/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi`
on the dev Mac), CMake ≥ 3.20, Ninja, Python 3. Then:

```sh
git submodule update --init --recursive alt_firmwares/palette_seed3/libDaisy
cd alt_firmwares/palette_seed3
./build.sh                       # all four targets -> build/*.bin (+ .elf, .map)
(cd examples/passthru && make)   # libDaisy Makefile flow, APP_TYPE=BOOT_QSPI by default
```

`build.sh` refuses to build if `generated/` is stale against the catalog
(`tools/gen_engine_table.py --check`); re-run the generator after a catalog change.

## Signed through-zero FM

`PALETTE_EXTENDED_TZFM` defaults ON and compiles the shared engines with
`PLAITS_BUILD_EXTENDED_TZFM=1`: 61 eligible engines rather than Plaits' 29.
The F373 build rejects this flag, and the public Plaits catalog is unchanged.
`FwVoice::linear_tzfm_capable()` reports this target's eligibility; `Render`
accepts an optional 12-frame offset buffer in cycles per 48 kHz sample.

For the same CPU sweep under signed FM:

```sh
./build.sh -DPALETTE_TZFM_BENCH=ON
```

This supplies a 997 Hz sine at 2000 Hz depth only to eligible engines; the serial
header identifies the mode. Set `-DPALETTE_TZFM_BENCH=OFF` for the original sweep.
`-DPALETTE_EXTENDED_TZFM=OFF` restores the original eligibility for comparison.
Cross-compilation and host stability tests are not a board timing qualification:
measure the real FM input path and sustainable voice counts before release.

## Flash (Seed3, on-board USB-C)

1. Once per board: hold BOOT, tap RESET, release → `./flash.sh boot` (Daisy
   bootloader v6.4, DFU on the on-board port, 2 s grace).
2. Tap RESET; while the LED pulses (2.5 s; press BOOT to hold it there) →
   `./flash.sh app build/palette96_qspi.bin`.
3. `tools/capture_bench.sh qspi.csv` — waits for the CDC port, records until
   `# bench done` (~45 s for 96 engines × 2 voice counts).
4. Repeat for `palette24_sram.bin` and `palette96_qspi_notables.bin`, then
   `python3 tools/bench_table.py qspi.csv sram.csv notables.csv` for the plan table.

`examples/*` also build `APP_TYPE=BOOT_NONE` images for the ST ROM DFU
(`./flash.sh internal …`), which is the fastest first-light path on a virgin
board but overwrites the Daisy bootloader.

## Notes

- Engine blocks are 12 frames (the module's `kBlockSize`), four per 48-frame
  callback: several engines change sound with the block length, so the bench
  and the product render at the module's block, not the callback's.
- Engine switching placement-constructs one engine per voice (16 KB arena +
  the largest engine object, 20.8 KB per voice) off the audio interrupt; the
  module's "all engines resident per voice" is not needed on the H7 but the
  §27 RAM figure was computed that way.
- `-fshort-enums` from the fork's F373 flags is deliberately not used:
  libDaisy is built without it and shares structs with our translation units.
- Not yet here: CV/ADC, encoder, screen, SAI2 TDM out, WebUSB. This is the
  measurement image, not the product firmware.
