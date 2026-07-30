# Plaits Lab engine SDK v1

Status: v0 implemented

## Decision

Every Plaits model is represented by a versioned engine package. Mutable
Instruments, Rubato, and community packages share the same manifest and test
scenario format. Built-in reference packages may point at canonical source in
this repository; submitted community packages must contain all of their source
and license files within the package directory.

The SDK is source based. Plaits statically links C++ for an STM32F373, so it
does not have a useful binary plugin ABI. The v1 contract is a constrained
subset of the existing `plaits::Engine` API:

- `Init(stmlib::BufferAllocator*)`
- `Reset()`
- `LoadUserData(const uint8_t*)`, which must be a no-op in community v1
- `Render(const EngineParameters&, float*, float*, size_t, bool*)`

The inputs are note, trigger state, accent, Harmonics, Timbre, Morph, and the
Plaits Lab Macro control. Models render MAIN and AUX in 12-sample hardware
blocks. The host preview uses the same source and parameter contract.

## Package

An engine package contains:

```text
my-engine/
├── plaits-engine.json
├── LICENSE
├── README.md
├── src/
│   ├── my_engine.h
│   └── my_engine.cc
└── tests/
    └── scenarios.json
```

`plaits-engine.json` is validated by the CLI and documented by
`engine-package.schema.json`. Package IDs are namespaced and versions are
immutable. The public catalog assigns a digest derived from normalized metadata
and implementation source. Recipe schema v3 pins engine ID, package ID,
semantic version, and digest.

The four control declarations document hardware behavior; they do not create
additional runtime parameters. Post-processing gains and envelope behavior are
author suggestions until approved by validation and review.

## Local workflow

```sh
SDK="python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py"
PKG="alt_firmwares/plaits_lab_sdk/packages/community"   # id-namespaced package home

$SDK init $PKG/my-model --from pulsar
$SDK check $PKG/my-model --full
$SDK dev $PKG/my-model
$SDK submit $PKG/my-model --output my-model.zip
```

`check` validates metadata, licensing, source boundaries, scenarios, and a
host compilation. `check --full` and `submit` also run sanitizer-backed preview
scenarios and audio-health analysis. `dev` serves a same-origin audition page
(hot reload, scope, spectrum, and A/B listening) at the address it prints. When
the Emscripten toolchain is present, `dev` also compiles the engine to
WebAssembly and offers real-time live audition in the browser (an AudioWorklet
running the same source and parameter contract); it is optional and never part
of validation or the firmware build.

Local tooling can produce an explicitly unreviewed hardware WAV for the
contributor's own module, using the pinned ARM toolchain locally or through the
builder container. The hosted service must not return an installable
firmware image for arbitrary submitted C++; only immutable, approved package
digests may enter public firmware builds.

## Validation and publication

Publication advances an immutable version through these gates:

1. Package schema, source containment, SPDX license, and provenance.
2. Pinned host and ARM compilation with an allowlisted SDK surface.
3. Sanitizers and deterministic reset, trigger, and parameter-sweep tests.
4. Finite-output, peak, DC, silence, denormal, and stuck-output checks.
5. Flash delta and worst-case CPU budgets. Allocator high-water and stack
   measurement remain planned publication gates.
6. Complete firmware link and reproducible audio-update generation.
7. Human source, licensing, sound, switching, and hardware review.

Automated audio metrics identify hazards, not musical quality. Unusual spectra,
silence in some settings, or high dynamic range should usually produce review
warnings rather than automatic rejection.

## Deliberate v1 limits

- MIT, BSD-2-Clause, BSD-3-Clause, or ISC source only.
- No external packages or network access during builds.
- No custom user data, samples, generated binary blobs, assembly, threads,
  dynamic loading, system calls, or direct hardware access.
- No libm transcendentals (`std::sin`/`cos`/`exp`/`log`/`pow`/…): the bare-metal
  firmware can't link them (they pull in `__errno` and bloat flash). Use the
  shared LUTs — `plaits::Sine` and `stmlib::SemitonesToRatio`. `check` rejects
  them (the host compiler is more permissive), so this surfaces before the
  hardware link, not at it.
- The firmware builds as **C++98** (arm-none-eabi 4.8). The host `check` compile
  is C++11, so C++11-only code (e.g. `<cstdint>`, `auto`, range-for) passes there
  and fails the hardware toolchain. Write C++98 — use `<stdint.h>` for fixed-width
  integers. `check` flags `<cstdint>`; `check --arm` catches the rest.
- Community source is compiled in a networkless, resource-limited container,
  but container isolation does not make the resulting MCU firmware safe.
- Community catalog cards remain typographic; preview audio is generated from
  declared scenarios rather than accepting arbitrary artwork or media.

## Production safeguards and remaining work

Source intake is open, but publication remains maintainer-controlled. The
shipped safeguards are:

- every uploaded ZIP is opened server-side, structurally checked, and
  digest-matched to the source the contributor checked;
- the maintainer reruns `plaits-lab check --full --arm` before advancing a
  submission to `checks-passed`;
- QEMU cost estimation and the on-module DWT cycle probe measure CPU risk, and
  the review lifecycle requires a `hardware-beta` stage before publication;
- source-policy checks, upload caps, networkless/resource-limited builds,
  public-endpoint rate limits, review state/notes, and the immutable approved
  package-digest allowlist constrain intake and publication; and
- the public recipe builder continues to compile approved package digests only,
  never arbitrary submitted C++.

Still open:

- queue and run the full host/sanitizer/ARM revalidation server-side instead of
  relying on the maintainer's operational rerun;
- add allocator high-water and stack measurements as enforced publication
  budgets;
- keep append-only maintainer audit history rather than only the current review
  state, note, and timestamps;
- cryptographically sign the published catalog; and
- establish a broader hardware beta cohort beyond the maintainer-operated
  lifecycle stage.
