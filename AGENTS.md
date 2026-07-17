# Plaits Lab contributor guide

This fork is primarily being used for the Rubato Audio Plaits Lab project.
Before changing Plaits, read:

- `alt_firmwares/PLAITS_LAB_PROJECT.md` for the current project state,
  validation commands, hardware-test status, and next milestones.
- `alt_firmwares/README.md` for firmware behavior and control mappings.

The related web editor is a separate repository at `../plaits-editor`. Its
production URL is <https://rubato-plaits-lab.lyle522969.chatgpt.site/>.

## Project invariants

- User-facing bank order is always green, red, amber. Plaits' legacy internal
  engine registry is amber, green, red, so the future build service must
  translate between the manifest order and the firmware registry.
- The fourth synthesis macro is neutral at its midpoint for stock models. It is
  controlled by the locked-frequency menu's blinking-green option.
- Keep generated binaries, WAV files, test renders, and build directories out
  of Git. They are reproducible artifacts.
- Preserve the existing alternate-firmware features unless a task explicitly
  changes them.

## Required validation

Use the pinned AMD64 container on Apple Silicon. Build it once with:

```sh
docker build --platform linux/amd64 \
  -t mutable-eurorack-dev:local \
  -f .devcontainer/Dockerfile .
```

Run host tests:

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  bash -lc 'make -f plaits/test/makefile -j2 && ./plaits_test'
```

Build both firmware layouts into separate ignored directories:

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  bash -lc 'make -f plaits/makefile BUILD_ROOT=build/experimental/ -j2 wav'

docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  bash -lc 'make -f plaits/makefile BUILD_ROOT=build/stock/ \
    PROJECT_CONFIGURATION=-DPLAITS_STOCK_ENGINE_LAYOUT -j2 wav'
```

