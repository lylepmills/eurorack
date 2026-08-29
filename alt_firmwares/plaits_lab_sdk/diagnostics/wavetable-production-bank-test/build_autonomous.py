#!/usr/bin/env python3
"""Build autonomous images around the production Wavetable engine.

Unlike wavetable-bank-transport-bench, these images use the exact generated
recipe resource, production WavetableEngine, Voice, DAC path, and audio
bootloader artifact. The only diagnostic hook replaces the panel/CV parameters
with a timed scan before calling WavetableEngine::RenderInternal.
"""

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


AUTOSWEEP_DEFINE = "#define PLAITS_WAVETABLE_PRODUCTION_AUTOSWEEP 1"
IMAGE = "plaits-lab-builder:local"


def equation(harmonic: int) -> str:
    return (
        f"sin({harmonic} * phi)"
        f" + 0.25 * x * sin({harmonic + 1} * phi)"
        f" + 0.2 * y * sin({harmonic + 2} * phi)"
    )


def sampled_data(harmonic: int) -> str:
    values = bytearray()
    for frame in range(64):
        x = (frame % 8) / 7.0
        y = (frame // 8) / 7.0
        for sample in range(128):
            phi = 2.0 * math.pi * sample / 128.0
            value = (
                math.sin(harmonic * phi)
                + 0.25 * x * math.sin((harmonic + 1) * phi)
                + 0.2 * y * math.sin((harmonic + 2) * phi)
            ) / 1.45
            signed = max(-127, min(127, round(value * 127.0)))
            values.append(signed & 0xFF)
    return base64.b64encode(values).decode("ascii")


def custom(harmonic: int, representation: str) -> dict:
    model = {
        "kind": "wavetable",
        "name": f"Harmonic {harmonic} ({representation})",
        "equation": equation(harmonic),
        "data": sampled_data(harmonic),
    }
    if representation == "native":
        model["representation"] = "native"
    return {"kind": "custom", "model": model}


def recipe(entries: list[dict], mirrored: bool) -> dict:
    slots = ["virtual-analog"] * 24
    slots[0] = "wavetable"
    return {
        "schemaVersion": 25,
        "target": "mutable-instruments-plaits",
        "firmware": "rubato-plaits",
        "slots": slots,
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
        },
        "resources": {
            "chordTables": DEFAULT_CHORD_TABLES,
            "wavetableBank": {"mirrored": mirrored, "entries": entries},
        },
    }


def recipes() -> dict[str, dict]:
    mirrored = [
        {"kind": "factory", "id": "mutable-1"},
        custom(2, "native"),
        custom(3, "sampled"),
        {"kind": "factory", "id": "mutable-2"},
        custom(5, "native"),
        custom(6, "sampled"),
        {"kind": "factory", "id": "mutable-3"},
        custom(8, "native"),
    ]
    one_way = [
        custom(harmonic, "native" if harmonic % 2 else "sampled")
        for harmonic in range(1, 17)
    ]
    return {
        "mirrored-mixed-8": recipe(mirrored, True),
        "one-way-custom-16": recipe(one_way, False),
    }


def build(slug: str, payload: dict, output_dir: Path) -> None:
    with tempfile.TemporaryDirectory(prefix=f"plaits-{slug}-") as temporary:
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
            "DEPS=", "PLAITS_STEREO_WAVETABLE=0",
            "PLAITS_STEREO_VIRTUAL_ANALOG=0",
            "-j4", "wav",
        ], check=True)

        binary = build_root / "plaits" / "plaits.bin"
        firmware = build_root / "plaits" / "plaits.wav"
        if not binary.is_file() or not firmware.is_file():
            raise SystemExit(f"{slug}: builder did not produce bin and wav artifacts")
        output_dir.mkdir(parents=True, exist_ok=True)
        output_binary = output_dir / f"wavetable-production-{slug}.bin"
        output_firmware = output_dir / f"wavetable-production-{slug}.wav"
        output_elf = output_dir / f"wavetable-production-{slug}.elf"
        output_map = output_dir / f"wavetable-production-{slug}.map"
        output_recipe = output_dir / f"wavetable-production-{slug}.recipe.json"
        shutil.copyfile(binary, output_binary)
        shutil.copyfile(firmware, output_firmware)
        shutil.copyfile(build_root / "plaits" / "plaits.elf", output_elf)
        shutil.copyfile(build_root / "plaits" / "plaits.map", output_map)
        output_recipe.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        digest = hashlib.sha256(output_binary.read_bytes()).hexdigest()
        print(
            f"{slug}: {binary.stat().st_size:,} application bytes, "
            f"sha256 {digest}\n  {output_firmware}",
            flush=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--image", choices=("mirrored-mixed-8", "one-way-custom-16", "all"),
        default="all",
    )
    args = parser.parse_args()
    selected = recipes()
    if args.image != "all":
        selected = {args.image: selected[args.image]}
    for slug, payload in selected.items():
        build(slug, payload, args.output_dir.resolve())
    print("Cycle: 50 s; capture at least 100 s for a guaranteed complete pass.")
    print("Capture Plaits AUX as mono 16-bit PCM at 48 kHz.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
