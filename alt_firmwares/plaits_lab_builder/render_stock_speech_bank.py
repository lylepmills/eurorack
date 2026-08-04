#!/usr/bin/env python3
"""Build and run the host-only renderer for a stock Plaits LPC word bank."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


SCRIPT = Path(__file__).resolve()
CPP_SOURCE = SCRIPT.with_suffix(".cc")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--bank", required=True, type=int, choices=range(5))
    parser.add_argument("--mode", required=True, choices=("flat", "natural"))
    return parser.parse_args()


def build_renderer(repo: Path, output: Path) -> None:
    sources = [
        CPP_SOURCE,
        repo / "plaits/dsp/speech/lpc_speech_synth.cc",
        repo / "plaits/dsp/speech/lpc_speech_synth_controller.cc",
        repo / "plaits/dsp/speech/lpc_speech_synth_phonemes.cc",
        repo / "plaits/dsp/speech/lpc_speech_synth_words.cc",
        repo / "plaits/resources.cc",
        repo / "stmlib/dsp/units.cc",
        repo / "stmlib/utils/random.cc",
    ]
    stale = not output.exists() or any(path.stat().st_mtime > output.stat().st_mtime for path in sources)
    if not stale:
        return
    compiler = shutil.which("c++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("No host C++ compiler was found.")
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        compiler,
        "-std=c++11",
        "-O2",
        "-w",
        "-I",
        str(repo),
        *(str(source) for source in sources),
        "-o",
        str(output),
    ], check=True)


def main() -> int:
    args = parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    renderer = args.output.parent / "render-stock-speech-bank"
    build_renderer(args.repo, renderer)
    subprocess.run([
        str(renderer),
        str(args.bank),
        str(args.output),
        "1.0" if args.mode == "natural" else "0.0",
    ], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
