#!/usr/bin/env python3
"""Encode a Natural Speech word bank, with audible previews.

The sibling of encode_word_bank.py, and deliberately the same shape: take a
request naming words and a voice, synthesize each word through the SAME
TtsArtifactSession the Speech pipeline uses, and emit both the recipe bank and
per-word preview audio for the editor.

Two things differ from the LPC path, both consequences of the format:

  * frames are NSH1 (23 B, WORLD analysis) via natural_speech_encode, not
    14-byte LPC10 -- and encode_bank already returns exactly the shape
    natural_speech_banks.validate_natural_speech_banks accepts, so the encoder
    and the recipe contract cannot drift;

  * previews are rendered by COMPILING THE SHIPPED ENGINE against the very
    header the firmware compiles (render_natural_speech_config), rather than by
    a separate offline renderer. WORLD analysis is where words go wrong -- a
    fricative stored as voiced turns /s/ into /z/ -- and only the real engine
    reveals that. Compiling per request costs a few seconds against a TTS pass
    that costs far more.

Usage:
    encode_natural_speech_bank.py --repo <workspace> --request <json>
        --output-dir <dir> --artifact-cache <dir>
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from natural_speech_banks import (
    render_natural_speech_config,
    validate_natural_speech_banks,
)
from natural_speech_encode import encode_bank
from preview_artifacts import (
    KOKORO_REPOSITORY,
    PUBLISHED_MODEL_SHA256,
    TtsArtifactSession,
    link_or_copy,
    write_json_atomic,
)

PREVIEW_SECONDS = 2.0
BUILDER_DIR = Path(__file__).resolve().parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--artifact-cache", type=Path, required=True)
    return parser.parse_args()


def build_preview_renderer(repo: Path, config: Path, binary: Path) -> None:
    """Compile the preview harness against this bank's generated config.

    Force-including the config is the whole point: it is byte-identical to what
    the firmware build compiles, so a preview cannot describe a synthesis the
    device will not produce.
    """
    sources = [
        BUILDER_DIR / "render_natural_speech_preview.cc",
        repo / "plaits" / "dsp" / "engine2" / "natural_speech_engine.cc",
        repo / "plaits" / "resources.cc",
        repo / "stmlib" / "utils" / "random.cc",
        repo / "stmlib" / "dsp" / "units.cc",
    ]
    missing = [str(s) for s in sources if not s.is_file()]
    if missing:
        raise SystemExit(f"missing preview sources: {', '.join(missing)}")
    command = [
        "g++", "-std=c++11", "-O2", "-DTEST",
        f"-I{repo}",
        "-include", str(config),
        *[str(s) for s in sources],
        "-o", str(binary),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(
            "preview renderer failed to build:\n" + result.stderr[-4000:])


def main() -> int:
    args = parse_args()
    request = json.loads(args.request.read_text(encoding="utf-8"))
    language = request["language"]
    voice = request["synthesis"]["voice"]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.artifact_cache.mkdir(parents=True, exist_ok=True)

    tts_session = TtsArtifactSession(args.artifact_cache, language, voice)
    sources: list[tuple[str, Path]] = []
    entries = []
    source_cache_hits = 0
    voice_sha256 = ""
    for index, entry in enumerate(request["entries"]):
        word = entry["word"]
        spoken_as = entry.get("spokenAs", "")
        prefix = f"entry-{index:02d}"
        source_output = args.output_dir / f"{prefix}-source.wav"

        tts_source, tts_manifest, source_hit = tts_session.source_artifact(
            spoken_as or word,
            trim_token_edges=language in {"en-US", "en-GB"},
        )
        source_cache_hits += int(source_hit)
        voice_sha256 = str(tts_manifest["publishedVoiceSha256"])
        link_or_copy(tts_source, source_output)
        sources.append((word, source_output))
        entries.append({
            "index": index,
            "word": word,
            "spokenAs": spoken_as,
            "cache": {"source": source_hit},
            "files": {"source": source_output.name},
        })

    # encode_bank raises on a word that produced no frames, which is the
    # failure worth surfacing by name rather than as a malformed bank later.
    bank = encode_bank(sources)
    # Validate our own output against the recipe contract before anyone builds
    # with it: the encoder and the validator are separate code, and a bank that
    # only fails at build time would fail after the user has committed.
    validated = validate_natural_speech_banks({"customBanks": [bank]})

    config_path = args.output_dir / "recipe_config.h"
    config_path.write_text(render_natural_speech_config(validated),
                           encoding="utf-8")
    binary = args.output_dir / "render_preview"
    build_preview_renderer(args.repo, config_path, binary)
    result = subprocess.run(
        [str(binary), str(args.output_dir), str(PREVIEW_SECONDS)],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit("preview render failed:\n" + result.stderr[-4000:])
    preview_totals = json.loads(result.stdout)

    boundaries = bank["wordBoundaries"]
    for index, entry in enumerate(entries):
        entry["frames"] = boundaries[index + 1] - boundaries[index]
        entry["frameBytes"] = entry["frames"] * 23
        entry["files"].update({
            "natural": f"word-{index:02d}-natural.wav",
            "flat": f"word-{index:02d}-flat.wav",
        })

    manifest = {
        "format": "rubato.plaits-natural-speech-word-bank-preview/v1",
        "synthesis": {
            "language": language,
            "voice": voice,
            "repository": KOKORO_REPOSITORY,
            "publishedModelSha256": PUBLISHED_MODEL_SHA256,
            "publishedVoiceSha256": voice_sha256,
            "referencePitchHz": 100,
            "sampleRate": 48000,
        },
        "bankFiles": {"natural": "bank-natural.wav", "flat": "bank-flat.wav"},
        "cache": {
            "sourceHits": source_cache_hits,
            "entries": len(entries),
        },
        "entries": entries,
        "bank": bank,
        "preview": preview_totals,
        "totals": {
            "frames": boundaries[-1],
            "frameBytes": boundaries[-1] * 23,
            "words": len(entries),
        },
    }
    write_json_atomic(args.output_dir / "manifest.json", manifest)
    print(json.dumps(manifest["totals"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
