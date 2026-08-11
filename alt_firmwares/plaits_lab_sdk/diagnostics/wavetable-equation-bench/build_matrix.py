#!/usr/bin/env python3
"""Build linker-prunable, CPU-probed firmware for each wavetable case."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
SDK = HERE.parents[1] / "plaits_lab.py"
SOURCE = HERE / "src" / "wavetable_equation_bench_engine.cc"
FIXED_DEFINE = "#define PLAITS_WAVETABLE_BENCH_FIXED_CASE -1"
CASE_NAMES = (
    "sampled-bank-4kb",
    "mutable-fm",
    "glass-fm",
    "harmonic-grid",
    "phase-warp",
    "pulse-matrix",
    "parity-weave",
    "glass-upper-partial",
    "glass-row-motion",
    "glass-folded",
    "glass-fm-wrapped",
    "glass-ring-grid",
    "glass-terraced",
    "glass-soft-clipped",
    "glass-hard-clipped",
    "three-transform-stack",
    "eight-sine-stress",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--case", type=int, action="append", dest="cases")
    args = parser.parse_args()
    selected = args.cases if args.cases is not None else list(range(len(CASE_NAMES)))
    invalid = [case for case in selected if case < 0 or case >= len(CASE_NAMES)]
    if invalid:
        parser.error(f"case must be 0 through {len(CASE_NAMES) - 1}: {invalid}")

    source = SOURCE.read_text(encoding="utf-8")
    if source.count(FIXED_DEFINE) != 1:
        raise SystemExit("fixed-case define changed; update build_matrix.py")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    for case in selected:
        name = CASE_NAMES[case]
        with tempfile.TemporaryDirectory(prefix=f"wavetable-equation-{case:02d}-") as temp:
            package = Path(temp) / "wavetable-equation-bench"
            shutil.copytree(HERE, package, ignore=shutil.ignore_patterns("__pycache__"))
            fixed_source = package / "src" / SOURCE.name
            fixed_source.write_text(
                source.replace(FIXED_DEFINE,
                               f"#define PLAITS_WAVETABLE_BENCH_FIXED_CASE {case}"),
                encoding="utf-8",
            )
            output = args.output_dir / f"{case:02d}-{name}.wav"
            subprocess.run([
                sys.executable, str(SDK), "build", str(package), "--hardware",
                "--cpu-probe-aux", "--output", str(output),
            ], check=True)

    print(f"Built {len(selected)} diagnostic firmware file(s) in {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
