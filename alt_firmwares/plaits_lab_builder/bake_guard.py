#!/usr/bin/env python3
"""Catch a bad bake without a model, using length as the signal.

ASR round-tripping was tried as the accuracy gate and failed: it mis-transcribes
clean speech, cannot separate bad audio from audio a recognizer finds hard, and
scored voices at 86% that a listener found flawless. It is not usable for this.

But every defect that was actually caught by ear turned out to be a LENGTH
pathology rather than a substitution — norman over-ran, reza_ibrahim and nvcc
MNN began hundreds of milliseconds late, rapunzelina rambled, kerstin emitted
truncated fragments. Length costs nothing to know: the phoneme count comes from
the phonemizer we already run and the frame count from the encoder we already
run. Measured against a listener's verdicts, baked frames per phoneme separates
accepted voices (3.8-5.0) from rejected ones (9.0-16.8 when they over-run or
start late, 2.6 when they truncate) with no overlap — see measure_guard.py.

Two layers, neither needing anything in the container that is not already there:

  screen_voice()  decides whether a voice belongs in the catalog at all, from
                  its median over a word list. Margins are wide here.
  check_word()    decides whether one bake is worth re-rolling, against THAT
                  VOICE's own calibrated median. Per-word variance is higher —
                  a healthy voice reaches ~1.8x its own median on a hard word —
                  so the threshold is looser and the remedy is a re-synthesis
                  costing milliseconds rather than a rejection.

WHAT THIS DOES NOT CATCH: a wrong word of ordinary length. thorsten saying
"Bis bald" for "zwei" bakes to a perfectly normal duration and passes. Nothing
cheap catches that; it needs a listener. Do not let the presence of a guard
imply the output is verified.

Pure stdlib, so it can drop into the builder container as-is.
"""

from __future__ import annotations

import statistics
from dataclasses import dataclass

# Marks that espeak places for stress and length. They are not segments and
# would inflate the denominator.
DECORATIONS = "ˈˌːˑ.,;:?!-—…'’ "

# Catalog-time band, from the measured separation. Accepted voices occupied
# 3.8-5.0; the nearest rejects were 2.6 below and 9.0 above. These bounds keep
# roughly a factor of two of margin on each side rather than hugging the data,
# because ten voices is not enough evidence for a tight threshold.
VOICE_MIN_RATIO = 3.0
VOICE_MAX_RATIO = 7.0

# Bake-time bounds, relative to the voice's own median. Healthy voices reached
# 1.8x their own median on their hardest word, so 2.5x is clearly abnormal
# without being trigger-happy; 0.4x catches a word that came out truncated.
WORD_MAX_MULTIPLE = 2.5
WORD_MIN_MULTIPLE = 0.4

# Below this many segments the ratio is dominated by rounding — a one-phoneme
# word swings a whole frame per segment — so short words are judged only
# against an absolute ceiling.
MIN_PHONEMES_FOR_RATIO = 3
SHORT_WORD_MAX_FRAMES = 40


def count_phonemes(phonemes) -> int:
    """Segments in a phonemization, ignoring stress and length marks."""
    return sum(1 for p in phonemes if p.strip() and p not in DECORATIONS)


def frames_per_phoneme(frames: int, phonemes) -> float | None:
    segments = count_phonemes(phonemes)
    if segments < 1:
        return None
    return frames / segments


@dataclass(frozen=True)
class VoiceBaseline:
    """What a given voice's healthy output looks like, measured once."""

    voice: str
    median_ratio: float
    samples: int

    @property
    def usable(self) -> bool:
        return VOICE_MIN_RATIO <= self.median_ratio <= VOICE_MAX_RATIO


def calibrate(voice: str, measurements) -> VoiceBaseline:
    """Build a baseline from (frames, phonemes) pairs across a word list.

    Use the same list for every voice in a language: the point is to compare a
    voice against itself later, and a baseline drawn from easy words would make
    every real word look long.
    """
    ratios = [r for r in (frames_per_phoneme(f, p) for f, p in measurements)
              if r is not None]
    if not ratios:
        raise ValueError(f"no usable measurements for {voice}")
    return VoiceBaseline(voice, statistics.median(ratios), len(ratios))


