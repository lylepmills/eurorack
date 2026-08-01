#!/usr/bin/env python3
"""Continuously analyze one recording and resynthesize it with Plaits LPC.

This is a control experiment for the custom-word pipeline: it deliberately
does not use phone labels, CMUdict, median trajectories, or concatenation.
"""

from __future__ import annotations

import argparse
import math
import re
import subprocess
import tempfile
import wave
from pathlib import Path

try:
    import numpy as np
except ImportError as error:
    raise SystemExit("continuous LPC analysis requires numpy") from error

import render_lpc_gate2 as gate2


FRAME_RATE = 40
ANALYSIS_RATE = 8000
OUTPUT_RATE = 48000
FRAME_SIZE = ANALYSIS_RATE // FRAME_RATE
LPC_ORDER = 10
MIN_PERIOD = 32
MAX_PERIOD = 133

# The Plaits pulse table is read at indices 0, 32, ... 608. These are the
# corresponding normalized samples. The synth multiplies both pulse and noise
# excitation by 1.5 before the lattice filter.
PULSE = np.array([
    0.0, 0.375, 0.9921875, 0.0078125, -0.234375,
    -0.2734375, -0.1953125, -0.2265625, -0.5, -0.1484375,
    -0.1015625, 0.1640625, 0.1015625, 0.265625, 0.1640625,
    0.203125, 0.0859375, 0.046875, -0.0546875, -0.015625,
], dtype=np.float64)
PULSE_ENERGY = float(np.dot(PULSE, PULSE))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="16 kHz mono CMU Arctic WAV")
    parser.add_argument("--output", required=True, type=Path,
                        help="48 kHz Plaits LPC reconstruction")
    parser.add_argument("--reference-output", required=True, type=Path,
                        help="time-matched 48 kHz source reference")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--renderer", type=Path, default=gate2.DEFAULT_RENDERER)
    parser.add_argument("--rebuild-renderer", action="store_true")
    parser.add_argument("--formant-semitones", type=float, default=0.0)
    parser.add_argument("--pitch-scale", type=float, default=1.0,
                        help="multiply the analyzed pitch contour (default: 1)")
    parser.add_argument("--voicing-threshold", type=float, default=0.60)
    parser.add_argument("--silence-db", type=float, default=-42.0,
                        help="silence threshold relative to p95 frame RMS")
    parser.add_argument("--energy-scale", type=float, default=0.70,
                        help="residual-to-Plaits excitation calibration")
    parser.add_argument("--gain", type=float, default=1.0,
                        help="gain applied equally to reference and reconstruction")
    return parser.parse_args()


def downsample(samples: np.ndarray) -> np.ndarray:
    taps = gate2.lowpass_taps()
    margin = len(taps) // 2
    padded = np.pad(samples, (margin, margin), mode="reflect")
    return np.convolve(padded, taps, mode="valid")[::2]


def normalized_autocorrelation(samples: np.ndarray, lag: int) -> float:
    left = samples[:-lag]
    right = samples[lag:]
    denominator = math.sqrt(float(np.dot(left, left) * np.dot(right, right)))
    return float(np.dot(left, right)) / max(denominator, 1.0e-12)


