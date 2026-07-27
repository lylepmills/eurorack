#!/usr/bin/env python3
"""Estimate an engine's on-target CPU cost without a module.

Runs the engine on an emulated Cortex-M4 (QEMU's mps2-an386, the same core the
STM32F373 uses), counting executed instructions and memory reads with a TCG
plugin, then converts those counts into cycles with a model calibrated against
real DWT measurements taken on hardware by plaits/cpu_probe.h.

Why counts rather than a host timing: a development machine is not merely faster
than the module, its memory system differs in kind. Wavetables live in FLASH
here -- wait-stated, no data cache, contending with instruction fetch -- while on
a laptop they sit in L1. Counting flash reads separately is precisely what makes
the estimate transfer; a host stopwatch cannot see them, which is how an engine
measured at 0.6x a stock engine on the host and still ran at 281% of budget.

The harness is run TWICE with different block counts and the counts subtracted,
so startup, table init and Engine::Init cancel out and what remains is the
marginal cost of the render loop alone.
"""

from __future__ import annotations

import argparse
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

QEMU_DIR = Path(__file__).resolve().parent
SDK_DIR = QEMU_DIR.parent
REPO_ROOT = SDK_DIR.parents[1]

# Cycles per sample the audio callback actually has (72 MHz / 48 kHz).
BUDGET_CYCLES_PER_SAMPLE = 1500.0
BLOCK_SIZE = 12

# Cost model, fitted to DWT readings from real hardware. See CALIBRATION.md.
# cycles = A * instructions + B * flash_reads + C * ram_reads
COST_INSN = 1.0
COST_FLASH_READ = 1.0
COST_RAM_READ = 0.0

ARCH_FLAGS = [
    "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
    "-fno-exceptions", "-fno-rtti", "-O2", "-funroll-loops",
    # nano+nosys give newlib its syscall stubs; without them the link pulls in
    # _sbrk/_write/... which a bare-metal harness has no business providing.
    "--specs=nano.specs", "--specs=nosys.specs", "-fno-use-cxa-atexit",
]

COUNTS_RE = re.compile(
    r"PLAITS_QEMU_COUNTS insns=(\d+) flash_reads=(\d+) ram_reads=(\d+) writes=(\d+)"
)


def container_compile(sources: list[str], includes: list[str], defines: list[str],
                      out_elf: str, image: str, mounts: list[str]) -> list[str]:
    """The command that builds the harness ELF with the SAME toolchain the
    firmware uses -- a different compiler would generate different code and make
    the counts incomparable."""
    cxx = "/usr/local/arm-4.8.3/bin/arm-none-eabi-g++"
    cmd = [cxx, *ARCH_FLAGS, *defines]
    for inc in includes:
        cmd += ["-I", inc]
    cmd += ["/qemu/startup.c", *sources]
    cmd += ["-T", "/qemu/mps2.ld", "-nostartfiles", "-Wl,--gc-sections",
            "-o", out_elf]
    return cmd


