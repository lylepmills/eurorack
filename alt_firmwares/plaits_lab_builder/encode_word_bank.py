#!/usr/bin/env python3
"""Generate per-word and whole-bank Kokoro/Plaits LPC previews for the local UI."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

from preview_artifacts import (
    KOKORO_REPOSITORY,
    PUBLISHED_MODEL_SHA256,
    TtsArtifactSession,
    content_key,
    link_or_copy,
    write_json_atomic,
)


FRAME_BYTES = 14
FRAME_RATE = 40
LPC_ARTIFACT_REVISION = "continuous-lpc-v1-voicing60-silence42-energy70-center100"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--request", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--artifact-cache", required=True, type=Path)
    return parser.parse_args()


def trim_plan(
    plan: list[list[int]],
    samples: np.ndarray,
    source_rate: int,
) -> tuple[list[list[int]], np.ndarray]:
    active = [index for index, frame in enumerate(plan) if frame[0] != 0]
    if not active:
        raise ValueError("LPC analysis produced no active speech frames")
    first = max(0, active[0] - 1)
    last = min(len(plan), active[-1] + 2)
    samples_per_frame = source_rate // FRAME_RATE
    return (
        plan[first:last],
        samples[first * samples_per_frame:last * samples_per_frame],
    )


def write_plan(path: Path, plan: list[list[int]]) -> None:
    path.write_text(
        "".join(" ".join(str(value) for value in frame) + "\n" for frame in plan),
        encoding="utf-8",
    )


def render(renderer: Path, plan: Path, output: Path, prosody: float) -> None:
    subprocess.run([
        str(renderer), str(output), str(prosody), str(plan),
    ], check=True, text=True, capture_output=True)


def lpc_artifact(
    cache_root: Path,
    source_path: Path,
    continuous: object,
    gate2: object,
    renderer: Path,
) -> tuple[Path, dict[str, object], bool]:
    source_sha256 = hashlib.sha256(source_path.read_bytes()).hexdigest()
    key = content_key({"revision": LPC_ARTIFACT_REVISION, "sourceSha256": source_sha256})
    artifact_dir = cache_root / "lpc" / key
    manifest_path = artifact_dir / "manifest.json"
    required = [artifact_dir / name for name in ("source.wav", "natural.wav", "flat.wav", "frames.plan")]
    if manifest_path.is_file() and all(path.is_file() for path in required):
        return artifact_dir, json.loads(manifest_path.read_text(encoding="utf-8")), True

    artifact_dir.mkdir(parents=True, exist_ok=True)
    samples = continuous.read_source(source_path)
    plan, stats = continuous.make_plan(
        samples,
        voicing_threshold=0.60,
        silence_db=-42.0,
        energy_scale=0.70,
        contour_center_hz=100.0,
    )
    plan, trimmed_samples = trim_plan(plan, samples, gate2.SOURCE_RATE)
    plan_output = artifact_dir / "frames.plan"
    source_output = artifact_dir / "source.wav"
    natural_output = artifact_dir / "natural.wav"
    flat_output = artifact_dir / "flat.wav"
    write_plan(plan_output, plan)
    output_samples = len(plan) * (continuous.OUTPUT_RATE // FRAME_RATE)
    continuous.write_reference(source_output, trimmed_samples, output_samples, 1.0)
    render(renderer, plan_output, natural_output, 1.0)
    render(renderer, plan_output, flat_output, 0.0)
    manifest = {
        "key": key,
        "sourceSha256": source_sha256,
        "frames": len(plan),
        "durationSeconds": len(plan) / FRAME_RATE,
        "frameBytes": (len(plan) + 1) * FRAME_BYTES,
        "sourceF0Median": round(float(stats["source_f0_median"]), 1),
    }
    write_json_atomic(manifest_path, manifest)
    return artifact_dir, manifest, False


def main() -> int:
    args = parse_args()
    test_dir = Path(__file__).resolve().parent
    if not (test_dir / "render_lpc_continuous.py").is_file():
        raise SystemExit(f"missing continuous LPC pipeline in {test_dir}")
    sys.path.insert(0, str(test_dir))
    import render_lpc_continuous as continuous  # pylint: disable=import-outside-toplevel
    import render_lpc_gate2 as gate2  # pylint: disable=import-outside-toplevel

    request = json.loads(args.request.read_text(encoding="utf-8"))
    language = request["language"]
    voice = request["synthesis"]["voice"]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.artifact_cache.mkdir(parents=True, exist_ok=True)

    renderer = gate2.DEFAULT_RENDERER
    gate2.build_renderer(renderer, False)
    tts_session = TtsArtifactSession(args.artifact_cache, language, voice)
    entries = []
    bank_audio: dict[str, list[np.ndarray]] = {"natural": [], "flat": []}
    source_cache_hits = 0
    lpc_cache_hits = 0
    voice_sha256 = ""
    for index, entry in enumerate(request["entries"]):
        word = entry["word"]
        spoken_as = entry.get("spokenAs", "")
        speech_text = spoken_as or word
        prefix = f"entry-{index:02d}"
        source_output = args.output_dir / f"{prefix}-source.wav"
        natural_output = args.output_dir / f"{prefix}-natural.wav"
        flat_output = args.output_dir / f"{prefix}-flat.wav"
        plan_output = args.output_dir / f"{prefix}.plan"

        tts_source, tts_manifest, source_hit = tts_session.source_artifact(
            speech_text,
            trim_token_edges=language in {"en-US", "en-GB"},
        )
        artifact_dir, lpc_manifest, lpc_hit = lpc_artifact(
            args.artifact_cache, tts_source, continuous, gate2, renderer)
        source_cache_hits += int(source_hit)
        lpc_cache_hits += int(lpc_hit)
        voice_sha256 = str(tts_manifest["publishedVoiceSha256"])
        link_or_copy(artifact_dir / "source.wav", source_output)
        link_or_copy(artifact_dir / "natural.wav", natural_output)
        link_or_copy(artifact_dir / "flat.wav", flat_output)
        link_or_copy(artifact_dir / "frames.plan", plan_output)
        for mode, output in (("natural", natural_output), ("flat", flat_output)):
            rendered, rendered_rate = sf.read(output, dtype="float32")
            if rendered_rate != continuous.OUTPUT_RATE:
                raise ValueError(f"unexpected renderer sample rate: {rendered_rate}")
            bank_audio[mode].append(np.asarray(rendered, dtype=np.float32))

        entries.append({
            "index": index,
            "word": word,
            "spokenAs": spoken_as,
            "frames": lpc_manifest["frames"],
            "durationSeconds": lpc_manifest["durationSeconds"],
            "frameBytes": lpc_manifest["frameBytes"],
            "sourceF0Median": lpc_manifest["sourceF0Median"],
            "cache": {"source": source_hit, "lpc": lpc_hit},
            "files": {
                "source": source_output.name,
                "natural": natural_output.name,
                "flat": flat_output.name,
            },
        })

    bank_files = {}
    for mode, chunks in bank_audio.items():
        output = args.output_dir / f"bank-{mode}.wav"
        sf.write(output, np.concatenate(chunks), continuous.OUTPUT_RATE, subtype="PCM_16")
        bank_files[mode] = output.name

    manifest = {
        "format": "rubato.plaits-lpc-word-bank-preview/v1",
        "synthesis": {
            "language": language,
            "voice": voice,
            "repository": KOKORO_REPOSITORY,
            "publishedModelSha256": PUBLISHED_MODEL_SHA256,
            "publishedVoiceSha256": voice_sha256,
            "referencePitchHz": 100,
            "sampleRate": 48000,
        },
        "bankFiles": bank_files,
        "cache": {
            "sourceHits": source_cache_hits,
            "lpcHits": lpc_cache_hits,
            "entries": len(entries),
        },
        "entries": entries,
        "totals": {
            "frames": sum(entry["frames"] for entry in entries),
            "guardFrames": len(entries),
            "frameBytes": sum(entry["frameBytes"] for entry in entries),
            "durationSeconds": round(sum(entry["durationSeconds"] for entry in entries), 3),
        },
    }
    write_json_atomic(args.output_dir / "manifest.json", manifest)
    print(json.dumps(manifest["totals"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