def analyze_frame(samples: np.ndarray) -> dict[str, object] | None:
    centered = samples - np.mean(samples)
    rms = float(np.sqrt(np.mean(centered * centered)))
    windowed = centered * np.hamming(len(centered))
    autocorrelation = np.array([
        np.dot(windowed[:len(windowed) - lag], windowed[lag:])
        for lag in range(LPC_ORDER + 1)
    ])
    if autocorrelation[0] < 1.0e-10:
        return None

    coefficients = np.array([1.0])
    error = float(autocorrelation[0])
    reflection = []
    for order in range(1, LPC_ORDER + 1):
        correction = sum(
            coefficients[index] * autocorrelation[order - index]
            for index in range(1, order)
        )
        value = -(autocorrelation[order] + correction) / max(error, 1.0e-12)
        value = float(np.clip(value, -0.98, 0.98))
        updated = np.empty(order + 1)
        updated[0] = 1.0
        updated[order] = value
        for index in range(1, order):
            updated[index] = coefficients[index] + value * coefficients[order - index]
        coefficients = updated
        error *= max(1.0e-5, 1.0 - value * value)
        reflection.append(value)

    residual = centered[LPC_ORDER:].copy()
    for order in range(1, LPC_ORDER + 1):
        residual += coefficients[order] * centered[LPC_ORDER - order:-order]
    residual_rms = float(np.sqrt(np.mean(residual * residual)))

    correlations = np.array([
        normalized_autocorrelation(centered, lag)
        for lag in range(MIN_PERIOD, MAX_PERIOD + 1)
    ])
    # The largest autocorrelation peak is often the second pitch period. Pick
    # the earliest local peak that is effectively tied with the global one.
    peak_indices = [
        index for index in range(len(correlations))
        if (index == 0 or correlations[index] >= correlations[index - 1])
        and (index + 1 == len(correlations) or correlations[index] >= correlations[index + 1])
    ]
    maximum = float(np.max(correlations))
    near_maximum = [
        index for index in peak_indices
        if correlations[index] >= maximum * 0.95
    ]
    period_index = min(near_maximum) if near_maximum else int(np.argmax(correlations))
    period = period_index + MIN_PERIOD
    periodicity = float(correlations[period - MIN_PERIOD])
    return {
        "rms": rms,
        "residual_rms": residual_rms,
        "reflection": reflection,
        "period": period,
        "periodicity": periodicity,
    }


def quantized_coefficients(reflection: list[float]) -> list[int]:
    values = [
        int(np.clip(round(reflection[0] * 32768.0), -32768, 32767)),
        int(np.clip(round(reflection[1] * 32768.0), -32768, 32767)),
    ]
    values.extend(
        int(np.clip(round(value * 128.0), -128, 127))
        for value in reflection[2:]
    )
    return values


def excitation_energy(
    residual_rms: float,
    period: int,
    voiced: bool,
    energy_scale: float,
) -> int:
    if voiced:
        unit_rms = 1.5 * math.sqrt(PULSE_ENERGY / period)
    else:
        unit_rms = 1.5
    return int(np.clip(round(256.0 * residual_rms / unit_rms * energy_scale), 1, 255))


def make_plan(
    samples: np.ndarray,
    voicing_threshold: float,
    silence_db: float,
    energy_scale: float,
) -> tuple[list[list[int]], dict[str, float]]:
    analyzed = downsample(samples)
    frame_count = math.ceil(len(analyzed) / FRAME_SIZE)
    padded = np.pad(analyzed, (0, frame_count * FRAME_SIZE - len(analyzed)))
    features = [
        analyze_frame(padded[index:index + FRAME_SIZE])
        for index in range(0, len(padded), FRAME_SIZE)
    ]
    frame_rms = [float(item["rms"]) for item in features if item is not None]
    reference_rms = float(np.percentile(frame_rms, 95))
    silence_rms = reference_rms * 10.0 ** (silence_db / 20.0)

    plan = []
    previous = [0] * 12
    voiced_count = 0
    unvoiced_count = 0
    silence_count = 0
    energy_values = []
    periods = []
    for item in features:
        if item is None or float(item["rms"]) < silence_rms:
            frame = previous[:]
            frame[0] = 0
            silence_count += 1
        else:
            voiced = float(item["periodicity"]) >= voicing_threshold
            period = int(item["period"]) if voiced else 0
            energy = excitation_energy(
                float(item["residual_rms"]), period, voiced, energy_scale)
            coefficients = quantized_coefficients(item["reflection"])
            if not voiced:
                # TI unvoiced frames update K0..K3 only. Preserve K4..K9 just
                # as LPCSpeechSynthWordBank::LoadNextWord does.
                coefficients[4:] = previous[6:]
            frame = [energy, period, *coefficients]
            energy_values.append(energy)
            if voiced:
                voiced_count += 1
                periods.append(period)
            else:
                unvoiced_count += 1
        plan.append(frame)
        previous = frame

    stats = {
        "frames": float(frame_count),
        "duration": frame_count / FRAME_RATE,
        "reference_rms": reference_rms,
        "silence_rms": silence_rms,
        "voiced_frames": float(voiced_count),
        "unvoiced_frames": float(unvoiced_count),
        "silence_frames": float(silence_count),
        "energy_min": float(min(energy_values, default=0)),
        "energy_max": float(max(energy_values, default=0)),
        "period_min": float(min(periods, default=0)),
        "period_max": float(max(periods, default=0)),
    }
    return plan, stats


