#!/usr/bin/env python3
"""Build the autonomous schema-28 shared-wave Chords hardware gate."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
BUILDER = REPO / "alt_firmwares" / "plaits_lab_builder"
sys.path.insert(0, str(BUILDER))

from generate_engine_config import (  # noqa: E402
    DEFAULT_CHORD_TABLES,
    DEFAULT_CONFIGURATION,
    render_config,
    validate_recipe,
)


AUTOSWEEP_DEFINE = "#define PLAITS_SHARED_WAVE_CHORD_AUTOSWEEP 1"
IMAGE = "plaits-lab-builder:local"


def sampled_data(harmonic: int) -> str:
    values = bytearray()
    for _frame in range(64):
        for sample in range(128):
            phase = 2.0 * math.pi * sample / 128.0
            signed = max(-127, min(127, round(math.sin(harmonic * phase) * 127.0)))
            values.append(signed & 0xFF)
    return base64.b64encode(values).decode("ascii")


def custom_bank(harmonic: int) -> dict:
    model = {
        "kind": "wavetable",
        "name": f"Diagnostic harmonic {harmonic}",
        "equation": f"sin({harmonic} * phi)",
        "data": sampled_data(harmonic),
    }
    if harmonic & 1:
        model["representation"] = "native"
    return {
        "kind": "custom",
        "model": model,
    }


def recipe() -> dict:
    entries = [custom_bank(harmonic) for harmonic in range(1, 16)]
    chord_line = [
        {"bank": index, "frame": (index * 13) % 64}
        for index in range(15)
    ]
    braids_line = [
        {"bank": index % 15, "frame": (index * 7) % 64}
        for index in range(33)
    ]
    return {
        "schemaVersion": 28,
        "target": "mutable-instruments-plaits",
        "firmware": "rubato-plaits",
        # Persisted engine selection cannot escape the instrumented path.
        "slots": ["chords"] * 24,
        "output": "audio-wav",
        "preferences": {
            "navigationMode": "linear",
            "calibration": False,
            "colorBlindMode": False,
            "replaceableFmBanks": False,
            "syncInput": False,
            "linearTzfm": False,
            "fastFm": False,
            "simplifiedPitchRanges": False,
        },
        "initialOptions": {
            **DEFAULT_CONFIGURATION["initialOptions"],
            "attenuverterMode": "stock",
            "trigResponse": "trigger",
        },
        "resources": {
            "chordTables": DEFAULT_CHORD_TABLES,
            "wavetableBank": {
                "mirrored": False,
                "entries": entries,
                "waveLines": {
                    "chords": chord_line,
                    "braids": braids_line,
                },
            },
        },
    }


def build(output_dir: Path) -> None:
    payload = recipe()
    with tempfile.TemporaryDirectory(prefix="plaits-shared-chords-") as temporary:
        root = Path(temporary)
        build_root = root / "build"
        build_root.mkdir()
        config_path = build_root / "engine_config.h"
        config = render_config(validate_recipe(payload))
        marker = "#define PLAITS_BUILD_LINEAR_TZFM"
        if marker not in config:
            raise SystemExit("generated config layout changed; autosweep was not enabled")
        config = config.replace(marker, f"{AUTOSWEEP_DEFINE}\n{marker}", 1)
        config_path.write_text(config, encoding="utf-8")

        # resources.cc is checked in, but fresh checkout mtimes can make GNU
        # make try to regenerate it with SciPy inside the lean builder image.
        # Match the SDK hardware-build path: stamp the generated source newest
        # without changing its contents.
        (REPO / "plaits" / "resources.cc").touch()

        subprocess.run([
            "docker", "run", "--rm", "--platform", "linux/amd64",
            "-v", f"{REPO}:/work",
            "-v", f"{root}:/out",
            "-w", "/work",
            "--entrypoint", "make",
            IMAGE,
            "-f", "plaits/makefile",
            "BUILD_ROOT=/out/build/",
            "ENGINE_CONFIG=/out/build/engine_config.h",
            "DEPS=", "PLAITS_STEREO_CHORDS=0",
            "-j4", "wav",
        ], check=True)

        stem = "shared-wave-lines-chords-autonomous"
        output_dir.mkdir(parents=True, exist_ok=True)
        artifacts = {
            ".bin": build_root / "plaits" / "plaits.bin",
            ".wav": build_root / "plaits" / "plaits.wav",
            ".elf": build_root / "plaits" / "plaits.elf",
            ".map": build_root / "plaits" / "plaits.map",
        }
        for suffix, source in artifacts.items():
            if not source.is_file():
                raise SystemExit(f"builder did not produce {source.name}")
            shutil.copyfile(source, output_dir / f"{stem}{suffix}")
        (output_dir / f"{stem}.recipe.json").write_text(
            json.dumps(payload, indent=2) + "\n", encoding="utf-8"
        )
        binary = artifacts[".bin"]
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        print(
            f"Chords: {binary.stat().st_size:,} application bytes, sha256 {digest}\n"
            f"  {output_dir / f'{stem}.wav'}",
            flush=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    build(args.output_dir.resolve())
    print("Cycle: 50 s; capture MAIN for at least 100 s at 48 kHz mono 16-bit PCM.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
