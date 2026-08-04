#!/usr/bin/env python3
"""Split one paused speech recording into a bank of Plaits LPC previews."""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf


FRAME_BYTES = 14
FRAME_RATE = 40
MAX_SECONDS = 15
MAX_WORDS = 16
MIN_GAP_FRAMES = 7
MIN_WORD_FRAMES = 3
EDGE_PADDING_FRAMES = 2
BANK_GAP_SECONDS = 0.1
TARGET_SPEECH_FRAME_RMS = 0.12
MAX_NORMALIZATION_GAIN = 8.0
MIN_NORMALIZATION_GAIN = 0.125
NORMALIZED_PEAK_CEILING = 0.90
PEAK_PERCENTILE = 99.9


@dataclass(frozen=True)
class WordBounds:
    """Frame-aligned speech and padded extraction bounds."""

    start: int
    speech_start: int
    speech_end: int
    end: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    return parser.parse_args()


def frame_speech_energy(samples: np.ndarray, source_rate: int) -> np.ndarray:
    """Measure energy above the recording's steady per-frequency noise floor."""
    # Pre-emphasis prevents a strong low-frequency component from dominating the
    # spectrum while retaining consonants and vocal harmonics.
    emphasized = np.empty_like(samples)
    if len(samples):
        emphasized[0] = samples[0] * 0.03
        emphasized[1:] = samples[1:] - 0.97 * samples[:-1]
    samples_per_frame = source_rate // FRAME_RATE
    frame_count = math.ceil(len(emphasized) / samples_per_frame)
    padded = np.pad(emphasized, (0, frame_count * samples_per_frame - len(emphasized)))
    framed = padded.reshape(frame_count, samples_per_frame)
    centered = framed - np.mean(framed, axis=1, keepdims=True)
    power = np.abs(np.fft.rfft(centered * np.hanning(samples_per_frame), axis=1)) ** 2

    # The quietest fifth of frames estimates the stable room spectrum. Spectral
    # subtraction handles broadband hiss and computer/fan noise as well as hum;
    # fourfold over-subtraction prevents random noise variation from becoming
    # false speech islands.
    noise_floor = np.percentile(power, 20, axis=0)
    speech_power = np.maximum(power - 4.0 * noise_floor, 0.0)
    return np.sqrt(np.mean(speech_power, axis=1))


def adaptive_quiet_threshold_db(levels_db: np.ndarray) -> float:
    """Separate the room-tone and speech energy clusters on a relative dB scale."""
    low = float(np.percentile(levels_db, 20))
    high = float(np.percentile(levels_db, 80))
    for _ in range(12):
        midpoint = (low + high) / 2.0
        lower = levels_db[levels_db <= midpoint]
        upper = levels_db[levels_db > midpoint]
        if not len(lower) or not len(upper):
            break
        next_low = float(np.mean(lower))
        next_high = float(np.mean(upper))
        if abs(next_low - low) + abs(next_high - high) < 0.01:
            low, high = next_low, next_high
            break
        low, high = next_low, next_high
    if high - low < 3.0:
        # There is no reliable quiet/speech separation. Keep the recording as
        # one island instead of inventing word breaks from tiny level ripples.
        return float(np.min(levels_db) - 1.0)
    # The old -18 dB upper clamp made a loud-but-steady room tone active by
    # definition. Let the learned midpoint rise almost to the speech peak.
    return float(np.clip((low + high) / 2.0, -36.0, -4.0))


def segment_word_bounds(
    samples: np.ndarray,
    source_rate: int,
    minimum_gap_frames: int = MIN_GAP_FRAMES,
) -> tuple[list[WordBounds], float]:
    """Find speech islands separated by a sustained quiet gap."""
    energy = frame_speech_energy(samples, source_rate)
    peak = float(np.percentile(energy, 95))
    if peak <= 1.0e-7:
        raise ValueError("The microphone recording contains no audible speech")
    levels_db = 20.0 * np.log10(np.maximum(energy, peak * 1.0e-6) / peak)
    threshold_db = adaptive_quiet_threshold_db(levels_db)
    active = levels_db > threshold_db

    islands: list[tuple[int, int]] = []
    cursor = 0
    while cursor < len(active):
        while cursor < len(active) and not active[cursor]:
            cursor += 1
        if cursor >= len(active):
            break
        speech_start = cursor
        quiet_start: int | None = None
        while cursor < len(active):
            if active[cursor]:
                quiet_start = None
            elif quiet_start is None:
                quiet_start = cursor
            if quiet_start is not None and cursor - quiet_start + 1 >= minimum_gap_frames:
                break
            cursor += 1
        speech_end = quiet_start if quiet_start is not None else len(active)
        if speech_end - speech_start >= MIN_WORD_FRAMES:
            islands.append((speech_start, speech_end))
        cursor = speech_end + minimum_gap_frames if quiet_start is not None else len(active)

    if not islands:
        raise ValueError("No clearly separated speech was found in the recording")
    if len(islands) > MAX_WORDS:
        raise ValueError(f"The recording contains {len(islands)} words; keep each bank to {MAX_WORDS}")

    bounds = [
        WordBounds(
            start=max(0, speech_start - EDGE_PADDING_FRAMES),
            speech_start=speech_start,
            speech_end=speech_end,
            end=min(len(active), speech_end + EDGE_PADDING_FRAMES),
        )
        for speech_start, speech_end in islands
    ]
    return bounds, threshold_db


