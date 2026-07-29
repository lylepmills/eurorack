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

# An engine's cost can depend on its parameters, so one sample of the space can
# miss the case that actually breaks. These positions cover the corners plus the
# centre; the estimate reports the WORST, because the worst is what glitches.
SWEEP_POSITIONS = (
    ("centre",    0.5, 0.5, 0.5, 48.0),
    ("low",       0.05, 0.05, 0.05, 36.0),
    ("high",      0.95, 0.95, 0.95, 72.0),
    ("harm-high", 0.95, 0.5, 0.5, 48.0),
    ("timbre-hi", 0.5, 0.95, 0.5, 60.0),
)

COUNTS_RE = re.compile(
    r"PLAITS_QEMU_COUNTS insns=(\d+) flash_reads=(\d+) ram_reads=(\d+) writes=(\d+)"
    r" divs=(\d+) sqrts=(\d+) vcmps=(\d+) branches=(\d+)"
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


PC_RE = re.compile(r"PLAITS_QEMU_PC 0x([0-9a-f]+) (\d+) (\d+)")


def run_qemu(elf: Path, plugin: Path) -> tuple[tuple[int, ...], dict[int, tuple[int, int]]]:
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
    pcs = {int(m.group(1), 16): (int(m.group(2)), int(m.group(3)))
           for m in PC_RE.finditer(result.stderr)}
    return tuple(int(g) for g in match.groups()), pcs


def report_profile(symbols_path: Path, pc_lo: dict, pc_hi: dict, samples: int) -> None:
    """Attribute the marginal PC histogram to functions.

    The A/B subtraction cancels startup and Init, so what remains is the render
    loop alone. Paired with the hardware probe's per-section cycles this gives a
    per-FUNCTION cycles-per-instruction -- the number that says which code runs
    slow, rather than leaving anyone to theorize about a whole engine."""
    symbols = []
    for line in symbols_path.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[2].lower() in ("t", "w"):
            try:
                symbols.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
            except ValueError:
                continue
    symbols.sort()

    def function_of(addr: int) -> str:
        lo, hi = 0, len(symbols) - 1
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            if symbols[mid][0] <= addr:
                best = symbols[mid]
                lo = mid + 1
            else:
                hi = mid - 1
        if best is None:
            return "<unknown>"
        start, size, name = best
        if size and addr >= start + size + 32:
            return "<unknown>"
        return name

    per_function: dict[str, list[float]] = {}
    for addr in set(pc_lo) | set(pc_hi):
        d_insn = pc_hi.get(addr, (0, 0))[0] - pc_lo.get(addr, (0, 0))[0]
        d_vcmp = pc_hi.get(addr, (0, 0))[1] - pc_lo.get(addr, (0, 0))[1]
        if d_insn <= 0 and d_vcmp <= 0:
            continue
        entry = per_function.setdefault(function_of(addr), [0.0, 0.0])
        entry[0] += d_insn / samples
        entry[1] += d_vcmp / samples

    rows = sorted(per_function.items(), key=lambda item: -item[1][0])
    total = sum(v[0] for _, v in rows)
    print(f"\nper-function instruction counts (per sample, render loop only):")
    print(f"{'insns':>8} {'vcmps':>7} {'share':>7}  function")
    for name, (insns, vcmps) in rows:
        if insns < 1.0:
            continue
        display = name if len(name) <= 76 else name[:73] + "..."
        print(f"{insns:8.1f} {vcmps:7.1f} {100*insns/total:6.1f}%  {display}")


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
    # unpatched = TRIGGER_UNPATCHED (2), the state an engine sees on a module
    # with nothing in TRIG -- the calibration condition. patched-idle = 0.
    parser.add_argument("--trigger", choices=("unpatched", "patched-idle"),
                        default="unpatched")
    parser.add_argument("--stereo", action="store_true",
        help="measure the stereo (OUT/AUX as L/R) render path instead of mono")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--sweep", action="store_true",
        help="measure several parameter positions and report the worst")
    parser.add_argument("--profile", action="store_true",
        help="per-function instruction histogram (single position)")
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

    from cost_model import CostModel
    model = CostModel()

    positions = SWEEP_POSITIONS if args.sweep else (
        ("as-given", args.harmonics, args.timbre, args.macro, args.note),
    )

    with tempfile.TemporaryDirectory(prefix="plaits-qemu-") as temp:
        out_dir = Path(temp)
        mapped = []
        for unit in units:
            q = Path(unit)
            if q.is_relative_to(REPO_ROOT):
                mapped.append(f"/workspace/{q.relative_to(REPO_ROOT)}")
            else:
                mapped.append(f"/contributor/{q.relative_to(src_root.parent)}")

        # One docker invocation builds every ELF. Under amd64 emulation the
        # container start dominates, so batching turns a 5x sweep into roughly
        # the cost of a single run.
        commands = []
        for name, harm, timb, macro, note in positions:
            for label, blocks in (("a", args.blocks_a), ("b", args.blocks_b)):
                commands.append(" ".join(shlex.quote(c) for c in container_compile(
                    mapped, ["/workspace", "/contributor/src", "/qemu"],
                    [f'-DPLAITS_LAB_ENGINE_HEADER="{header_define}"',
                     f"-DPLAITS_LAB_ENGINE_CLASS=plaits::{package['manifest']['source']['className']}",
                     f"-DPLAITS_QEMU_BLOCKS={blocks}",
                     f"-DPLAITS_QEMU_HARMONICS={harm}f",
                     f"-DPLAITS_QEMU_MACRO={macro}f",
                     f"-DPLAITS_QEMU_TIMBRE={timb}f",
                     f"-DPLAITS_QEMU_NOTE={note}f",
                     f"-DPLAITS_QEMU_TRIGGER={2 if args.trigger == 'unpatched' else 0}",
                     f"-DPLAITS_QEMU_STEREO={1 if args.stereo else 0}"],
                    f"/output/h_{name}_{label}.elf", args.image, [])))
        if args.profile:
            # The symbol table is what turns PC buckets into function names.
            commands.append(
                "/usr/local/arm-4.8.3/bin/arm-none-eabi-nm -C -S -n "
                f"/output/h_{positions[0][0]}_a.elf > /output/symbols.txt")
        docker = [
            "docker", "run", "--rm", "--platform", "linux/amd64",
            "--entrypoint", "sh",
            "-v", f"{REPO_ROOT}:/workspace:ro",
            "-v", f"{src_root.parent}:/contributor:ro",
            "-v", f"{QEMU_DIR}:/qemu:ro",
            "-v", f"{out_dir}:/output",
            "-w", "/workspace",
            args.image, "-c", " && ".join(commands),
        ]
        if not args.quiet:
            print(f"building {len(commands)} harnesses ({len(positions)} positions)...")
        r = subprocess.run(docker, text=True, capture_output=True, check=False)
        if r.returncode:
            raise SystemExit(f"harness build failed\n{(r.stderr or r.stdout)[-4000:]}")

        results = []
        samples = (args.blocks_b - args.blocks_a) * BLOCK_SIZE
        for name, harm, timb, macro, note in positions:
            lo, pc_lo = run_qemu(out_dir / f"h_{name}_a.elf", plugin)
            hi, pc_hi = run_qemu(out_dir / f"h_{name}_b.elf", plugin)
            d = [hi[i] - lo[i] for i in range(len(lo))]
            results.append({"position": name, "insns": d[0] / samples,
                            "flash": d[1] / samples, "ram": d[2] / samples,
                            "writes": d[3] / samples,
                            "divs": d[4] / samples, "sqrts": d[5] / samples,
                            "vcmps": d[6] / samples, "branches": d[7] / samples})
            if not args.quiet:
                print(f"  {name:10} {d[0]/samples:8.1f} instructions/sample")

        if args.profile:
            report_profile(out_dir / "symbols.txt", pc_lo, pc_hi, samples)

    worst = max(results, key=lambda r: r["insns"])
    est = model.estimate(worst["insns"])
    symbol, sentence = model.verdict(worst["insns"])

    if args.quiet:
        print(f"RESULT worst={worst['position']} insns={worst['insns']:.1f} "
              f"vcmps={worst['vcmps']:.1f} branches={worst['branches']:.1f} "
              f"divs={worst['divs']:.2f} usage={100*est['usage']:.0f}%")
        return 0

    print(f"\nworst case: {worst['position']}  ({worst['insns']:.1f} instructions/sample)")
    if len(results) > 1:
        spread = worst["insns"] / min(r["insns"] for r in results)
        print(f"  cost varies {spread:.2f}x across the parameter space")
    print(f"\n{symbol}: approximately {100*est['usage']:.0f}% of the CPU budget "
          f"(likely {100*est['usage_low']:.0f}-{100*est['usage_high']:.0f}%) -- {sentence}")
    print(f"\n{model.outlier_note()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
