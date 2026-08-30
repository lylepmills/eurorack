#!/usr/bin/env python3
"""Render preview audio for a SAVED Natural Speech bank.

The encode driver returns previews alongside the bank it just built. A bank
that arrives already encoded -- from a saved configuration, or from the
editor's seeded starter banks -- has no encode session behind it, so its
previews have to come from the stored frames.

Everything downstream of the frames is shared with the encoder: the same
generated config header, the same harness compiled against the shipped engine.
There is no TTS and no analysis here, which is why this is seconds rather than
a minute.

    render_natural_speech_bank.py --repo <workspace> --bank <json> --output-dir <dir>
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from encode_natural_speech_bank import PREVIEW_SECONDS, build_preview_renderer
from natural_speech_banks import render_natural_speech_config


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--bank", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    # Already normalized by the caller, which validated it against the recipe
    # contract before writing it here.
    banks = json.loads(args.bank.read_text(encoding="utf-8"))

    config_path = args.output_dir / "recipe_config.h"
    config_path.write_text(render_natural_speech_config(banks), encoding="utf-8")
    binary = args.output_dir / "render_preview"
    build_preview_renderer(args.repo, config_path, binary)

    import subprocess
    result = subprocess.run(
        [str(binary), str(args.output_dir), str(PREVIEW_SECONDS)],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit("preview render failed:\n" + result.stderr[-4000:])
    print(result.stdout.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