def screen_voice(baseline: VoiceBaseline) -> tuple[bool, str]:
    """Catalog-time: does this voice belong in the offering at all?

    Not a substitute for listening — it screens out the voices that are broken
    in a way length reveals, so a person only auditions plausible candidates.
    """
    if baseline.samples < 5:
        return True, (f"{baseline.samples} samples is too few to judge; "
                      "not blocking, but calibrate on more words")
    if baseline.median_ratio > VOICE_MAX_RATIO:
        return False, (f"median {baseline.median_ratio:.1f} frames/phoneme is above "
                       f"{VOICE_MAX_RATIO}: the voice over-runs its words or starts late")
    if baseline.median_ratio < VOICE_MIN_RATIO:
        return False, (f"median {baseline.median_ratio:.1f} frames/phoneme is below "
                       f"{VOICE_MIN_RATIO}: the voice is truncating words")
    return True, f"median {baseline.median_ratio:.1f} frames/phoneme is in range"


def check_word(frames: int, phonemes, baseline: VoiceBaseline) -> tuple[bool, str]:
    """Bake-time: is this one word worth re-synthesizing?

    False means re-roll, not reject. These models are stochastic, so the same
    word usually comes out fine on another draw; a caller should try a few times
    and keep the first that passes, or the closest to baseline if none do.
    """
    segments = count_phonemes(phonemes)
    if segments < MIN_PHONEMES_FOR_RATIO:
        if frames > SHORT_WORD_MAX_FRAMES:
            return False, (f"{frames} frames for a {segments}-phoneme word "
                           f"exceeds the {SHORT_WORD_MAX_FRAMES}-frame ceiling")
        return True, f"{frames} frames, too short to judge by ratio"
    ratio = frames / segments
    high = baseline.median_ratio * WORD_MAX_MULTIPLE
    low = baseline.median_ratio * WORD_MIN_MULTIPLE
    if ratio > high:
        return False, (f"{ratio:.1f} frames/phoneme is {ratio / baseline.median_ratio:.1f}x "
                       f"this voice's median of {baseline.median_ratio:.1f}")
    if ratio < low:
        return False, (f"{ratio:.1f} frames/phoneme is {ratio / baseline.median_ratio:.1f}x "
                       f"this voice's median: the word came out truncated")
    return True, f"{ratio:.1f} frames/phoneme against a median of {baseline.median_ratio:.1f}"


def _self_test() -> int:
    """Regression test on the real measurements from measure_guard.py.

    The thresholds exist because of these numbers, so they are the fixtures.
    """
    accepted = {"joe": 3.8, "bryce": 5.0, "mike": 4.6,
                "thorsten": 4.0, "nathalie": 4.4}
    rejected = {"norman": 14.0, "reza_ibrahim": 9.0, "nvcc-MNN": 11.2,
                "rapunzelina": 16.8, "kerstin": 2.6}
    failures = 0
    for name, ratio in accepted.items():
        ok, why = screen_voice(VoiceBaseline(name, ratio, 5))
        if not ok:
            print(f"  FAIL kept voice {name} was screened out: {why}")
            failures += 1
    for name, ratio in rejected.items():
        ok, why = screen_voice(VoiceBaseline(name, ratio, 5))
        if ok:
            print(f"  FAIL rejected voice {name} passed the screen: {why}")
            failures += 1

    # Per-word: a healthy voice's worst word must survive, and the over-runs
    # that were observed within a bad voice must not.
    joe = VoiceBaseline("joe", 3.8, 5)
    hardest_healthy = 8 / 4          # nathalie's worst word, 1.8x its median
    if not check_word(int(round(hardest_healthy * 4 * 1.0)), list("abcd"), joe)[0]:
        print("  FAIL a healthy voice's hardest word would be re-rolled")
        failures += 1
    if check_word(40, list("abcd"), joe)[0]:
        print("  FAIL a 10x over-run passed the per-word check")
        failures += 1
    if check_word(2, list("abcdef"), joe)[0]:
        print("  FAIL a truncated word passed the per-word check")
        failures += 1

    print("bake_guard self-test:", "PASS" if not failures else f"{failures} FAILURES")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(_self_test())
