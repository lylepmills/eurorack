#!/usr/bin/env python3
"""Run the hosted builder's post-link safety gates on a local Plaits build."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from container_server import BuildError, validate_linked_firmware


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check a linked Plaits firmware before installing it."
    )
    parser.add_argument("config", type=Path, help="generated engine_config.h")
    parser.add_argument("elf", type=Path, help="linked plaits.elf")
    arguments = parser.parse_args()

    try:
        config_text = arguments.config.read_text(encoding="utf-8")
        result = validate_linked_firmware(arguments.elf, config_text)
    except BuildError as error:
        print(f"Safety check failed: {error.message}", file=sys.stderr)
        if error.detail:
            print(error.detail, file=sys.stderr)
        return 1
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        print(f"Safety check could not run: {error}", file=sys.stderr)
        return 1

    print(
        "Safety checks passed: "
        f"flash {result['flashBytes']} bytes, "
        f"RAM {result['bssBytes']} bytes + 1024-byte stack reserve, "
        f"rewritable user-data regions {result['userDataRegions']}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
