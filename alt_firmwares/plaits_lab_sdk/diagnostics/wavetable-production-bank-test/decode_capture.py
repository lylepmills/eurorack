#!/usr/bin/env python3
"""Analyze a Core Audio capture from the production wavetable bank gate."""

from __future__ import annotations

import argparse
import array
import math
import statistics
import wave


RATE = 48000
LEADER = 6.0
GAP = 1.5
WINDOW = 8.0
SLOT = GAP + WINDOW
PROFILES = 4
CYCLE = LEADER + PROFILES * SLOT + 6.0
NOTES = (36, 48, 60, 72)


def load(path: str) -> tuple[int, array.array]:
    with wave.open(path, "rb") as source:
        if source.getnchannels() != 1 or source.getsampwidth() != 2:
            raise SystemExit("expected mono 16-bit PCM WAV")
        rate = source.getframerate()
        samples = array.array("h")
        samples.frombytes(source.readframes(source.getnframes()))
    return rate, samples


def rms(samples) -> float:
    if not samples:
        return 0.0
    return math.sqrt(sum(float(sample) * sample for sample in samples) / len(samples)) / 32768.0


def feature(samples, rate: int, bins: int = 64) -> list[float]:
    chunk_size = len(samples) // bins
    result = []
    for index in range(bins):
        chunk = samples[index * chunk_size:(index + 1) * chunk_size]
        if len(chunk) < 512:
            result.append(0.0)
            continue
        crossings = 0
        previous = chunk[0]
        for current in chunk[1:]:
            if previous <= 0 < current:
                crossings += 1
            previous = current
        result.append(crossings * rate / float(len(chunk)))
    return result


def correlation(left, right) -> float:
    if len(left) != len(right) or not left:
        return 0.0
    left_mean = statistics.mean(left)
    right_mean = statistics.mean(right)
    numerator = sum((a - left_mean) * (b - right_mean) for a, b in zip(left, right))
    left_energy = sum((value - left_mean) ** 2 for value in left)
    right_energy = sum((value - right_mean) ** 2 for value in right)
    denominator = math.sqrt(left_energy * right_energy)
    return numerator / denominator if denominator > 1.0e-12 else 0.0


def percentile(values, percent: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = (len(ordered) - 1) * percent / 100.0
    low = int(index)
    high = min(low + 1, len(ordered) - 1)
    fraction = index - low
    return ordered[low] + (ordered[high] - ordered[low]) * fraction


def find_cycle(samples, rate: int) -> int:
    hop = max(1, rate // 10)
    envelope = [
        rms(samples[start:start + hop])
        for start in range(0, max(0, len(samples) - hop), hop)
    ]
    threshold = max(0.002, percentile(envelope, 20) * 3.0)
    silent = [value < threshold for value in envelope]
    # Trailer and the following leader merge into a unique long silent run.
    # The next profile's 1.5-second settling gap touches it too, so the cycle
    # boundary is six seconds after the run begins (the trailer length), not
    # the midpoint. A lone six-second leader can occur at the edge of a capture,
    # so accepting it would align every window six seconds early.
    minimum = int(10.0 * rate / hop)
    candidates = []
    start = None
    for index, value in enumerate(silent + [False]):
        if value and start is None:
            start = index
        elif not value and start is not None:
            if index - start >= minimum:
                candidates.append((start * hop, index * hop))
            start = None
    for run_start, run_end in candidates:
        candidate = run_start + int(6.0 * rate)
        if candidate + int(CYCLE * rate) <= len(samples):
            return candidate
    raise SystemExit("could not find a complete 50-second autosweep cycle")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wav")
    parser.add_argument("--mode", choices=("mirrored", "one-way"), required=True)
    args = parser.parse_args()
    rate, samples = load(args.wav)
    if rate != RATE:
        raise SystemExit(f"expected 48 kHz, got {rate} Hz")
    cycle = find_cycle(samples, rate)
    print(f"capture {len(samples) / rate:.3f} s; cycle starts {cycle / rate:.3f} s")

    failures = []
    for profile in range(PROFILES):
        start = cycle + int((LEADER + profile * SLOT + GAP) * rate)
        window = samples[start:start + int(WINDOW * rate)]
        level = rms(window)
        peak = max(abs(sample) for sample in window) / 32768.0
        track = feature(window, rate)
        span = percentile(track, 90) - percentile(track, 10)
        endpoint = correlation(track[:8], track[-8:])
        direction = correlation(list(range(len(track))), track)
        print(
            f"profile {profile + 1} note {NOTES[profile]}: "
            f"rms {level:.4f}, peak {peak:.4f}, centroid span {span:.1f} Hz, "
            f"endpoint r {endpoint:+.3f}, direction r {direction:+.3f}"
        )
        if level < 0.01:
            failures.append(f"profile {profile + 1} is silent or too quiet")
        if peak >= 0.999:
            failures.append(f"profile {profile + 1} clips")
        if span < 40.0:
            failures.append(f"profile {profile + 1} does not audibly traverse its bank")
        if args.mode == "mirrored" and endpoint < 0.45:
            failures.append(f"profile {profile + 1} does not return toward its starting spectrum")
        if args.mode == "one-way" and profile == 0 and direction < 0.55:
            failures.append("neutral one-way scan does not climb through the 16 harmonic banks")

    if failures:
        print("\nFAIL")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("\nPASS: all production-engine windows were present, unclipped, and traversed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
