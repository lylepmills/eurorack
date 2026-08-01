#!/usr/bin/env python3
"""Build a labelled LPC phone bank from CMU Arctic and render CMUdict text.

Gate 2 remains host-only. The generated bank is derived from the external CMU
Arctic SLT corpus and is intentionally kept outside the repository. Corpus use
must retain its own copyright and conditions from the distribution's COPYING.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
import wave
from collections import defaultdict
from pathlib import Path

try:
    import numpy as np
except ImportError as error:
    raise SystemExit("Gate 2 requires numpy for offline LPC analysis") from error


SCRIPT = Path(__file__).resolve()
REPO_ROOT = SCRIPT.parents[2]
CPP_SOURCE = SCRIPT.with_suffix(".cc")
DEFAULT_RENDERER = Path(tempfile.gettempdir()) / "plaits-render-lpc-gate2"
DEFAULT_BANK = Path(tempfile.gettempdir()) / "cmu-arctic-slt-plaits-lpc-bank.json"
CORPUS_COPYRIGHT = "Carnegie Mellon University, Copyright (c) 2003"
CORPUS_RELEASE_SHA256 = "9fddec16fbfbfb7d4989dff0fe77ccbe31f80b07b57be49d09994aa7a67d6dba"
FRAME_RATE = 40
ANALYSIS_RATE = 8000
SOURCE_RATE = 16000
LPC_ORDER = 10
TRAJECTORY_POSITIONS = (0.15, 0.35, 0.55, 0.75, 0.90)
UNVOICED = {"ch", "f", "hh", "k", "p", "s", "sh", "t", "th"}
WORD_RE = re.compile(r"[a-z]+(?:'[a-z]+)?")
PHONE_RE = re.compile(r"^([A-Z]+)([012]?)$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("text", nargs="*", help="English text to render")
    parser.add_argument("--corpus-root", required=True, type=Path,
                        help="extracted cmu_us_slt_arctic directory")
    parser.add_argument("--cmudict", required=True, type=Path,
                        help="path to official cmudict.dict")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--bank", type=Path, default=DEFAULT_BANK,
                        help="generated phone-bank cache (default: temporary directory)")
    parser.add_argument("--rebuild-bank", action="store_true")
    parser.add_argument("--renderer", type=Path, default=DEFAULT_RENDERER)
    parser.add_argument("--rebuild-renderer", action="store_true")
    parser.add_argument("--formant-semitones", type=float, default=9.0)
    parser.add_argument("--pitch-hz", type=float, default=100.0)
    parser.add_argument("--word-gap-ms", type=int, default=125)
    parser.add_argument("--max-occurrences", type=int, default=48,
                        help="deterministic examples per phone (default: 48)")
    return parser.parse_args()


def load_cmudict(path: Path) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    with path.open(encoding="latin-1") as source:
        for line in source:
            line = line.split("#", 1)[0].strip()
            if not line or line.startswith(";;;"):
                continue
            word, *phones = line.split()
            result.setdefault(re.sub(r"\(\d+\)$", "", word.lower()), phones)
    return result


def parse_labels(path: Path) -> list[tuple[str, float, float]]:
    segments = []
    start = 0.0
    with path.open(encoding="ascii") as source:
        for line in source:
            fields = line.split()
            if len(fields) != 3 or fields[0] == "#":
                continue
            end = float(fields[0])
            segments.append((fields[2], start, end))
            start = end
    return segments


def read_wave(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, SOURCE_RATE):
            raise ValueError(f"unexpected WAV format: {path}")
        return np.frombuffer(source.readframes(source.getnframes()), dtype="<i2").astype(np.float64) / 32768.0


def lowpass_taps() -> np.ndarray:
    size = 63
    offset = np.arange(size) - size // 2
    cutoff = 0.225  # 3.6 kHz at the 16 kHz source rate.
    taps = 2.0 * cutoff * np.sinc(2.0 * cutoff * offset) * np.hamming(size)
    return taps / np.sum(taps)


def analysis_window(samples: np.ndarray, center_seconds: float, taps: np.ndarray) -> np.ndarray | None:
    output_size = 200  # 25 ms at 8 kHz.
    center = int(round(center_seconds * SOURCE_RATE))
    first = center - output_size
    last = center + output_size - 2
    margin = len(taps) // 2
    begin = first - margin
    end = last + margin + 1
    if begin < 0 or end > len(samples):
        return None
    filtered = np.convolve(samples[begin:end], taps, mode="valid")
    window = filtered[::2]
    return window if len(window) == output_size else None


def lpc_features(samples: np.ndarray) -> dict[str, object] | None:
    centered = samples - np.mean(samples)
    rms = float(np.sqrt(np.mean(centered * centered)))
    windowed = centered * np.hamming(len(centered))
    autocorrelation = np.array([
        np.dot(windowed[:len(windowed) - lag], windowed[lag:])
        for lag in range(LPC_ORDER + 1)
    ])
    if autocorrelation[0] < 1.0e-8:
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

    denominator = float(np.dot(centered, centered))
    voicing = 0.0
    if denominator > 1.0e-9:
        correlations = [
            float(np.dot(centered[:-lag], centered[lag:])) / denominator
            for lag in range(32, 134)
        ]
        voicing = max(correlations)
    return {"rms": rms, "reflection": reflection, "voicing": voicing}


def evenly_sample(items: list[tuple[str, float, float]], count: int) -> list[tuple[str, float, float]]:
    if len(items) <= count:
        return items
    indices = np.linspace(0, len(items) - 1, count, dtype=int)
    return [items[int(index)] for index in indices]


def collect_occurrences(corpus_root: Path) -> dict[str, list[tuple[str, float, float]]]:
    result: dict[str, list[tuple[str, float, float]]] = defaultdict(list)
    for label_path in sorted((corpus_root / "lab").glob("*.lab")):
        for phone, start, end in parse_labels(label_path):
            duration = end - start
            if phone != "pau" and 0.04 <= duration <= 0.35:
                result[phone].append((label_path.stem, start, end))
    return result


def quantize_frame(phone: str, feature: dict[str, object], energy_scale: float) -> list[int]:
    rms = float(feature["rms"])
    energy = int(round(224.0 * min(1.0, rms / energy_scale) ** 0.65))
    energy = max(3, min(240, energy))
    voiced = phone not in UNVOICED and float(feature["voicing"]) >= 0.18
    reflection = [float(value) for value in feature["reflection"]]
    values = [energy, 80 if voiced else 0]
    values.extend([
        int(np.clip(round(reflection[0] * 32768.0), -32768, 32767)),
        int(np.clip(round(reflection[1] * 32768.0), -32768, 32767)),
    ])
    values.extend(int(np.clip(round(value * 128.0), -128, 127)) for value in reflection[2:])
    return values


def build_bank(corpus_root: Path, max_occurrences: int) -> dict[str, object]:
    if not (corpus_root / "COPYING").is_file():
        raise ValueError(f"missing CMU Arctic COPYING: {corpus_root}")
    if not (corpus_root / "lab").is_dir() or not (corpus_root / "wav").is_dir():
        raise ValueError(f"corpus root must contain lab/ and wav/: {corpus_root}")
    occurrences = collect_occurrences(corpus_root)
    if not occurrences:
        raise ValueError(f"no labelled phone occurrences found: {corpus_root / 'lab'}")
    selected = {
        phone: evenly_sample(items, max_occurrences)
        for phone, items in occurrences.items()
    }
    by_utterance: dict[str, list[tuple[str, float, float]]] = defaultdict(list)
    for phone, items in selected.items():
        for stem, start, end in items:
            by_utterance[stem].append((phone, start, end))

    taps = lowpass_taps()
    features: dict[str, list[list[dict[str, object]]]] = defaultdict(list)
    for stem in sorted(by_utterance):
        samples = read_wave(corpus_root / "wav" / f"{stem}.wav")
        for phone, start, end in by_utterance[stem]:
            trajectory = []
            for position in TRAJECTORY_POSITIONS:
                center = start + (end - start) * position
                window = analysis_window(samples, center, taps)
                feature = lpc_features(window) if window is not None else None
                if feature is None:
                    trajectory = []
                    break
                trajectory.append(feature)
            if trajectory:
                features[phone].append(trajectory)

    all_rms = [
        float(frame["rms"])
        for trajectories in features.values()
        for trajectory in trajectories
        for frame in trajectory
    ]
    if not all_rms:
        raise ValueError("no analysable LPC frames were extracted from the corpus")
    energy_scale = float(np.percentile(all_rms, 95))
    phones = {}
    for phone in sorted(features):
        trajectories = features[phone]
        frames = []
        for position in range(len(TRAJECTORY_POSITIONS)):
            rms = float(np.median([float(item[position]["rms"]) for item in trajectories]))
            voicing = float(np.median([float(item[position]["voicing"]) for item in trajectories]))
            reflection = np.median(np.array([
                item[position]["reflection"] for item in trajectories
            ], dtype=np.float64), axis=0).tolist()
            frames.append(quantize_frame(
                phone,
                {"rms": rms, "voicing": voicing, "reflection": reflection},
                energy_scale,
            ))
        durations = [end - start for _, start, end in selected[phone]]
        phones[phone] = {
            "frames": frames,
            "median_duration": float(np.median(durations)),
            "occurrences": len(trajectories),
        }

    return {
        "format": "plaits-cmu-arctic-lpc-phone-bank-v1",
        "corpus": "cmu_us_slt_arctic-0.95-release",
        "corpus_release_sha256": CORPUS_RELEASE_SHA256,
        "copyright": CORPUS_COPYRIGHT,
        "modified": "Derived 8 kHz, order-10 LPC trajectories; five median frames per labelled phone.",
        "analysis": {
            "frame_rate": FRAME_RATE,
            "positions": list(TRAJECTORY_POSITIONS),
            "max_occurrences": max_occurrences,
            "energy_scale_rms_p95": energy_scale,
        },
        "phones": phones,
    }


def load_or_build_bank(args: argparse.Namespace) -> dict[str, object]:
    if args.bank.exists() and not args.rebuild_bank:
        bank = json.loads(args.bank.read_text(encoding="utf-8"))
        if bank.get("format") != "plaits-cmu-arctic-lpc-phone-bank-v1":
            raise ValueError(f"unsupported bank format: {args.bank}")
        return bank
    bank = build_bank(args.corpus_root, args.max_occurrences)
    args.bank.parent.mkdir(parents=True, exist_ok=True)
    args.bank.write_text(json.dumps(bank, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return bank


def build_renderer(path: Path, force: bool) -> None:
    sources = [
        CPP_SOURCE,
        REPO_ROOT / "plaits/dsp/speech/lpc_speech_synth.cc",
        REPO_ROOT / "plaits/resources.cc",
        REPO_ROOT / "stmlib/utils/random.cc",
    ]
    stale = force or not path.exists() or any(source.stat().st_mtime > path.stat().st_mtime for source in sources)
    if not stale:
        return
    compiler = shutil.which("c++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("no C++ compiler found")
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        compiler, "-std=c++11", "-O2", "-w", "-I", str(REPO_ROOT),
        *(str(source) for source in sources), "-o", str(path),
    ], check=True)


def corpus_phone(cmudict_phone: str) -> tuple[str, str]:
    match = PHONE_RE.match(cmudict_phone)
    if not match:
        raise ValueError(f"unsupported CMUdict phone: {cmudict_phone}")
    symbol, stress = match.groups()
    phone = symbol.lower()
    if symbol == "AH" and stress == "0":
        phone = "ax"
    return phone, stress


def interpolate_frame(frames: list[list[int]], position: float) -> list[int]:
    scaled = position * (len(frames) - 1)
    left = min(int(math.floor(scaled)), len(frames) - 1)
    right = min(left + 1, len(frames) - 1)
    blend = scaled - left
    result = []
    for index in range(12):
        if index == 1:
            result.append(frames[left if blend < 0.5 else right][index])
        else:
            result.append(int(round(frames[left][index] * (1.0 - blend) + frames[right][index] * blend)))
    return result


def make_plan(
    words: list[str], pronunciations: dict[str, list[str]], bank: dict[str, object], gap_ticks: int
) -> tuple[list[list[int]], list[str]]:
    silence = [0] * 12
    plan = [silence[:] for _ in range(4)]
    report = [
        "Gate 2 CMUdict -> CMU Arctic labelled LPC trajectories -> Plaits LPC synth",
        f"Corpus: {bank['corpus']} ({bank['copyright']})",
        str(bank["modified"]),
        "",
    ]
    phone_bank = bank["phones"]
    missing_words = [word for word in words if word not in pronunciations]
    if missing_words:
        raise ValueError("not found in CMUdict: " + ", ".join(sorted(set(missing_words))))
    cursor = len(plan)
    for word_index, word in enumerate(words):
        start = cursor / FRAME_RATE
        mappings = []
        for raw_phone in pronunciations[word]:
            phone, stress = corpus_phone(raw_phone)
            if phone not in phone_bank:
                raise ValueError(f"phone {phone!r} has no extracted trajectory")
            item = phone_bank[phone]
            duration = float(item["median_duration"])
            duration *= 1.12 if stress == "1" else 1.04 if stress == "2" else 0.88 if stress == "0" else 1.0
            # Preserve corpus rhythm. The five-point stored trajectory can be
            # sampled into fewer output frames; forcing every phone to five
            # frames would flatten reduced vowels, stops, and long vowels to
            # the same 125 ms duration.
            ticks = max(2, int(round(duration * FRAME_RATE)))
            for tick in range(ticks):
                position = tick / max(1, ticks - 1)
                plan.append(interpolate_frame(item["frames"], position))
            cursor += ticks
            mappings.append(f"{raw_phone}->{phone}({ticks})")
        end = cursor / FRAME_RATE
        report.append(f"{start:7.3f}-{end:7.3f}  {word:<14} {' '.join(mappings)}")
        gap = gap_ticks if word_index + 1 < len(words) else 4
        plan.extend(silence[:] for _ in range(gap))
        cursor += gap
    return plan, report


def main() -> int:
    args = parse_args()
    if not -18.0 <= args.formant_semitones <= 18.0:
        raise SystemExit("--formant-semitones must be between -18 and 18")
    if not 50.0 <= args.pitch_hz <= 300.0:
        raise SystemExit("--pitch-hz must be between 50 and 300")
    if not 0 <= args.word_gap_ms <= 5000:
        raise SystemExit("--word-gap-ms must be between 0 and 5000")
    text = " ".join(args.text).strip()
    if not text:
        raise SystemExit("provide text to render")
    words = WORD_RE.findall(text.lower())

    try:
        bank = load_or_build_bank(args)
        pronunciations = load_cmudict(args.cmudict)
        gap_ticks = max(1, round(args.word_gap_ms * FRAME_RATE / 1000))
        plan, report = make_plan(words, pronunciations, bank, gap_ticks)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error

    build_renderer(args.renderer, args.rebuild_renderer)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", suffix=".plan", delete=False) as plan_file:
        plan_path = Path(plan_file.name)
        for frame in plan:
            plan_file.write(" ".join(str(value) for value in frame) + "\n")
    try:
        subprocess.run([
            str(args.renderer), str(plan_path), str(args.output),
            str(args.formant_semitones), str(args.pitch_hz),
        ], check=True)
    finally:
        plan_path.unlink(missing_ok=True)

    report.insert(1, f"Formant {args.formant_semitones:+.1f} semitones; pitch {args.pitch_hz:.1f} Hz")
    report_text = "\n".join(report) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report_text, encoding="utf-8")
    sys.stdout.write(report_text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
