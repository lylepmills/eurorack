#!/usr/bin/env python3
"""Render text with Kokoro, then encode it through the continuous Plaits LPC path."""

from __future__ import annotations

import argparse
import random
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import numpy as np
    import soundfile as sf
    import torch
    from kokoro import KPipeline
except ImportError as error:
    raise SystemExit(
        "Kokoro prototype requires: pip install kokoro>=0.9.4 soundfile"
    ) from error

import render_lpc_gate2 as gate2


SCRIPT = Path(__file__).resolve()
CONTINUOUS_SCRIPT = SCRIPT.with_name("render_lpc_continuous.py")
KOKORO_REPOSITORY = "hexgrad/Kokoro-82M"
KOKORO_SAMPLE_RATE = 24000
KOKORO_MODEL_SHA256 = (
    "496dba118d1a58f5f3db2efc88dbdc216e0483fc89fe6e47ee1f2c53f18ad1e4"
)
PUBLISHED_VOICE_SHA256 = {
    "af_heart": "0ab5709b8ffab19bfd849cd11d98f75b60af7733253ad0d67b12382a102cb4ff",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("text", nargs="+", help="text to synthesize and LPC-encode")
    parser.add_argument("--tts-output", required=True, type=Path,
                        help="raw 24 kHz Kokoro waveform")
    parser.add_argument("--reference-output", required=True, type=Path,
                        help="time-matched 48 kHz TTS reference")
    parser.add_argument("--output", required=True, type=Path,
                        help="48 kHz Plaits LPC reconstruction")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--plan-output", type=Path,
                        help="retain the generated LPC frame plan")
    parser.add_argument("--voice", default="af_heart")
    parser.add_argument("--language", default="a",
                        help="Kokoro language code (default: a, American English)")
    parser.add_argument("--speed", type=float, default=1.0)
    parser.add_argument("--formant-semitones", type=float, default=0.0)
    parser.add_argument("--pitch-scale", type=float, default=1.0)
    parser.add_argument("--prosody-amount", type=float, default=1.0)
    parser.add_argument("--gain", type=float, default=1.0)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--renderer", type=Path, default=gate2.DEFAULT_RENDERER)
    parser.add_argument("--rebuild-renderer", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    text = " ".join(args.text).strip()
    if not text:
        raise SystemExit("provide text to synthesize")
    if not 0.5 <= args.speed <= 2.0:
        raise SystemExit("--speed must be between 0.5 and 2")

    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.set_num_threads(1)
    torch.use_deterministic_algorithms(True)
    pipeline = KPipeline(lang_code=args.language, repo_id=KOKORO_REPOSITORY)
    chunks = [
        np.asarray(audio, dtype=np.float32)
        for _, _, audio in pipeline(text, voice=args.voice, speed=args.speed)
    ]
    if not chunks:
        raise SystemExit("Kokoro produced no audio")
    separator = np.zeros(round(0.1 * KOKORO_SAMPLE_RATE), dtype=np.float32)
    joined = []
    for index, chunk in enumerate(chunks):
        if index:
            joined.append(separator)
        joined.append(chunk)
    audio = np.concatenate(joined)
    args.tts_output.parent.mkdir(parents=True, exist_ok=True)
    sf.write(args.tts_output, audio, KOKORO_SAMPLE_RATE, subtype="PCM_16")

    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as encoder_report:
        encoder_report_path = Path(encoder_report.name)
    command = [
        sys.executable, str(CONTINUOUS_SCRIPT), str(args.tts_output),
        "--output", str(args.output),
        "--reference-output", str(args.reference_output),
        "--report", str(encoder_report_path),
        "--renderer", str(args.renderer),
        "--formant-semitones", str(args.formant_semitones),
        "--pitch-scale", str(args.pitch_scale),
        "--prosody-amount", str(args.prosody_amount),
        "--gain", str(args.gain),
    ]
    if args.rebuild_renderer:
        command.append("--rebuild-renderer")
    if args.plan_output:
        command.extend(["--plan-output", str(args.plan_output)])
    try:
        subprocess.run(command, check=True)
        encoder_text = encoder_report_path.read_text(encoding="utf-8")
    finally:
        encoder_report_path.unlink(missing_ok=True)

    report = (
        "Kokoro text -> waveform -> continuous Plaits LPC frames\n"
        f"Repository: {KOKORO_REPOSITORY}\n"
        f"Published model SHA256: {KOKORO_MODEL_SHA256}\n"
        f"Published voice SHA256: {PUBLISHED_VOICE_SHA256.get(args.voice, '(not recorded)')}\n"
        f"Voice: {args.voice}; language: {args.language}; speed: {args.speed:.2f}; "
        f"seed: {args.seed}\n"
        f'Text: "{text}"\n'
        f"TTS waveform: {args.tts_output}\n"
        + encoder_text
    )
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    print(report, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
