"""Recipe-baked word banks for the Natural Speech engine.

The sibling of speech_banks.py, and deliberately the same shape: a recipe
carries its banks as validated frame data, and this module renders them into
a generated header the firmware compiles in. Natural Speech does NOT take its
banks through Engine::LoadUserData -- that path exists for the six-op FM
patch banks, which are transferable over TIMBRE at runtime. Word banks are
compile-time recipe content, exactly as they are for Speech.

The frame format is NSH1, 23 bytes, as produced by the WORLD-vocoder encoder
(see research/natural_speech/analyze_world.py in the rubato-audio repo):

    0     gain     dB = 0.5 * value - 96, 0 meaning hard silence
    1     f0       int8 semitone offset from the bank's 100 Hz register,
                   in quarter-semitone steps
    2     v0 | v1 << 4    per-band voicing nibbles, 0-15
    3     v2 | v3 << 4
    4     v4 | flags << 4  flags bit 0 = voiced
    5-22  lar0..lar17     int8 log-area-ratios, value / 127 * 7.0

Per-bank mean tract shapes are DERIVED here rather than accepted from the
client: MACRO interpolates each frame toward its bank's mean, so a mean that
disagreed with the frames would quietly mistune the articulation control,
and it is computable from the frames themselves.
"""

from __future__ import annotations

import base64
import struct
from typing import Any

MAX_BANKS = 8
MAX_WORDS = 32
MAX_FRAMES = 1024
LPC_ORDER = 18
BYTES_PER_FRAME = 5 + LPC_ORDER
LAR_MAX = 7.0

FRAME_STRUCT = struct.Struct("<5B18b")
assert FRAME_STRUCT.size == BYTES_PER_FRAME


def _safe_comment(value: str) -> str:
    return "".join(c for c in value if c.isprintable() and c not in "*/")[:60]


def _mean_lar(frames: list[tuple[int, ...]]) -> list[int]:
    """Energy-weighted mean tract over a bank's audible frames.

    Weighting by frame amplitude keeps near-silent frames, whose fitted
    tracts are meaningless, from dragging the mean around.
    """
    totals = [0.0] * LPC_ORDER
    weight_sum = 0.0
    for frame in frames:
        gain = frame[0]
        if gain == 0:
            continue
        # gain byte -> dB -> amplitude, matching the firmware's decode.
        weight = 10.0 ** ((0.5 * gain - 96.0) / 20.0)
        weight_sum += weight
        for i in range(LPC_ORDER):
            totals[i] += weight * frame[5 + i]
    if weight_sum <= 0.0:
        return [0] * LPC_ORDER
    return [max(-127, min(127, int(round(total / weight_sum))))
            for total in totals]