def normalize_recording_level(
    samples: np.ndarray,
    bounds: list[WordBounds],
    source_rate: int,
) -> tuple[np.ndarray, dict[str, float]]:
    """Match one take to the generated-voice level without flattening its words."""
    samples_per_frame = source_rate // FRAME_RATE
    speech_frame_rms: list[float] = []
    extracted_samples: list[np.ndarray] = []
    for word in bounds:
        extracted_samples.append(samples[
            word.start * samples_per_frame:word.end * samples_per_frame
        ])
        for frame_index in range(word.speech_start, word.speech_end):
            frame = samples[
                frame_index * samples_per_frame:(frame_index + 1) * samples_per_frame
            ]
            if len(frame):
                centered = frame - np.mean(frame)
                speech_frame_rms.append(float(np.sqrt(np.mean(centered * centered))))

    if not speech_frame_rms or not extracted_samples:
        raise ValueError("No speech level could be measured in the recording")
    input_speech_rms = float(np.percentile(speech_frame_rms, 95))
    if input_speech_rms <= 1.0e-7:
        raise ValueError("The microphone recording contains no audible speech")

    extracted = np.concatenate(extracted_samples)
    input_peak = float(np.percentile(np.abs(extracted), PEAK_PERCENTILE))
    rms_gain = TARGET_SPEECH_FRAME_RMS / input_speech_rms
    peak_gain = NORMALIZED_PEAK_CEILING / max(input_peak, 1.0e-7)
    gain = float(np.clip(
        min(rms_gain, peak_gain),
        MIN_NORMALIZATION_GAIN,
        MAX_NORMALIZATION_GAIN,
    ))
    normalized = np.clip(
        samples * gain,
        -NORMALIZED_PEAK_CEILING,
        NORMALIZED_PEAK_CEILING,
    ).astype(np.float32)
    return normalized, {
        "inputSpeechFrameRmsP95": input_speech_rms,
        "targetSpeechFrameRmsP95": TARGET_SPEECH_FRAME_RMS,
        "inputPeakP999": input_peak,
        "normalizationGain": gain,
        "normalizationGainDb": 20.0 * math.log10(gain),
        "outputSpeechFrameRmsP95": input_speech_rms * gain,
    }


def clean_word_plan(
    plan: list[list[int]],
    samples: np.ndarray,
    bounds: WordBounds,
    source_rate: int,
) -> tuple[list[list[int]], np.ndarray]:
    """Remove the shared pause and guarantee one silent frame at each word edge."""
    samples_per_frame = source_rate // FRAME_RATE
    pre_padding = bounds.speech_start - bounds.start
    speech_frames = bounds.speech_end - bounds.speech_start
    post_start = pre_padding + speech_frames
    for index in range(min(pre_padding, len(plan))):
        plan[index][0] = 0
    for index in range(min(post_start, len(plan)), len(plan)):
        plan[index][0] = 0

    if pre_padding == 0:
        guard = plan[0][:]
        guard[0] = 0
        plan.insert(0, guard)
        samples = np.pad(samples, (samples_per_frame, 0))
    if bounds.end == bounds.speech_end:
        guard = plan[-1][:]
        guard[0] = 0
        plan.append(guard)
        samples = np.pad(samples, (0, samples_per_frame))

    # The browser reference should demonstrate the same clean edges as the LPC bank.
    samples[:samples_per_frame] = 0.0
    samples[-samples_per_frame:] = 0.0
    return plan, samples


def write_plan(path: Path, plan: list[list[int]]) -> None:
    path.write_text(
        "".join(" ".join(str(value) for value in frame) + "\n" for frame in plan),
        encoding="utf-8",
    )


def render(renderer: Path, plan: Path, output: Path, prosody: float) -> None:
    subprocess.run([
        str(renderer), str(output), str(prosody), str(plan),
    ], check=True, text=True, capture_output=True)


