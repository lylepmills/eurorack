#!/usr/bin/env python3
"""Split Japanese or Mandarin text into musically useful selector entries."""

from __future__ import annotations

import argparse
import json


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--language", required=True, choices=("ja", "zh"))
    parser.add_argument("--text", required=True)
    return parser.parse_args()


def segment_japanese(text: str) -> list[str]:
    from fugashi import Tagger  # pylint: disable=import-outside-toplevel

    words: list[str] = []
    current = ""
    for token in Tagger()(text):
        surface = token.surface.strip()
        if not surface:
            continue
        pos1 = token.feature.pos1
        pos2 = token.feature.pos2
        if pos1 == "補助記号":
            if current:
                words.append(current)
                current = ""
            continue
        attaches = bool(current) and (
            pos1 in {"助詞", "助動詞"}
            or (pos1 == "動詞" and pos2 == "非自立可能")
            or "接尾辞" in pos2
        )
        if attaches:
            current += surface
        else:
            if current:
                words.append(current)
            current = surface
    if current:
        words.append(current)
    return words


def segment_mandarin(text: str) -> list[str]:
    import jieba.posseg as posseg  # pylint: disable=import-outside-toplevel

    words: list[str] = []
    for token in posseg.cut(text):
        surface = token.word.strip()
        if not surface or token.flag == "x":
            continue
        if words and token.flag.startswith("u"):
            words[-1] += surface
        else:
            words.append(surface)
    return words


def main() -> int:
    args = parse_args()
    words = segment_japanese(args.text) if args.language == "ja" else segment_mandarin(args.text)
    print(json.dumps({"words": words}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
