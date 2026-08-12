#!/usr/bin/env python3
"""Decode AUX captured from the autonomous bank transport probe."""

from __future__ import annotations

import importlib.util
from pathlib import Path


CONFIGURATIONS = (
    "1 bank mirrored", "2 banks mirrored", "3 banks mirrored",
    "8 banks mirrored", "8 banks one-way", "16 banks one-way",
)


def main() -> None:
    shared_path = Path(__file__).resolve().parents[1] / (
        "terrain-equation-bench/decode_autosweep.py")
    spec = importlib.util.spec_from_file_location(
        "terrain_autosweep_decoder", shared_path)
    if spec is None or spec.loader is None:
        raise SystemExit("could not load the shared autosweep decoder")
    decoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(decoder)
    decoder.CASE_NAMES = tuple(
        f"nominal — {name}" for name in CONFIGURATIONS
    ) + tuple(
        f"high-note corner — {name}" for name in CONFIGURATIONS
    )
    decoder.CYCLE = decoder.LEADER + len(decoder.CASE_NAMES) * decoder.SLOT + 6.0
    decoder.main()


if __name__ == "__main__":
    main()
