#!/usr/bin/env python3
"""Decode a direct Core Audio capture from the autonomous wavetable probe."""

from __future__ import annotations

import importlib.util
from pathlib import Path


CASE_NAMES = (
    "4 KB sampled bank",
    "Mutable FM",
    "Glass FM",
    "Harmonic grid",
    "Phase warp",
    "Pulse matrix",
    "Odd / even weave",
    "Glass + upper partial",
    "Glass + row motion",
    "Glass folded",
    "Glass FM-wrapped",
    "Glass ring grid",
    "Glass terraced",
    "Glass soft-clipped",
    "Glass hard-clipped",
    "Three-transform stack",
    "Eight-sine stress",
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
    decoder.CASE_NAMES = CASE_NAMES
    decoder.CYCLE = decoder.LEADER + len(CASE_NAMES) * decoder.SLOT + 6.0
    decoder.main()


if __name__ == "__main__":
    main()
