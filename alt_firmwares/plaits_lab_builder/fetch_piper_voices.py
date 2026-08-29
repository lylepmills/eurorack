#!/usr/bin/env python3
"""Download the Piper voices listed in piper_voices.json into the image.

Run at image build time, when there is still network. At request time the
container has none, so a voice that is not baked in is simply unavailable —
preview_artifacts raises with the path it looked in rather than trying to fetch.

Every file is checked against the md5 recorded in the manifest. These are ~2.4
GB of model weights fetched over the network into an artifact users' firmware
is built from; a silent truncation would surface much later as a voice that
sounds wrong, which is exactly the failure that is hardest to trace back here.

Usage:
  python3 fetch_piper_voices.py --out /opt/piper-voices
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import urllib.parse
import urllib.request
from pathlib import Path

REPOSITORY = "https://huggingface.co/rhasspy/piper-voices/resolve/main"
MANIFEST = Path(__file__).with_name("piper_voices.json")


def md5(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as handle:
        while chunk := handle.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def fetch(relative: str, destination: Path, expected: str) -> bool:
    """Download one file unless a verified copy is already present."""
    if destination.is_file() and md5(destination) == expected:
        return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Percent-encode: pt_PT-tugão-medium carries a non-ASCII character, and
    # urllib encodes request lines as ASCII.
    url = f"{REPOSITORY}/{urllib.parse.quote(relative)}"
    temporary = destination.with_suffix(destination.suffix + ".part")
    with urllib.request.urlopen(url) as response, temporary.open("wb") as handle:
        while chunk := response.read(1 << 20):
            handle.write(chunk)
    actual = md5(temporary)
    if actual != expected:
        temporary.unlink(missing_ok=True)
        raise SystemExit(
            f"{relative}: md5 {actual} does not match the manifest's {expected}")
    temporary.replace(destination)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    models = manifest["models"]
    downloaded = skipped = 0
    total = 0
    for name, model in sorted(models.items()):
        for relative, meta in sorted(model["files"].items()):
            # Flat names in the voice root: preview_artifacts looks for
            # "<model>.onnx" beside "<model>.onnx.json", not the repository's
            # nested language directories.
            target = args.out / Path(relative).name
            if fetch(relative, target, meta["md5"]):
                downloaded += 1
            else:
                skipped += 1
            total += meta["bytes"]
        print(f"  {name}", flush=True)
    print(f"\n{len(models)} voices, {downloaded} fetched, {skipped} already present, "
          f"{total / 1e6:,.0f} MB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
