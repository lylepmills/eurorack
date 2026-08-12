#!/usr/bin/env python3
"""Build the autonomous two-profile wavetable equation probe."""

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
AUTOSWEEP_DEFINE = "#define PLAITS_WAVETABLE_BENCH_AUTOSWEEP 0"
STRESS_DEFINE = "#define PLAITS_WAVETABLE_BENCH_STRESS_AUTOSWEEP 0"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = SOURCE.read_text(encoding="utf-8")
    if source.count(AUTOSWEEP_DEFINE) != 1 or source.count(STRESS_DEFINE) != 1:
        raise SystemExit("autosweep defines changed; update build_stress_autosweep.py")
    args.output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="wavetable-equation-stress-") as temp:
        package = Path(temp) / "wavetable-equation-bench"
        shutil.copytree(HERE, package, ignore=shutil.ignore_patterns("__pycache__"))
        autosweep_source = package / "src" / SOURCE.name
        autosweep_source.write_text(
            source.replace(AUTOSWEEP_DEFINE,
                           "#define PLAITS_WAVETABLE_BENCH_AUTOSWEEP 1")
                  .replace(STRESS_DEFINE,
                           "#define PLAITS_WAVETABLE_BENCH_STRESS_AUTOSWEEP 1"),
            encoding="utf-8",
        )
        subprocess.run([
            sys.executable, str(SDK), "build", str(package), "--hardware",
            "--cpu-probe-aux", "--output", str(args.output),
        ], check=True)

    print(f"Built autonomous wavetable stress probe: {args.output}")
    print("Cycle: 199 s; capture at least 398 s for a guaranteed full pass.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
