#!/usr/bin/env python3
"""Build a CMU Arctic diphone bank and render text with the Plaits LPC synth.

Gate 3 preserves real within-phone transitions. Each diphone is one coherent
corpus exemplar spanning the midpoint of one phone to the midpoint of the next;
unlike Gate 2, coefficients from unrelated utterances are never averaged.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

import numpy as np

import render_lpc_continuous as continuous
import render_lpc_gate2 as gate2


DEFAULT_BANK = Path(tempfile.gettempdir()) / "cmu-arctic-slt-plaits-lpc-diphones.json"
BANK_FORMAT = "plaits-cmu-arctic-lpc-diphone-bank-v1"
SILENCE_DB = -42.0
VOICING_THRESHOLD = 0.60
ENERGY_SCALE = 0.70


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("text", nargs="+", help="English text to render")
    parser.add_argument("--corpus-root", required=True, type=Path)
    parser.add_argument("--cmudict", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--bank", type=Path, default=DEFAULT_BANK)
    parser.add_argument("--rebuild-bank", action="store_true")
    parser.add_argument("--renderer", type=Path, default=gate2.DEFAULT_RENDERER)
    parser.add_argument("--rebuild-renderer", action="store_true")
    parser.add_argument("--formant-semitones", type=float, default=0.0)
    parser.add_argument("--pitch-hz", type=float, default=100.0)
    parser.add_argument("--word-gap-ms", type=int, default=125)
    parser.add_argument("--gain", type=float, default=1.0)
    return parser.parse_args()


def pair_key(left: str, right: str) -> str:
    return f"{left}|{right}"


def interval_features(
    samples: np.ndarray,
    start: float,
    end: float,
) -> list[dict[str, object] | None]:
    duration = end - start
    frame_count = max(1, int(round(duration * continuous.FRAME_RATE)))
    result = []
    for index in range(frame_count):
        center = start + duration * (index + 0.5) / frame_count
        center_sample = int(round(center * continuous.ANALYSIS_RATE))
        first = center_sample - continuous.FRAME_SIZE // 2
        last = first + continuous.FRAME_SIZE
        if first < 0 or last > len(samples):
            result.append(None)
        else:
            result.append(continuous.analyze_frame(samples[first:last]))
    return result


def select_median_occurrence(
    occurrences: list[tuple[str, float, float]],
) -> tuple[str, float, float]:
    durations = np.array([end - start for _, start, end in occurrences])
    median = float(np.median(durations))
    return min(
        occurrences,
        key=lambda item: (abs((item[2] - item[1]) - median), item[0], item[1]),
    )


def quantize_feature(
    feature: dict[str, object] | None,
    silence_rms: float,
) -> list[int]:
    if feature is None or float(feature["rms"]) < silence_rms:
        return [0] * 12
    voiced = float(feature["periodicity"]) >= VOICING_THRESHOLD
    period = int(feature["period"]) if voiced else 0
    energy = continuous.excitation_energy(
        float(feature["residual_rms"]), period, voiced, ENERGY_SCALE)
    coefficients = continuous.quantized_coefficients(feature["reflection"])
    return [energy, period, *coefficients]


def build_bank(corpus_root: Path) -> dict[str, object]:
    if not (corpus_root / "COPYING").is_file():
        raise ValueError(f"missing CMU Arctic COPYING: {corpus_root}")
    pair_occurrences: dict[str, list[tuple[str, float, float]]] = defaultdict(list)
    phone_occurrences: dict[str, list[tuple[str, float, float]]] = defaultdict(list)
    phone_durations: dict[str, list[float]] = defaultdict(list)
    for label_path in sorted((corpus_root / "lab").glob("*.lab")):
        segments = gate2.parse_labels(label_path)
        stem = label_path.stem
        for phone, start, end in segments:
            if phone == "pau":
                continue
            phone_occurrences[phone].append((stem, start, end))
            phone_durations[phone].append(end - start)
        for left, right in zip(segments, segments[1:]):
            left_phone, left_start, left_end = left
            right_phone, right_start, right_end = right
            if left_phone == "pau" or right_phone == "pau":
                continue
            start = 0.5 * (left_start + left_end)
            end = 0.5 * (right_start + right_end)
            pair_occurrences[pair_key(left_phone, right_phone)].append(
                (stem, start, end))
    if not pair_occurrences:
        raise ValueError(f"no diphones found in {corpus_root / 'lab'}")

    selected_pairs = {
        key: select_median_occurrence(items)
        for key, items in pair_occurrences.items()
    }
    selected_phones = {
        phone: select_median_occurrence(items)
        for phone, items in phone_occurrences.items()
    }
    by_utterance: dict[str, list[tuple[str, str, float, float]]] = defaultdict(list)
    for key, (stem, start, end) in selected_pairs.items():
        by_utterance[stem].append(("diphone", key, start, end))
    for phone, (stem, start, end) in selected_phones.items():
        by_utterance[stem].append(("phone", phone, start, end))

    analyzed: dict[tuple[str, str], dict[str, object]] = {}
    all_rms = []
    for stem in sorted(by_utterance):
        samples = continuous.downsample(gate2.read_wave(corpus_root / "wav" / f"{stem}.wav"))
        for kind, key, start, end in by_utterance[stem]:
            features = interval_features(samples, start, end)
            analyzed[(kind, key)] = {
                "source": stem,
                "start": start,
                "end": end,
                "features": features,
            }
            all_rms.extend(
                float(feature["rms"])
                for feature in features
                if feature is not None
            )
    reference_rms = float(np.percentile(all_rms, 95))
    silence_rms = reference_rms * 10.0 ** (SILENCE_DB / 20.0)

    diphones = {}
    for key, token in sorted(
        (key, analyzed[("diphone", key)]) for key in selected_pairs
    ):
        left, right = key.split("|", 1)
        diphones[key] = {
            "phones": [left, right],
            "source": token["source"],
            "start": token["start"],
            "end": token["end"],
            "occurrences": len(pair_occurrences[key]),
            "frames": [
                quantize_feature(feature, silence_rms)
                for feature in token["features"]
            ],
        }
    phones = {}
    for phone, token in sorted(
        (phone, analyzed[("phone", phone)]) for phone in selected_phones
    ):
        phones[phone] = {
            "source": token["source"],
            "start": token["start"],
            "end": token["end"],
            "occurrences": len(phone_occurrences[phone]),
            "frames": [
                quantize_feature(feature, silence_rms)
                for feature in token["features"]
            ],
        }
    return {
        "format": BANK_FORMAT,
        "corpus": "cmu_us_slt_arctic-0.95-release",
        "corpus_release_sha256": gate2.CORPUS_RELEASE_SHA256,
        "copyright": gate2.CORPUS_COPYRIGHT,
        "modified": (
            "Derived 8 kHz, order-10 LPC diphones; one duration-medoid "
            "corpus exemplar per phone pair."
        ),
        "analysis": {
            "frame_rate": continuous.FRAME_RATE,
            "silence_db": SILENCE_DB,
            "silence_rms": silence_rms,
            "voicing_threshold": VOICING_THRESHOLD,
            "energy_scale": ENERGY_SCALE,
        },
        "phone_durations": {
            phone: float(np.median(durations))
            for phone, durations in sorted(phone_durations.items())
        },
        "diphones": diphones,
        "phones": phones,
    }


def load_or_build_bank(args: argparse.Namespace) -> dict[str, object]:
    if args.bank.exists() and not args.rebuild_bank:
        bank = json.loads(args.bank.read_text(encoding="utf-8"))
        if bank.get("format") != BANK_FORMAT:
            raise ValueError(f"unsupported bank format: {args.bank}")
        return bank
    bank = build_bank(args.corpus_root)
    args.bank.parent.mkdir(parents=True, exist_ok=True)
    args.bank.write_text(
        json.dumps(bank, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return bank


def silence_after(previous: list[int]) -> list[int]:
    frame = previous[:]
    frame[0] = 0
    return frame


def apply_ti_frame_semantics(frames: list[list[int]]) -> list[list[int]]:
    result = []
    previous = [0] * 12
    for source in frames:
        frame = source[:]
        if frame[0] == 0:
            frame = silence_after(previous)
        elif frame[1] == 0:
            # The TI format transmits K0..K3 only for unvoiced frames.
            frame[6:] = previous[6:]
        result.append(frame)
        previous = frame
    return result


def word_frames(phones: list[str], bank: dict[str, object]) -> tuple[list[list[int]], list[str]]:
    if len(phones) == 1:
        item = bank["phones"].get(phones[0])
        if item is None:
            raise ValueError(f"missing phone exemplar: {phones[0]}")
        return [frame[:] for frame in item["frames"]], [phones[0]]

    keys = [pair_key(left, right) for left, right in zip(phones, phones[1:])]
    missing = [key for key in keys if key not in bank["diphones"]]
    if missing:
        raise ValueError("missing diphones: " + ", ".join(missing))
    units = [bank["diphones"][key] for key in keys]
    first_ticks = max(1, int(round(bank["phone_durations"][phones[0]] * 20.0)))
    last_ticks = max(1, int(round(bank["phone_durations"][phones[-1]] * 20.0)))
    frames = [units[0]["frames"][0][:] for _ in range(first_ticks)]
    for unit in units:
        frames.extend(frame[:] for frame in unit["frames"])
    frames.extend(units[-1]["frames"][-1][:] for _ in range(last_ticks))
    return frames, keys


def make_plan(
    words: list[str],
    pronunciations: dict[str, list[str]],
    bank: dict[str, object],
    gap_ticks: int,
) -> tuple[list[list[int]], list[str]]:
    raw_plan = [[0] * 12 for _ in range(4)]
    report = [
        "Gate 3 CMUdict -> coherent CMU Arctic diphones -> Plaits LPC synth",
        f"Corpus: {bank['corpus']} ({bank['copyright']})",
        str(bank["modified"]),
        "",
    ]
    missing_words = [word for word in words if word not in pronunciations]
    if missing_words:
        raise ValueError("not found in CMUdict: " + ", ".join(sorted(set(missing_words))))
    cursor = len(raw_plan)
    for word_index, word in enumerate(words):
        phones = [gate2.corpus_phone(phone)[0] for phone in pronunciations[word]]
        frames, units = word_frames(phones, bank)
        start = cursor / continuous.FRAME_RATE
        raw_plan.extend(frames)
        cursor += len(frames)
        end = cursor / continuous.FRAME_RATE
        counts = [bank["diphones"][key]["occurrences"] for key in units] if len(phones) > 1 else []
        coverage = f"occurrences={min(counts)}..{max(counts)}" if counts else "monophone"
        report.append(
            f"{start:7.3f}-{end:7.3f}  {word:<14} "
            f"{' '.join(phones)}  {coverage}"
        )
        gap = gap_ticks if word_index + 1 < len(words) else 4
        raw_plan.extend([[0] * 12 for _ in range(gap)])
        cursor += gap
    return apply_ti_frame_semantics(raw_plan), report


def main() -> int:
    args = parse_args()
    if not -18.0 <= args.formant_semitones <= 18.0:
        raise SystemExit("--formant-semitones must be between -18 and 18")
    if not 50.0 <= args.pitch_hz <= 300.0:
        raise SystemExit("--pitch-hz must be between 50 and 300")
    if not 0 <= args.word_gap_ms <= 5000:
        raise SystemExit("--word-gap-ms must be between 0 and 5000")
    if not 0.0 <= args.gain <= 2.0:
        raise SystemExit("--gain must be between 0 and 2")
    words = gate2.WORD_RE.findall(" ".join(args.text).lower())
    try:
        bank = load_or_build_bank(args)
        pronunciations = gate2.load_cmudict(args.cmudict)
        gap_ticks = max(1, round(args.word_gap_ms * continuous.FRAME_RATE / 1000))
        plan, report = make_plan(words, pronunciations, bank, gap_ticks)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error

    gate2.build_renderer(args.renderer, args.rebuild_renderer)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", suffix=".plan", delete=False) as plan_file:
        plan_path = Path(plan_file.name)
        for frame in plan:
            plan_file.write(" ".join(str(value) for value in frame) + "\n")
    try:
        rendered = subprocess.run([
            str(args.renderer), str(plan_path), str(args.output),
            str(args.formant_semitones), str(args.pitch_hz), "0.0", str(args.gain),
        ], check=True, text=True, capture_output=True)
    finally:
        plan_path.unlink(missing_ok=True)

    report.insert(
        1,
        f"Formant {args.formant_semitones:+.1f} semitones; "
        f"fixed pitch {args.pitch_hz:.1f} Hz",
    )
    report.insert(2, rendered.stdout.strip())
    report_text = "\n".join(report) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report_text, encoding="utf-8")
    sys.stdout.write(report_text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
