#!/usr/bin/env python3
"""Render CMUdict text through a deliberately coarse Plaits LPC inventory.

This is Gate 1 research code, not a firmware word-bank generator. Pronunciations
come from an external CMUdict file supplied with --cmudict. CMUdict's canonical
source and license are https://github.com/cmusphinx/cmudict.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


SCRIPT = Path(__file__).resolve()
REPO_ROOT = SCRIPT.parents[2]
CPP_SOURCE = SCRIPT.with_suffix(".cc")
DEFAULT_RENDERER = Path(tempfile.gettempdir()) / "plaits-render-lpc-gate1"
TICKS_PER_SECOND = 40


@dataclass(frozen=True)
class Target:
    frame: int
    label: str
    confidence: str


# Frames 0..4 are explicitly documented by Plaits as a/e/i/o/u. Frame 5 is an
# exact match for frame 2 of stock "two"; frame 6 exactly matches frame 17 of
# stock "six". The remaining labels are coarse acoustic/contextual assignments,
# not names supplied by Mutable Instruments.
TARGETS = {
    "a": Target(0, "a", "documented"),
    "e": Target(1, "e", "documented"),
    "i": Target(2, "i", "documented"),
    "o": Target(3, "o", "documented"),
    "u": Target(4, "u", "documented"),
    "stop": Target(5, "unvoiced stop", "exact stock /t/ context"),
    "sibilant": Target(6, "unvoiced sibilant", "exact stock /s/ context"),
    "glide": Target(7, "voiced glide", "heuristic"),
    "vfricative": Target(8, "voiced fricative", "nearest stock /v/ contexts"),
    "liquid": Target(9, "voiced liquid", "heuristic"),
    "nasal_coda": Target(10, "nasal/coda", "nearest stock final /n/ contexts"),
    "r": Target(11, "r-colored", "nearest stock /r/ contexts"),
    "nasal": Target(12, "nasal/onset", "nearest stock /n/ and /ng/ contexts"),
    "voiced_stop": Target(13, "voiced stop/nasal", "heuristic"),
    "l": Target(14, "l-colored", "nearest stock initial /l/ contexts"),
}


VOWELS = {
    "AA": ("a",), "AE": ("a",), "AH": ("u",), "AO": ("o",),
    "AW": ("a", "u"), "AY": ("a", "i"), "EH": ("e",),
    "ER": ("u", "r"), "EY": ("e", "i"), "IH": ("i",),
    "IY": ("i",), "OW": ("o", "u"), "OY": ("o", "i"),
    "UH": ("u",), "UW": ("u",),
}


CONSONANTS = {
    "B": ("voiced_stop",), "CH": ("stop", "sibilant"),
    "D": ("voiced_stop",), "DH": ("vfricative",), "F": ("sibilant",),
    "G": ("voiced_stop",), "HH": ("sibilant",), "JH": ("voiced_stop", "sibilant"),
    "K": ("stop",), "L": ("l",), "M": ("voiced_stop",),
    "N": ("nasal",), "NG": ("nasal",), "P": ("stop",),
    "R": ("r",), "S": ("sibilant",), "SH": ("sibilant",),
    "T": ("stop",), "TH": ("sibilant",), "V": ("vfricative",),
    "W": ("glide",), "Y": ("i",), "Z": ("vfricative",),
    "ZH": ("vfricative",),
}


CONTINUANTS = {"DH", "F", "HH", "L", "M", "N", "NG", "R", "S", "SH", "TH", "V", "W", "Y", "Z", "ZH"}
WORD_RE = re.compile(r"[a-z]+(?:'[a-z]+)?")
PHONE_RE = re.compile(r"^([A-Z]+)([012]?)$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("text", nargs="*", help="English text to render")
    parser.add_argument("--cmudict", type=Path,
                        help="path to the official cmudict.dict file")
    parser.add_argument("--output", required=True, type=Path, help="output WAV")
    parser.add_argument("--report", type=Path, help="optional timestamp/mapping report")
    parser.add_argument("--renderer", type=Path, default=DEFAULT_RENDERER,
                        help="compiled helper path (default: temporary directory)")
    parser.add_argument("--rebuild", action="store_true", help="force helper rebuild")
    parser.add_argument("--word-gap-ms", type=int, default=125,
                        help="silence between words (default: 125)")
    parser.add_argument("--phoneme-chart", action="store_true",
                        help="render all 15 Plaits frames instead of text")
    parser.add_argument("--formant-semitones", type=float, default=0.0,
                        help="voice-register shift, -18..18 semitones (default: 0)")
    parser.add_argument("--pitch-hz", type=float, default=100.0,
                        help="fundamental frequency, 50..300 Hz (default: 100)")
    return parser.parse_args()


def load_cmudict(path: Path) -> dict[str, list[str]]:
    pronunciations: dict[str, list[str]] = {}
    with path.open(encoding="latin-1") as source:
        for line in source:
            line = line.split("#", 1)[0].strip()
            if not line or line.startswith(";;;"):
                continue
            word, *phones = line.split()
            base = re.sub(r"\(\d+\)$", "", word.lower())
            pronunciations.setdefault(base, phones)
    return pronunciations


def build_renderer(path: Path, force: bool) -> None:
    sources = [
        CPP_SOURCE,
        REPO_ROOT / "plaits/dsp/speech/lpc_speech_synth.cc",
        REPO_ROOT / "plaits/dsp/speech/lpc_speech_synth_phonemes.cc",
        REPO_ROOT / "plaits/resources.cc",
        REPO_ROOT / "stmlib/utils/random.cc",
    ]
    stale = force or not path.exists() or any(source.stat().st_mtime > path.stat().st_mtime for source in sources)
    if not stale:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which("c++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("no C++ compiler found")
    command = [
        compiler, "-std=c++11", "-O2", "-w", "-I", str(REPO_ROOT),
        *(str(source) for source in sources), "-o", str(path),
    ]
    subprocess.run(command, check=True)


def phone_targets(phone: str) -> tuple[list[Target], int, str]:
    match = PHONE_RE.match(phone)
    if not match:
        raise ValueError(f"unsupported CMUdict phone {phone!r}")
    symbol, stress = match.groups()
    if symbol in VOWELS:
        keys = VOWELS[symbol]
        total_ticks = 5 if stress == "1" else 4 if stress == "2" else 3
    elif symbol in CONSONANTS:
        keys = CONSONANTS[symbol]
        total_ticks = 2 if symbol in CONTINUANTS else 1
    else:
        raise ValueError(f"unmapped CMUdict phone {symbol!r}")
    targets = [TARGETS[key] for key in keys]
    total_ticks = max(total_ticks, len(targets))
    return targets, total_ticks, symbol + stress


def distribute_ticks(total: int, count: int) -> list[int]:
    quotient, remainder = divmod(total, count)
    return [quotient + (1 if index < remainder else 0) for index in range(count)]


def render_plan(
    words: list[str], pronunciations: dict[str, list[str]], gap_ticks: int
) -> tuple[list[tuple[int, int]], list[str]]:
    plan: list[tuple[int, int]] = [(-1, 4)]
    report = ["Gate 1 coarse CMUdict -> Plaits LPC render", "", "Mapping confidence is per target; heuristic does not mean a source phoneme label.", ""]
    cursor = 4
    missing = [word for word in words if word not in pronunciations]
    if missing:
        raise ValueError("not found in CMUdict: " + ", ".join(sorted(set(missing))))

    for word_index, word in enumerate(words):
        start = cursor / TICKS_PER_SECOND
        phone_descriptions = []
        for raw_phone in pronunciations[word]:
            targets, total_ticks, phone = phone_targets(raw_phone)
            ticks = distribute_ticks(total_ticks, len(targets))
            descriptions = []
            for target, duration in zip(targets, ticks):
                plan.append((target.frame, duration))
                cursor += duration
                descriptions.append(f"{target.frame}:{target.label}[{target.confidence}]")
            phone_descriptions.append(f"{phone}=>" + "+".join(descriptions))
        end = cursor / TICKS_PER_SECOND
        report.append(f"{start:7.3f}-{end:7.3f}  {word:<14} {' '.join(pronunciations[word])}")
        report.append("                 " + " | ".join(phone_descriptions))
        gap = gap_ticks if word_index + 1 < len(words) else 4
        plan.append((-1, gap))
        cursor += gap
    return plan, report


def phoneme_chart_plan() -> tuple[list[tuple[int, int]], list[str]]:
    plan: list[tuple[int, int]] = [(-1, 4)]
    report = [
        "Gate 1 Plaits LPC phoneme-frame chart",
        "",
        "Frames 0-4 are held directly. Frames 5-14 are heard as a-frame-a.",
        "",
    ]
    cursor = 4
    by_frame = {target.frame: target for target in TARGETS.values()}
    for frame in range(15):
        target = by_frame[frame]
        start = cursor / TICKS_PER_SECOND
        if frame < 5:
            item = [(frame, 6)]
        else:
            item = [(0, 3), (frame, 3), (0, 3)]
        plan.extend(item)
        cursor += sum(ticks for _, ticks in item)
        end = cursor / TICKS_PER_SECOND
        report.append(
            f"{start:7.3f}-{end:7.3f}  frame {frame:02d}  "
            f"{target.label} [{target.confidence}]"
        )
        plan.append((-1, 10))
        cursor += 10
    return plan, report


def main() -> int:
    args = parse_args()
    if args.word_gap_ms < 0 or args.word_gap_ms > 5000:
        raise SystemExit("--word-gap-ms must be between 0 and 5000")
    if not -18.0 <= args.formant_semitones <= 18.0:
        raise SystemExit("--formant-semitones must be between -18 and 18")
    if not 50.0 <= args.pitch_hz <= 300.0:
        raise SystemExit("--pitch-hz must be between 50 and 300")
    if args.phoneme_chart:
        plan, report = phoneme_chart_plan()
    else:
        text = " ".join(args.text).strip()
        if not text:
            raise SystemExit("provide text to render")
        if not args.cmudict:
            raise SystemExit("--cmudict is required when rendering text")
        words = WORD_RE.findall(text.lower())
        if not words:
            raise SystemExit("text contains no dictionary words")
        pronunciations = load_cmudict(args.cmudict)
        gap_ticks = max(1, round(args.word_gap_ms * TICKS_PER_SECOND / 1000))
        try:
            plan, report = render_plan(words, pronunciations, gap_ticks)
        except ValueError as error:
            raise SystemExit(str(error)) from error

    build_renderer(args.renderer, args.rebuild)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", suffix=".plan", delete=False) as plan_file:
        plan_path = Path(plan_file.name)
        for frame, ticks in plan:
            plan_file.write(f"{frame} {ticks}\n")
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