def run_qemu(elf: Path, plugin: Path) -> tuple[int, int, int, int]:
    result = subprocess.run(
        ["qemu-system-arm", "-M", "mps2-an386", "-cpu", "cortex-m4",
         "-nographic", "-no-reboot",
         "-semihosting-config", "enable=on,target=native",
         "-plugin", str(plugin), "-kernel", str(elf)],
        text=True, capture_output=True, check=False, timeout=420,
    )
    match = COUNTS_RE.search(result.stderr)
    if not match:
        raise SystemExit(
            f"QEMU produced no counts.\nstdout:\n{result.stdout[-2000:]}\n"
            f"stderr:\n{result.stderr[-2000:]}"
        )
    return tuple(int(g) for g in match.groups())  # type: ignore[return-value]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("package", nargs="?")
    parser.add_argument("--builtin", help="measure a stock catalog engine instead of a package")
    parser.add_argument("--image", default="plaits-lab-builder:local")
    parser.add_argument("--blocks-a", type=int, default=200)
    parser.add_argument("--blocks-b", type=int, default=400)
    parser.add_argument("--harmonics", type=float, default=0.5)
    parser.add_argument("--macro", type=float, default=0.5)
    parser.add_argument("--timbre", type=float, default=0.5)
    parser.add_argument("--note", type=float, default=48.0)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    sys.path.insert(0, str(SDK_DIR))
    import plaits_lab  # noqa: E402

    if args.builtin:
        # Stock engines are described by the catalog, not a package directory.
        entry, _ = plaits_lab.builtin_engine(args.builtin)
        package = {
            "repo_root": REPO_ROOT,
            "source_root": REPO_ROOT / "plaits",
            "manifest": {"source": entry["source"], "postProcessing": entry["postProcessing"]},
            "source_files": [REPO_ROOT / f for f in entry["source"]["files"]],
            "header": Path(entry["source"]["header"]),
            "shared": entry.get("sharedModules", []),
        }
        units = plaits_lab.dedupe_units([
            QEMU_DIR / "harness.cc",
            *package["source_files"],
            *plaits_lab.shared_module_sources(package["shared"], REPO_ROOT),
            REPO_ROOT / "plaits/resources.cc",
            REPO_ROOT / "stmlib/dsp/units.cc",
            REPO_ROOT / "stmlib/utils/random.cc",
        ])
        header_define = entry["source"]["header"]
    else:
        package = plaits_lab.load_package(args.package, autodeclare=True)
        units = None
        header_define = None
    plugin = QEMU_DIR / "cycles_plugin.so"
    if not plugin.is_file():
        raise SystemExit(
            f"missing {plugin.name}; build it first:\n"
            f"  cc -shared -fPIC -undefined dynamic_lookup -I/opt/homebrew/include "
            f"$(pkg-config --cflags glib-2.0) -o {plugin} {QEMU_DIR/'cycles_plugin.c'}"
        )
    if shutil.which("qemu-system-arm") is None:
        raise SystemExit("qemu-system-arm not on PATH (brew install qemu)")

    if units is None:
        units = plaits_lab.engine_translation_units(package, QEMU_DIR / "harness.cc")
        header_define = plaits_lab.engine_header_define(package)
    src_root = Path(package["source_root"])

    with tempfile.TemporaryDirectory(prefix="plaits-qemu-") as temp:
        out_dir = Path(temp)
        counts = {}
        for label, blocks in (("a", args.blocks_a), ("b", args.blocks_b)):
            mapped = []
            for unit in units:
                p = Path(unit)
                if p.is_relative_to(REPO_ROOT):
                    mapped.append(f"/workspace/{p.relative_to(REPO_ROOT)}")
                else:
                    mapped.append(f"/contributor/{p.relative_to(src_root.parent)}")
            cmd = container_compile(
                mapped,
                ["/workspace", "/contributor/src", "/qemu"],
                [f'-DPLAITS_LAB_ENGINE_HEADER="{header_define}"',
                 f'-DPLAITS_LAB_ENGINE_CLASS=plaits::{package["manifest"]["source"]["className"]}',
                 f"-DPLAITS_QEMU_BLOCKS={blocks}",
                 f"-DPLAITS_QEMU_HARMONICS={args.harmonics}f",
                 f"-DPLAITS_QEMU_MACRO={args.macro}f",
                 f"-DPLAITS_QEMU_TIMBRE={args.timbre}f",
                 f"-DPLAITS_QEMU_NOTE={args.note}f"],
                f"/output/harness_{label}.elf", args.image, [],
            )
            # The builder image's ENTRYPOINT is its build server, so it has to
            # be overridden to run the toolchain directly -- otherwise the
            # container sits waiting for a request that never comes.
            docker = [
                "docker", "run", "--rm", "--platform", "linux/amd64",
                "--entrypoint", "sh",
                "-v", f"{REPO_ROOT}:/workspace:ro",
                "-v", f"{src_root.parent}:/contributor:ro",
                "-v", f"{QEMU_DIR}:/qemu:ro",
                "-v", f"{out_dir}:/output",
                "-w", "/workspace",
                # shlex.quote: the header define carries embedded quotes that a bare
                # join would hand to the shell to strip.
                args.image, "-c", " ".join(shlex.quote(c) for c in cmd),
            ]
            if not args.quiet: print(f"building harness ({blocks} blocks)...")
            r = subprocess.run(docker, text=True, capture_output=True, check=False)
            if r.returncode:
                raise SystemExit(f"harness build failed\n{(r.stderr or r.stdout)[-4000:]}")
            if not args.quiet: print(f"running under QEMU ({blocks} blocks)...")
            counts[label] = run_qemu(out_dir / f"harness_{label}.elf", plugin)

    d_insn, d_flash, d_ram, d_write = (
        counts["b"][i] - counts["a"][i] for i in range(4)
    )
    samples = (args.blocks_b - args.blocks_a) * BLOCK_SIZE
    per = lambda v: v / samples

    cycles = (COST_INSN * per(d_insn) + COST_FLASH_READ * per(d_flash)
              + COST_RAM_READ * per(d_ram))
    usage = cycles / BUDGET_CYCLES_PER_SAMPLE

    if args.quiet:
        print(f"RESULT note={args.note} harmonics={args.harmonics} macro={args.macro} "
              f"insns={per(d_insn):.1f} flash={per(d_flash):.1f} "
              f"ram={per(d_ram):.1f} writes={per(d_write):.1f}")
        return 0
    print(f"\nper sample, marginal (startup and Init subtracted out):")
    print(f"  instructions      {per(d_insn):8.1f}")
    print(f"  flash reads       {per(d_flash):8.1f}   <- the cost a host timing cannot see")
    print(f"  ram reads         {per(d_ram):8.1f}")
    print(f"  writes            {per(d_write):8.1f}")
    print(f"\nestimated {cycles:.0f} cycles/sample = {100*usage:.0f}% of the "
          f"{BUDGET_CYCLES_PER_SAMPLE:.0f}-cycle budget")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
