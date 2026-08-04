#!/usr/bin/env python3
"""Render one source-stage Kokoro voice preview without running LPC analysis."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from preview_artifacts import TtsArtifactSession, link_or_copy, write_json_atomic


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--language", required=True)
    parser.add_argument("--voice", required=True)
    parser.add_argument("--text", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--artifact-cache", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    source, source_manifest, cached = TtsArtifactSession(
        args.artifact_cache, args.language, args.voice).source_artifact(args.text)
    output = args.output_dir / "voice-preview-source.wav"
    link_or_copy(source, output)
    manifest = {
        "format": "rubato.plaits-source-voice-preview/v1",
        "language": args.language,
        "voice": args.voice,
        "text": args.text,
        "cached": cached,
        "file": output.name,
        "sourceArtifact": source_manifest,
    }
    write_json_atomic(args.output_dir / "voice-preview-manifest.json", manifest)
    print(json.dumps({"cached": cached}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