def concatenate(files: list[Path], output: Path, sample_rate: int) -> None:
    gap = np.zeros(round(BANK_GAP_SECONDS * sample_rate), dtype=np.float32)
    pieces: list[np.ndarray] = []
    for index, path in enumerate(files):
        audio, rate = sf.read(path, dtype="float32")
        if rate != sample_rate:
            raise ValueError(f"unexpected preview sample rate: {rate}")
        if index:
            pieces.append(gap)
        pieces.append(np.asarray(audio, dtype=np.float32))
    sf.write(output, np.concatenate(pieces), sample_rate, subtype="PCM_16")


def main() -> int:
    args = parse_args()
    test_dir = Path(__file__).resolve().parent
    if not (test_dir / "render_lpc_continuous.py").is_file():
        raise SystemExit(f"missing continuous LPC pipeline in {test_dir}")
    sys.path.insert(0, str(test_dir))
    import render_lpc_continuous as continuous  # pylint: disable=import-outside-toplevel
    import render_lpc_gate2 as gate2  # pylint: disable=import-outside-toplevel

    args.output_dir.mkdir(parents=True, exist_ok=True)
    samples = continuous.read_source(args.source)
    if len(samples) > MAX_SECONDS * gate2.SOURCE_RATE:
        raise ValueError(f"Keep recordings to {MAX_SECONDS} seconds or less")

    bounds, threshold_db = segment_word_bounds(samples, gate2.SOURCE_RATE)
    samples, normalization = normalize_recording_level(
        samples, bounds, gate2.SOURCE_RATE)
    renderer = gate2.DEFAULT_RENDERER
    gate2.build_renderer(renderer, False)
    entries = []
    bank_files: dict[str, list[Path]] = {"source": [], "natural": [], "flat": []}
    samples_per_frame = gate2.SOURCE_RATE // FRAME_RATE

    for index, word_bounds in enumerate(bounds):
        word_samples = samples[
            word_bounds.start * samples_per_frame:word_bounds.end * samples_per_frame
        ].copy()
        plan, stats = continuous.make_plan(
            word_samples,
            voicing_threshold=0.60,
            silence_db=-42.0,
            energy_scale=0.70,
            contour_center_hz=100.0,
        )
        plan, word_samples = clean_word_plan(
            plan, word_samples, word_bounds, gate2.SOURCE_RATE)
        prefix = f"recording-word-{index:02d}"
        plan_output = args.output_dir / f"{prefix}.plan"
        source_output = args.output_dir / f"{prefix}-source.wav"
        natural_output = args.output_dir / f"{prefix}-natural.wav"
        flat_output = args.output_dir / f"{prefix}-flat.wav"
        write_plan(plan_output, plan)
        output_samples = len(plan) * (continuous.OUTPUT_RATE // FRAME_RATE)
        continuous.write_reference(source_output, word_samples, output_samples, 1.0)
        render(renderer, plan_output, natural_output, 1.0)
        render(renderer, plan_output, flat_output, 0.0)
        bank_files["source"].append(source_output)
        bank_files["natural"].append(natural_output)
        bank_files["flat"].append(flat_output)
        entries.append({
            "index": index,
            "frames": len(plan),
            "guardFrames": 1,
            "frameBytes": (len(plan) + 1) * FRAME_BYTES,
            "durationSeconds": round(len(plan) / FRAME_RATE, 3),
            "sourceF0Median": round(float(stats["source_f0_median"]), 1),
            "files": {
                "source": source_output.name,
                "natural": natural_output.name,
                "flat": flat_output.name,
            },
        })

    combined_files = {}
    for mode, files in bank_files.items():
        output = args.output_dir / f"recording-bank-{mode}.wav"
        concatenate(files, output, continuous.OUTPUT_RATE)
        combined_files[mode] = output.name

    total_frames = sum(entry["frames"] for entry in entries)
    manifest = {
        "format": "rubato.plaits-lpc-recording-bank-preview/v2",
        "entries": entries,
        "bankFiles": combined_files,
        "totals": {
            "words": len(entries),
            "frames": total_frames,
            "guardFrames": len(entries),
            "frameBytes": sum(entry["frameBytes"] for entry in entries),
            "durationSeconds": round(total_frames / FRAME_RATE, 3),
        },
        "segmentation": {
            "quietThresholdDb": round(threshold_db, 1),
            "minimumGapSeconds": MIN_GAP_FRAMES / FRAME_RATE,
            "edgePaddingSeconds": EDGE_PADDING_FRAMES / FRAME_RATE,
        },
        "normalization": {
            key: round(value, 5) for key, value in normalization.items()
        },
        "sampleRate": continuous.OUTPUT_RATE,
    }
    temporary = args.output_dir / "recording-manifest.json.tmp"
    temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, args.output_dir / "recording-manifest.json")
    print(json.dumps(manifest["totals"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
