#!/usr/bin/env python3
"""Measure production Wavetable-bank flash costs with real ARM links."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from build_autonomous import IMAGE, REPO, custom, recipe, sampled_data

from generate_engine_config import render_config, validate_recipe


BENCHMARK_EQUATIONS = (
    ("mutable-fm", "sin(phi + 3 * (x + 0.125) * sin((1 + floor(4 * y)) * phi))"),
    ("glass-fm", "sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi))"),
    ("harmonic-grid", "sin(phi) + 0.45 * sin((2 + floor(5 * x)) * phi) + 0.28 * sin((3 + floor(8 * y)) * phi)"),
    ("phase-warp", "sin(phi + (0.3 + 2.8 * x) * sin(phi + pi * y))"),
    ("pulse-matrix", "sign(sin(phi) - (0.75 * x - 0.35)) + 0.18 * sin((2 + floor(5 * y)) * phi)"),
    ("parity-weave", "sin(phi) + 0.65 * x * sin(2 * phi) + 0.55 * y * sin(3 * phi) + 0.3 * x * y * sin(5 * phi)"),
    ("glass-upper-partial", "(sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi))) + 0.24 * sin((2 + floor(6 * x)) * phi)"),
    ("glass-row-motion", "(sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi))) + 0.22 * sin((3 + 7 * y) * phi + pi * x)"),
    ("glass-folded", "sin(pi * (sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi))))"),
    ("glass-fm-wrapped", "sin(phi + (1 + 4 * y) * (sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi))))"),
    ("glass-ring-grid", "(sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi))) * sin((2 + floor(4 * y)) * phi)"),
    ("glass-terraced", "round(5 * (sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi)))) / 5"),
    ("glass-soft-clipped", "atan(2.5 * (sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi))))"),
    ("glass-hard-clipped", "max(-0.6, min(0.6, (sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi)))))"),
    ("three-transform-stack", "atan(2.5 * (sin(pi * ((sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * sin(2 * phi))) + 0.22 * sin((3 + 7 * y) * phi + pi * x)))))"),
)


def native(name: str, equation: str, index: int) -> dict:
    return {
        "kind": "custom",
        "model": {
            "kind": "wavetable",
            "name": name,
            "equation": equation,
            "data": sampled_data(index % 12 + 1),
            "representation": "native",
        },
    }


def sampled(index: int) -> dict:
    return custom(index % 12 + 1, "sampled")


def wave_lines(bank_count: int) -> dict[str, list[dict[str, int]]]:
    """A deterministic shared-library mapping that touches every input bank."""
    def line(length: int) -> list[dict[str, int]]:
        return [
            {
                "bank": min(bank_count - 1, index * bank_count // length),
                "frame": round(index * 63 / (length - 1)),
            }
            for index in range(length)
        ]

    return {"chords": line(15), "braids": line(33)}


def shared_recipe(entries: list[dict]) -> dict:
    payload = recipe(entries, True)
    payload["schemaVersion"] = 28
    payload["initialOptions"]["trigResponse"] = "trigger"
    payload["resources"]["wavetableBank"]["waveLines"] = wave_lines(len(entries))
    return payload


def shared_consumer_recipe(entries: list[dict], consumer: str = "chords") -> dict:
    payload = shared_recipe(entries)
    payload["slots"] = [consumer] * 24
    return payload


def shared_all_consumers_recipe(entries: list[dict]) -> dict:
    payload = shared_recipe(entries)
    consumers = [
        "wavetable", "wave-terrain", "chords", "wave-paraphonic",
        "wavetable-chord", "wavetable-scale-stack",
    ]
    payload["slots"] = consumers * 4
    return payload


def stock_consumer_recipe(consumer: str = "chords") -> dict:
    payload = recipe([{"kind": "factory", "id": "mutable-1"}], True)
    payload["schemaVersion"] = 27
    payload["slots"] = [consumer] * 24
    payload["initialOptions"]["trigResponse"] = "trigger"
    del payload["resources"]["wavetableBank"]
    return payload


def matrix() -> dict[str, dict]:
    legacy = recipe([{"kind": "factory", "id": "mutable-1"}], True)
    legacy["schemaVersion"] = 24
    del legacy["resources"]["wavetableBank"]
    natives = [native(name, equation, index) for index, (name, equation) in enumerate(BENCHMARK_EQUATIONS)]
    native_sixteen = [*natives, native("glass-fm-repeat", BENCHMARK_EQUATIONS[1][1], 15)]
    mixed = [
        {"kind": "factory", "id": "mutable-1"},
        natives[0],
        sampled(2),
        {"kind": "factory", "id": "mutable-2"},
        natives[1],
        sampled(5),
        {"kind": "factory", "id": "mutable-3"},
        natives[2],
    ]
    return {
        "legacy-stock": legacy,
        "factory-three": recipe([
            {"kind": "factory", "id": "mutable-1"},
            {"kind": "factory", "id": "mutable-2"},
            {"kind": "factory", "id": "mutable-3"},
        ], True),
        "sampled-1": recipe([sampled(0)], True),
        "sampled-2": recipe([sampled(index) for index in range(2)], True),
        "sampled-8": recipe([sampled(index) for index in range(8)], True),
        "sampled-16": recipe([sampled(index) for index in range(16)], False),
        "native-1": recipe(natives[:1], True),
        "native-2": recipe(natives[:2], True),
        "native-3": recipe(natives[:3], True),
        "native-8": recipe(natives[:8], True),
        "native-16": recipe(native_sixteen, False),
        "mixed-8": recipe(mixed, True),
        # Schema 28 makes the left-column library authoritative for every
        # wavetable consumer. These four cases prove that each factory entry is
        # now independently linker-prunable and calibrate the selector cost.
        "shared-factory-3": shared_recipe([
            {"kind": "factory", "id": "mutable-1"},
            {"kind": "factory", "id": "mutable-2"},
            {"kind": "factory", "id": "mutable-3"},
        ]),
        "shared-factory-2": shared_recipe([
            {"kind": "factory", "id": "mutable-1"},
            {"kind": "factory", "id": "mutable-2"},
        ]),
        "shared-factory-1": shared_recipe([
            {"kind": "factory", "id": "mutable-1"},
        ]),
        "shared-sampled-1": shared_recipe([sampled(0)]),
        "chords-stock": stock_consumer_recipe(),
        "chords-factory-3": shared_consumer_recipe([
            {"kind": "factory", "id": "mutable-1"},
            {"kind": "factory", "id": "mutable-2"},
            {"kind": "factory", "id": "mutable-3"},
        ]),
        "chords-factory-1": shared_consumer_recipe([
            {"kind": "factory", "id": "mutable-1"},
        ]),
        "chords-sampled-1": shared_consumer_recipe([sampled(0)]),
        "braids-stock": stock_consumer_recipe("wave-paraphonic"),
        "braids-factory-3": shared_consumer_recipe([
            {"kind": "factory", "id": "mutable-1"},
            {"kind": "factory", "id": "mutable-2"},
            {"kind": "factory", "id": "mutable-3"},
        ], "wave-paraphonic"),
        "braids-sampled-1": shared_consumer_recipe(
            [sampled(0)], "wave-paraphonic"),
        "shared-all-consumers": shared_all_consumers_recipe([
            {"kind": "factory", "id": "mutable-1"},
            sampled(2),
            {"kind": "factory", "id": "mutable-2"},
            natives[1],
            {"kind": "factory", "id": "mutable-3"},
        ]),
    }


def build(slug: str, payload: dict, output_dir: Path) -> dict:
    with tempfile.TemporaryDirectory(prefix=f"plaits-wavetable-flash-{slug}-") as temporary:
        root = Path(temporary)
        build_root = root / "build"
        build_root.mkdir()
        (build_root / "engine_config.h").write_text(
            render_config(validate_recipe(payload)), encoding="utf-8"
        )
        subprocess.run([
            "docker", "run", "--rm", "--platform", "linux/amd64",
            "-v", f"{REPO}:/work", "-v", f"{root}:/out", "-w", "/work",
            "--entrypoint", "make", IMAGE, "-f", "plaits/makefile",
            "BUILD_ROOT=/out/build/", "ENGINE_CONFIG=/out/build/engine_config.h",
            "DEPS=", "PLAITS_STEREO_WAVETABLE=0", "-j8", "bin",
        ], check=True, stdout=subprocess.DEVNULL)
        artifacts = build_root / "plaits"
        binary = artifacts / "plaits.bin"
        map_path = artifacts / "plaits.map"
        if not binary.is_file() or not map_path.is_file():
            raise SystemExit(f"{slug}: build did not produce bin and map artifacts")
        output_dir.mkdir(parents=True, exist_ok=True)
        for suffix in ("bin", "elf", "map"):
            shutil.copyfile(artifacts / f"plaits.{suffix}", output_dir / f"{slug}.{suffix}")
        (output_dir / f"{slug}.recipe.json").write_text(
            json.dumps(payload, indent=2) + "\n", encoding="utf-8"
        )
        map_text = map_path.read_text(encoding="utf-8")
        functions = {
            int(index): int(size, 16)
            for index, size in re.findall(
                r" \.text\._ZN6plaitsL\d+WavetableEquation_(\d+)Efff\n"
                r"\s+0x[0-9a-f]+\s+0x([0-9a-f]+)",
                map_text,
            )
        }
        linked_factory_banks = [
            int(bank)
            for bank, address in re.findall(
                r"\.rodata\._ZN6plaits22wav_integrated_waves_([123])E\n\s+"
                r"(0x[0-9a-f]+)\s+0x4200",
                map_text,
            )
            if address != "0x00000000"
        ]
        result = {
            "bytes": binary.stat().st_size,
            "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "factoryPoolLinked": bool(linked_factory_banks),
            "linkedFactoryBanks": linked_factory_banks,
            "nativeFunctionBytes": functions,
        }
        print(
            f"{slug:14} {result['bytes']:>7,} B  "
            f"factory banks {linked_factory_banks or 'none'}",
            flush=True,
        )
        return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--case", action="append", choices=tuple(matrix()))
    args = parser.parse_args()
    cases = matrix()
    if args.case:
        cases = {name: cases[name] for name in args.case}
    results = {
        "sourceRevision": subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=REPO, check=True,
            text=True, stdout=subprocess.PIPE,
        ).stdout.strip(),
        "cases": {},
        "benchmarkEquations": [name for name, _equation in BENCHMARK_EQUATIONS],
    }
    for slug, payload in cases.items():
        results["cases"][slug] = build(slug, payload, args.output_dir.resolve())
        (args.output_dir / "results.json").write_text(
            json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
