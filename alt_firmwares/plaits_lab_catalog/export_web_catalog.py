#!/usr/bin/env python3
"""Export the public, content-addressed catalog consumed by Plaits Lab."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from validate_catalog import load_catalog, validate_catalog, web_catalog


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    catalog = load_catalog()
    validate_catalog(catalog)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(web_catalog(catalog), indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(catalog['engines'])} packages to {args.output}")


if __name__ == "__main__":
    main()