def write_reference(path: Path, samples: np.ndarray, output_samples: int, gain: float) -> None:
    source_x = np.arange(len(samples), dtype=np.float64)
    output_x = np.arange(output_samples, dtype=np.float64) / (OUTPUT_RATE / gate2.SOURCE_RATE)
    upsampled = np.interp(output_x, source_x, samples, left=0.0, right=0.0) * gain
    pcm = np.clip(np.round(upsampled * 32767.0), -32768, 32767).astype("<i2")
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(OUTPUT_RATE)
        output.writeframes(pcm.tobytes())


def transcript(source: Path) -> str | None:
    utterance = source.parent.parent / "festival" / "utts" / f"{source.stem}.utt"
    if not utterance.is_file():
        return None
    match = re.search(r'iform "\\"(.*?)\\""', utterance.read_text(encoding="latin-1"))
    return match.group(1) if match else None


def main() -> int:
    args = parse_args()
    if not -18.0 <= args.formant_semitones <= 18.0:
        raise SystemExit("--formant-semitones must be between -18 and 18")
    if not 0.5 <= args.pitch_scale <= 3.0:
        raise SystemExit("--pitch-scale must be between 0.5 and 3")
    if not 0.0 <= args.voicing_threshold <= 1.0:
        raise SystemExit("--voicing-threshold must be between 0 and 1")
    if not -80.0 <= args.silence_db <= -12.0:
        raise SystemExit("--silence-db must be between -80 and -12")
    if not 0.1 <= args.energy_scale <= 4.0:
        raise SystemExit("--energy-scale must be between 0.1 and 4")
    if not 0.0 <= args.gain <= 2.0:
        raise SystemExit("--gain must be between 0 and 2")

    samples = gate2.read_wave(args.source)
    plan, stats = make_plan(
        samples, args.voicing_threshold, args.silence_db, args.energy_scale)
    gate2.build_renderer(args.renderer, args.rebuild_renderer)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", suffix=".plan", delete=False) as plan_file:
        plan_path = Path(plan_file.name)
        for frame in plan:
            plan_file.write(" ".join(str(value) for value in frame) + "\n")
    try:
        command = [
            str(args.renderer), str(plan_path), str(args.output),
            str(args.formant_semitones), str(100.0 * args.pitch_scale),
            "1.0", str(args.gain),
        ]
        rendered = subprocess.run(command, check=True, text=True, capture_output=True)
    finally:
        plan_path.unlink(missing_ok=True)

    output_samples = len(plan) * (OUTPUT_RATE // FRAME_RATE)
    write_reference(args.reference_output, samples, output_samples, args.gain)
    lines = [
        "Continuous CMU Arctic recording -> frame-by-frame LPC -> Plaits LPC synth",
        f"Source: {args.source}",
    ]
    source_text = transcript(args.source)
    if source_text:
        lines.append(f'Text: "{source_text}"')
    lines.extend([
        f"Reference: {args.reference_output}",
        f"Reconstruction: {args.output}",
        rendered.stdout.strip(),
        f"Frames: {int(stats['frames'])} ({stats['duration']:.3f} s); "
        f"voiced {int(stats['voiced_frames'])}, unvoiced {int(stats['unvoiced_frames'])}, "
        f"silence {int(stats['silence_frames'])}",
        f"Energy bytes: {int(stats['energy_min'])}..{int(stats['energy_max'])}; "
        f"voiced periods: {int(stats['period_min'])}..{int(stats['period_max'])}",
        f"Silence threshold: {args.silence_db:.1f} dB from p95 frame RMS; "
        f"voicing threshold: {args.voicing_threshold:.2f}; "
        f"energy scale: {args.energy_scale:.2f}",
    ])
    report = "\n".join(lines) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    print(report, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
