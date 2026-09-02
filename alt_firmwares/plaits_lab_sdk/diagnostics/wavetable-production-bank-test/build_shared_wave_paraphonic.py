#!/usr/bin/env python3
"""Build the autonomous schema-28 Wave Paraphonic shared-line gate."""

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


AUTOSWEEP_DEFINES = {
    "wave-paraphonic": "#define PLAITS_SHARED_WAVE_PARAPHONIC_AUTOSWEEP 1",
    "scale-wavetables": "#define PLAITS_SHARED_WAVE_SCALE_AUTOSWEEP 1",
}
IMAGE = "plaits-lab-builder:local"


def sampled_data() -> str:
    values = bytearray()
    for frame in range(64):
        harmonic = frame + 1
        for sample in range(128):
            phase = 2.0 * math.pi * sample / 128.0
            signed = max(
                -127,
                min(127, round(math.sin(harmonic * phase) * 127.0)),
            )
            values.append(signed & 0xFF)
    return base64.b64encode(values).decode("ascii")


def recipe(engine: str = "wave-paraphonic") -> dict:
    data = sampled_data()
    entries = []
    for index in range(16):
        model = {
            "kind": "wavetable",
            "name": f"Diagnostic frame ladder {index + 1}",
            "equation": "sin((1 + floor(x * 63.999)) * phi)",
            "data": data,
        }
        if index & 1:
            model["representation"] = "native"
        entries.append({"kind": "custom", "model": model})

    return {
        "schemaVersion": 28,
        "target": "mutable-instruments-plaits",
        "firmware": "rubato-plaits",
        "slots": (
            ["wave-paraphonic"] * 24
            if engine == "wave-paraphonic"
            else ["wavetable-chord", "wavetable-scale-stack"] * 12
        ),
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
                    "chords": [
                        {"bank": index, "frame": index}
                        for index in range(15)
                    ],
                    "braids": [
                        {"bank": index % 16, "frame": index}
                        for index in range(33)
                    ],
                },
            },
        },
    }


def build(output_dir: Path, engine: str = "wave-paraphonic") -> None:
    payload = recipe(engine)
    with tempfile.TemporaryDirectory(prefix="plaits-shared-paraphonic-") as temporary:
        root = Path(temporary)
        build_root = root / "build"
        build_root.mkdir()
        config_path = build_root / "engine_config.h"
        config = render_config(validate_recipe(payload))
        marker = "#define PLAITS_BUILD_LINEAR_TZFM"
        if marker not in config:
            raise SystemExit("generated config layout changed; autosweep was not enabled")
        config_path.write_text(
            config.replace(
                marker, f"{AUTOSWEEP_DEFINES[engine]}\n{marker}", 1
            ),
            encoding="utf-8",
        )

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

        stem = (
            "shared-wave-line-wave-paraphonic-autonomous"
            if engine == "wave-paraphonic"
            else "shared-wave-line-scale-wavetables-autonomous"
        )
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
            f"{engine}: {binary.stat().st_size:,} application bytes, "
            f"sha256 {digest}\n  {output_dir / f'{stem}.wav'}",
            flush=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--engine", choices=tuple(AUTOSWEEP_DEFINES),
        default="wave-paraphonic",
    )
    args = parser.parse_args()
    build(args.output_dir.resolve(), args.engine)
    print("Cycle: 50 s; capture MAIN for at least 100 s at 48 kHz mono 16-bit PCM.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
