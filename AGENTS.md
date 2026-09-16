# Plaits Lab contributor guide

This fork contains the firmware and model tooling behind Rubato Audio's
Plaits Palette.
Before changing Plaits, read:

- `alt_firmwares/plaits_lab_builder/README.md` for firmware-builder
  architecture and validation.
- `alt_firmwares/plaits_lab_sdk/README.md` for model development and
  contributor tooling.
- `alt_firmwares/README.md` for legacy alternate-firmware information.
- `alt_firmwares/PLAITS_PALETTE_BACKLOG.md` for investigated, parked, and
  planned Palette feature work.

The production editor is at <https://rubato.audio/plaits-palette/>.

## Project invariants

- User-facing bank order is always green, red, amber, then optional orange.
  Plaits' legacy three-bank engine registry is amber, green, red; four-bank
  builds rotate orange, green, red, amber so the cyclic hardware order from
  green remains green, red, amber, orange. The build service must translate
  between the manifest order and the firmware registry.
- The fourth synthesis macro is neutral at its midpoint for stock models. It is
  controlled by the locked-frequency menu's blinking-green option.
- Keep generated binaries, WAV files, test renders, and build directories out
  of Git. They are reproducible artifacts.
- Preserve the existing alternate-firmware features unless a task explicitly
  changes them.
- Before adding any engine to the approved catalog, explicitly review it for
  Fast FM, linear TZFM, and native hard sync. A deliberate semantic/CPU
  rejection or the shared hard-sync fallback is valid, but the engine must be
  present in `engineCapabilityReview.reviewed`; catalog validation enforces
  exact coverage.

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
    PROJECT_CONFIGURATION=-DPLAITS_STOCK_ENGINE_LAYOUT \
    PLAITS_STEREO_ALL=0 -j2 wav'
```

`PLAITS_STEREO_ALL=0` on the stock layout is required, not optional, and it is
not a workaround for a regression. Per-engine stereo defaults to ON
(`plaits/dsp/engine/stereo_config.h`), so a bare `make` compiles the stereo
render path of all 24 engines — about 23 KB that only a stereo recipe can
reach. The stock palette (three DX7 banks, Wave Terrain, Speech) is the
flash-tightest one the builder offers and does not have that to spare, so a
bare stock build fails to link with `region FLASH overflowed by ~23 KB`. That
is a real budget result, not a broken tree: the hosted builder reaches the same
verdict for an all-stereo stock recipe and reports it to the user as
`flash_budget_exceeded` ("Remove an engine, disable per-engine stereo, …").
For a mono recipe the builder passes `PLAITS_STEREO_<X>=0` for every engine
(`_stereo_disable_flags` in `alt_firmwares/plaits_lab_builder/container_server.py`),
and `PLAITS_STEREO_ALL=0` is the local shorthand that emits the identical
per-object flags. Measured at `611657d`: stock links at 228,484 of 229,376
bytes (892 spare) and passes `validate_local_build.py`; experimental links
all-stereo at 227,108 (2,268 spare).

So the two commands cover different ground on purpose — experimental proves a
change still links with every stereo path compiled in, stock proves it links in
the legacy engine registry against a nearly full flash. Both must pass. If the
stock build overflows by roughly 23 KB, check for a missing `PLAITS_STEREO_ALL=0`
before looking for a size regression; if it overflows by a few hundred bytes,
the change really did cost more flash than the stock palette had left.
