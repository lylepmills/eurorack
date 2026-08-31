#!/usr/bin/env python3
"""Split one paused recording into a Natural Speech word bank.

The sibling of encode_recording.py. Word SEGMENTATION is the hard part and is
format-agnostic -- energy framing, the adaptive quiet threshold, the gap and
edge-padding rules, level normalisation -- so it is imported from there rather
than forked. Only the two format-specific halves differ: words are encoded to
NSH1 by the WORLD analyser instead of to LPC10, and previewed through the
shipped engine instead of the LPC renderer.

Without this, "Use my voice" sent recordings to the LPC encoder no matter which
engine the bank was for, so a Natural Speech user recorded their voice and heard
the old Speech engine play it back.

    encode_natural_speech_recording.py --repo <workspace> --source <wav>
        --output-dir <dir>
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

from encode_natural_speech_bank import PREVIEW_SECONDS, build_preview_renderer
from encode_recording import (
    EDGE_PADDING_FRAMES,
    FRAME_RATE,
    MAX_SECONDS,
    MIN_GAP_FRAMES,
    concatenate,
    normalize_recording_level,
    segment_word_bounds,
)
from natural_speech_banks import (
    render_natural_speech_config,
    validate_natural_speech_banks,
)
from natural_speech_encode import encode_bank
from preview_artifacts import write_json_atomic

BYTES_PER_FRAME = 23
FRAME_RATE_HZ = 40.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    builder_dir = Path(__file__).resolve().parent
    sys.path.insert(0, str(builder_dir))
    import render_lpc_continuous as continuous  # noqa: E402
    import render_lpc_gate2 as gate2  # noqa: E402

    args.output_dir.mkdir(parents=True, exist_ok=True)
    # Read and segment exactly as the LPC path does, so a recording splits into
    # the same words whichever engine it is destined for.
    samples = continuous.read_source(args.source)
    if len(samples) > MAX_SECONDS * gate2.SOURCE_RATE:
        raise ValueError(f"Keep recordings to {MAX_SECONDS} seconds or less")

    bounds, threshold_db = segment_word_bounds(samples, gate2.SOURCE_RATE)
    samples, normalization = normalize_recording_level(
        samples, bounds, gate2.SOURCE_RATE)
    samples_per_frame = gate2.SOURCE_RATE // FRAME_RATE

    sources: list[tuple[str, Path]] = []
    for index, word_bounds in enumerate(bounds):
        word = samples[
            word_bounds.start * samples_per_frame:word_bounds.end * samples_per_frame
        ].copy()
        path = args.output_dir / f"recording-word-{index:02d}-source.wav"
        sf.write(path, word, gate2.SOURCE_RATE, subtype="PCM_16")
        # The label is positional: a recording carries no text, and the editor
        # renames these itself (recordedSpeechWordLabels).
        sources.append((f"Recorded word {index + 1}", path))

    if not sources:
        raise ValueError("No words were found in the recording")

    # The whole-bank "Source" preview, the counterpart of the per-word one. The
    # natural and flat bank previews are concatenated by the preview renderer,
    # which only ever sees encoded frames -- nothing downstream of it knows the
    # untouched recording, so the source bank has to be assembled here or the
    # button has no audio to play at all.
    source_bank = args.output_dir / "bank-source.wav"
    concatenate([path for _, path in sources], source_bank, gate2.SOURCE_RATE)

    bank = encode_bank(sources)
    # Validate our own output against the recipe contract before the editor can
    # save it, the same as the text path does.
    validated = validate_natural_speech_banks({"customBanks": [bank]})

    config_path = args.output_dir / "recipe_config.h"
    config_path.write_text(render_natural_speech_config(validated), encoding="utf-8")
    binary = args.output_dir / "render_preview"
    build_preview_renderer(args.repo, config_path, binary)
    result = subprocess.run(
        [str(binary), str(args.output_dir), str(PREVIEW_SECONDS)],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit("preview render failed:\n" + result.stderr[-4000:])

    boundaries = bank["wordBoundaries"]
    entries = []
    for index in range(len(sources)):
        frames = boundaries[index + 1] - boundaries[index]
        entries.append({
            "index": index,
            "frames": frames,
            # NSH1 inserts no inter-word guard frames; the editor renders
            # "frames + guards", so zero keeps that display honest.
            "guardFrames": 0,
            "frameBytes": frames * BYTES_PER_FRAME,
            "durationSeconds": round(frames / FRAME_RATE_HZ, 3),
            "files": {
                "source": f"recording-word-{index:02d}-source.wav",
                "natural": f"word-{index:02d}-natural.wav",
                "flat": f"word-{index:02d}-flat.wav",
            },
        })

    manifest = {
        "format": "rubato.plaits-natural-speech-recording-preview/v1",
        "entries": entries,
        "bank": bank,
        "bankFiles": {
            "source": source_bank.name,
            "natural": "bank-natural.wav",
            "flat": "bank-flat.wav",
        },
        "totals": {
            "words": len(entries),
            "frames": boundaries[-1],
            "guardFrames": 0,
            "frameBytes": boundaries[-1] * BYTES_PER_FRAME,
            "durationSeconds": round(boundaries[-1] / FRAME_RATE_HZ, 3),
        },
        "segmentation": {
            # Derived from the same constants the segmentation actually used,
            # not restated -- these are reported to the user as the rules that
            # split their recording.
            "quietThresholdDb": round(float(threshold_db), 1),
            "minimumGapSeconds": MIN_GAP_FRAMES / FRAME_RATE,
            "edgePaddingSeconds": EDGE_PADDING_FRAMES / FRAME_RATE,
        },
        "normalization": normalization,
        "sampleRate": gate2.SOURCE_RATE,
    }
    write_json_atomic(args.output_dir / "recording-manifest.json", manifest)
    print(json.dumps(manifest["totals"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
