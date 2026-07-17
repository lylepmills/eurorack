# Plaits Lab project checkpoint

This document is the durable handoff for the Plaits alternate-firmware and
custom-firmware-editor work. It describes the state committed on July 17, 2026.

## Repositories

- Firmware: this repository, branch `codex/plaits-experimental-engines`
- Web editor: sibling repository `../plaits-editor`, branch `main`
- Live editor: <https://rubato-plaits-lab.lyle522969.chatgpt.site/>
- Reference build environment: sibling checkout `../mutable-devcontainer`

The unrelated `../crowmancy-firmware` repository is not part of this project.

## Firmware state

The default build is the experimental layout. Its amber bank contains:

1. VA + VCF
2. Phase Distortion
3. Glisson
4. GENDY
5. Scanned
6. Pulsar
7. String Machine
8. Chiptune

`PLAITS_STOCK_ENGINE_LAYOUT` restores the stock amber bank, including all three
factory DX patch banks and Wave Terrain. Both layouts retain the existing
alternate-firmware options.

Implemented in this checkpoint:

- Glisson, GENDY, Scanned, and Pulsar experimental engines
- A fourth macro for every stock synthesis implementation
- A locked-frequency menu option that routes the frequency knob to that macro
- Stock and experimental compile-time layouts
- Built-in factory DX data for stock-layout builds
- Host tests for finite output, control extremes, neutral stock midpoints,
  audible macro response, and distinct/responding DX banks
- An AMD64 Docker/devcontainer toolchain that works on Apple Silicon
- Flash-size recovery by size-optimizing non-audio UI/settings objects

Detailed engine controls are in `alt_firmwares/README.md`.

## Hardware-test status

- The audio firmware update path and containerized build system are proven on a
  physical Plaits.
- Glisson, GENDY, and Scanned were iterated on hardware and accepted as a good
  checkpoint.
- The fourth dimensions for all stock algorithms were iterated on hardware and
  accepted.
- Pulsar passes host and ARM builds but has not received the same focused
  hardware listening pass. Treat that as the first outstanding sound-design QA.
- The latest retained local listening artifacts are the ignored `v9` WAV files
  under `build/plaits/`. They are not source artifacts and can be regenerated.

## Canonical bank ordering

All user-facing products, manifests, and documentation use:

1. Green — tonal voices
2. Red — noise, physical modelling, and drums
3. Amber — digital and alternate models

The original firmware registry stores engines in amber, green, red order. That
is an implementation detail. The build service must explicitly map the web
manifest's green/red/amber slots to the registry order rather than passing its
array through unchanged.

## Build and validation

Initialize submodules after a fresh clone:

```sh
git submodule update --init --recursive
```

Build the container image:

```sh
docker build --platform linux/amd64 \
  -t mutable-eurorack-dev:local \
  -f .devcontainer/Dockerfile .
```

Run the complete host synthesis test:

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  bash -lc 'make -f plaits/test/makefile -j2 && ./plaits_test'
```

Build the default experimental audio updater:

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  bash -lc 'make -f plaits/makefile BUILD_ROOT=build/experimental/ -j2 wav'
```

Build the stock-layout audio updater:

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  bash -lc 'make -f plaits/makefile BUILD_ROOT=build/stock/ \
    PROJECT_CONFIGURATION=-DPLAITS_STOCK_ENGINE_LAYOUT -j2 wav'
```

The WAV files are written to `build/experimental/plaits/plaits.wav` and
`build/stock/plaits/plaits.wav` respectively.

At this checkpoint, clean ARM builds report:

- Experimental layout: 203,088 bytes text, 48 bytes data, 24,064 bytes BSS
- Stock layout: 227,296 bytes text, 48 bytes data, 27,776 bytes BSS

## Web editor state

The editor currently provides a complete engine catalog, three draggable banks,
search/filtering, device-local autosave, recipe import/export, stock and
experimental presets, Mutable Instruments artwork for stock models, and a
versioned manifest. It is deployed, but the firmware build button is
intentionally disabled.

Manifest schema version 2 stores slots in green/red/amber order. The editor
migrates version 1 amber/green/red recipes when they are opened.

The planned backend is documented in `../plaits-editor/docs/build-service.md`.
It should accept only approved engine IDs, translate bank ordering, compile in
an isolated container, reject over-budget builds, cache successful WAVs, and
return audio update files.

## Next milestones

1. Give Pulsar a focused hardware listening pass and tune it if needed.
2. Define the generated engine-registry format that maps manifest slots into
   Plaits' internal registry without allowing arbitrary source or flags.
3. Implement the first build-service endpoint for approved catalog engines.
4. Compare a service-produced binary and WAV with the same local container
   build before enabling the editor's build button.
5. Design the constrained SDK, validation pipeline, and review flow for
   community engines only after approved-engine builds are reliable.