def validate_natural_speech_banks(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != {"customBanks"}:
        raise ValueError("naturalSpeechBanks must contain customBanks")
    custom = value["customBanks"]
    if not isinstance(custom, list) or not 1 <= len(custom) <= MAX_BANKS:
        raise ValueError(
            f"Natural Speech must contain between one and {MAX_BANKS} banks")

    normalized = []
    for bank in custom:
        if (not isinstance(bank, dict)
                or set(bank) != {"words", "wordBoundaries", "frameData"}):
            raise ValueError("a Natural Speech bank has an unsupported shape")
        words = bank["words"]
        boundaries = bank["wordBoundaries"]
        encoded = bank["frameData"]
        if (not isinstance(words, list) or not 1 <= len(words) <= MAX_WORDS
                or any(not isinstance(word, str) or not word.strip()
                       or len(word) > 80 for word in words)):
            raise ValueError(
                f"a Natural Speech bank must contain 1 through {MAX_WORDS} "
                "named words")
        if not isinstance(encoded, str):
            raise ValueError("a Natural Speech bank is missing its frame data")
        try:
            packed = base64.b64decode(encoded, validate=True)
        except (ValueError, TypeError) as error:
            raise ValueError(
                "a Natural Speech bank contains invalid frame data") from error
        if not packed or len(packed) % BYTES_PER_FRAME:
            raise ValueError(
                "a Natural Speech bank contains misaligned frame data")
        frame_count = len(packed) // BYTES_PER_FRAME
        if frame_count > MAX_FRAMES:
            raise ValueError(
                f"a Natural Speech bank exceeds {MAX_FRAMES} frames")
        if (not isinstance(boundaries, list)
                or len(boundaries) != len(words) + 1
                or any(type(item) is not int for item in boundaries)
                or boundaries[0] != 0 or boundaries[-1] != frame_count
                or any(right <= left
                       for left, right in zip(boundaries, boundaries[1:]))):
            raise ValueError(
                "a Natural Speech bank contains invalid word boundaries")
        # Unpacking proves every frame has exactly the firmware's shape.
        frames = [FRAME_STRUCT.unpack_from(packed, offset)
                  for offset in range(0, len(packed), BYTES_PER_FRAME)]
        normalized.append({
            "words": [word.strip() for word in words],
            "wordBoundaries": boundaries,
            "frameData": base64.b64encode(packed).decode("ascii"),
            "frames": frames,
            "meanLar": _mean_lar(frames),
        })
    return {"customBanks": normalized}


def render_natural_speech_config(value: dict[str, Any] | None) -> str:
    header = [
        "// Generated by natural_speech_banks.py. Do not edit.",
        "#ifndef PLAITS_DSP_NATURAL_SPEECH_RECIPE_CONFIG_H_",
        "#define PLAITS_DSP_NATURAL_SPEECH_RECIPE_CONFIG_H_",
    ]
    if value is None:
        return "\n".join(header + [
            "#define PLAITS_HAS_CUSTOM_NATURAL_SPEECH_BANKS 0",
            "#endif",
            "",
        ])

    custom = value["customBanks"]
    frames: list[tuple[int, ...]] = []
    boundaries = [0]
    bank_first = [0]
    means: list[int] = []
    lines = header + [
        "#define PLAITS_HAS_CUSTOM_NATURAL_SPEECH_BANKS 1",
        "#include <stdint.h>",
        "namespace plaits {",
        "namespace natural_speech_recipe {",
    ]
    for bank_index, bank in enumerate(custom):
        bank_bounds = bank["wordBoundaries"]
        for word_index, word in enumerate(bank["words"]):
            span = bank["frames"][bank_bounds[word_index]:
                                  bank_bounds[word_index + 1]]
            frames.extend(span)
            boundaries.append(boundaries[-1] + len(span))
        bank_first.append(len(boundaries) - 1)
        means.extend(bank["meanLar"])

    lines.append(f"const int kNumBanks = {len(custom)};")
    lines.append(f"const int kNumWords = {len(boundaries) - 1};")
    lines.append(f"const int kNumFrames = {len(frames)};")
    lines.append(
        "const uint16_t kBankFirstWord[] = { "
        + ", ".join(str(v) for v in bank_first) + " };")
    lines.append(
        "const uint16_t kWordBoundaries[] = { "
        + ", ".join(str(v) for v in boundaries) + " };")
    lines.append(
        "const int8_t kBankMeanLar[] = { "
        + ", ".join(str(v) for v in means) + " };")
    lines.append("const uint8_t kBankFrames[] = {")
    cursor = 0
    for bank_index, bank in enumerate(custom):
        lines.append(f"  // bank {bank_index}")
        for word_index, word in enumerate(bank["words"]):
            lines.append(f"  // {word_index}: {_safe_comment(word)}")
            count = (bank["wordBoundaries"][word_index + 1]
                     - bank["wordBoundaries"][word_index])
            for frame in frames[cursor:cursor + count]:
                packed = FRAME_STRUCT.pack(*frame)
                lines.append("  " + ", ".join(str(b) for b in packed) + ",")
            cursor += count
    lines.append("};")
    lines.append("}  // namespace natural_speech_recipe")
    lines.append("}  // namespace plaits")
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def bank_flash_bytes(value: dict[str, Any] | None) -> int:
    """What these banks actually cost, for the website's flash meter.

    Natural Speech frames are 23 bytes at 40 Hz -- 920 B per second of
    speech, 1.64x the classic LPC rate -- and the meter has to say so.
    """
    if value is None:
        return 0
    total = 0
    for bank in value["customBanks"]:
        total += len(bank["frames"]) * BYTES_PER_FRAME
        total += LPC_ORDER                      # the bank's mean tract
        total += 2 * (len(bank["words"]) + 1)   # word boundaries
    total += 2 * (len(value["customBanks"]) + 1)  # bank index
    return total
