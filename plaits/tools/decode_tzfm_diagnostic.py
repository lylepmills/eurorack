#!/usr/bin/env python3
"""Decode an autonomous Plaits TZFM benchmark recording from AUX."""

from __future__ import annotations

import argparse
import array
import math
import statistics
import sys
import wave


MODEL_NAMES = {
    1: [
        "Virtual Analog", "VA Dual", "VA Crossfade", "VA VCF",
        "Waveshaping", "Phase Distortion", "Wavetable", "Additive/Harmonic",
    ],
    2: [
        "Harmonics", "Two-op FM", "Raw FM", "Ring Mod", "Sideband",
        "Vowel FOF", "Digital Modulation", "Loopback",
    ],
    3: [
        "Phase Weave", "Phase Flock", "Spectral Spiral", "Wave Terrain",
        "Fold", "Buzz", "Pulsar", "Toy",
    ],
    4: ["Saw Swarm", "Triple", "VOSIM", "Wave Scan", "Swarm"],
    # Group 8 is intentionally non-adjacent to the TZFM groups: its 6260 Hz
    # metadata tone stays comfortably clear of the 6208 Hz engine-count tone.
    8: [
        "CSaw", "Dual Sync", "Morph", "Saw Square",
        "Vowel", "Sub Oscillator", "GENDY", "Bytebeat",
    ],
    9: [
        "Glisson", "Scanned", "Lockstep", "Tapfield",
        "Attractor", "Rulefield", "Question Mark", "Freshets Formant",
    ],
    10: [
        "Undertow", "Reed Pipe", "Z Filter", "Granular Cloud",
        "Noise Bank", "Particle Burst", "Plucked", "Blown",
    ],
    11: [
        "Modal Resonator", "Inharmonic String", "Filtered Noise",
        "Particle Noise", "Analog Bass Drum", "Analog Snare",
        "Analog Hi-Hat", "String Machine",
    ],
    12: [
        "Struck Bell", "Struck Drum", "Kick", "Snare", "Cymbal",
        "Wave Paraphonic", "Fluted", "Bowed",
    ],
    13: [
        "Granular Formant", "Chords", "Speech", "Speech Sounds",
        "LPC Words", "6-Op FM Bank A", "6-Op FM Bank B",
        "6-Op FM Bank C", "Chiptune",
    ],
    14: [
        "Saw Comb", "Diatonic Chord", "Scale Stack",
        "Wavetable Diatonic Chord", "Wavetable Scale Stack", "Shakers",
        "Brass", "Helix", "Clap",
    ],
}

FIELD_NAMES = [
    "slow_peak", "fast_peak", "slow_over90", "fast_over90",
    "slow_deadline", "fast_deadline", "overruns", "resyncs",
    "underflows", "excess_lag",
]

# A 420 ms tone cannot distinguish a zero counter from a handful of events
# after analogue capture and zero-crossing estimation. Counts above this are
# unambiguously sustained transport trouble; smaller values remain visible in
# the table but do not fail an otherwise healthy engine.
COUNTER_NOISE_FLOOR = 10


def _pcm_value(data: bytes, offset: int, width: int) -> int:
    if width == 1:
        return data[offset] - 128
    if width == 2:
        return int.from_bytes(data[offset:offset + 2], "little", signed=True)
    if width == 3:
        value = int.from_bytes(data[offset:offset + 3], "little", signed=False)
        return value - (1 << 24) if value & (1 << 23) else value
    if width == 4:
        return int.from_bytes(data[offset:offset + 4], "little", signed=True)
    raise ValueError(f"unsupported PCM sample width: {width} bytes")


def read_loudest_channel(path: str) -> tuple[int, array.array]:
    with wave.open(path, "rb") as source:
        channels = source.getnchannels()
        width = source.getsampwidth()
        rate = source.getframerate()
        frames = source.getnframes()
        data = source.readframes(frames)
    if source.getcomptype() != "NONE":
        raise ValueError("compressed WAV files are not supported")

    stride = channels * width
    powers = [0.0] * channels
    step = max(1, frames // 200000)
    for frame in range(0, frames, step):
        base = frame * stride
        for channel in range(channels):
            value = _pcm_value(data, base + channel * width, width)
            powers[channel] += float(value) * float(value)
    selected = max(range(channels), key=powers.__getitem__)
    samples = array.array("i")
    append = samples.append
    for frame in range(frames):
        append(_pcm_value(data, frame * stride + selected * width, width))
    return rate, samples


def active_runs(samples: array.array, rate: int) -> list[tuple[int, int]]:
    window = max(128, int(rate * 0.01))
    levels = []
    for start in range(0, len(samples) - window + 1, window):
        levels.append(sum(abs(v) for v in samples[start:start + window]) / window)
    if not levels or max(levels) == 0:
        return []
    sorted_levels = sorted(levels)
    noise = sorted_levels[len(sorted_levels) // 5]
    threshold = noise + (max(levels) - noise) * 0.25
    active = [level > threshold for level in levels]

    # Fill isolated one-window holes caused by a crossing or interface filter.
    for i in range(1, len(active) - 1):
        if not active[i] and active[i - 1] and active[i + 1]:
            active[i] = True

    runs = []
    start = None
    for i, on in enumerate(active + [False]):
        if on and start is None:
            start = i * window
        elif not on and start is not None:
            end = min(i * window, len(samples))
            if end - start >= int(rate * 0.25):
                runs.append((start, end))
            start = None
    return runs


def estimate_frequency(samples: array.array, rate: int, run: tuple[int, int]) -> float:
    start, end = run
    trim = min(int(rate * 0.04), (end - start) // 5)
    start += trim
    end -= trim
    if end <= start + 2:
        return 0.0
    mean = sum(samples[start:end]) / float(end - start)
    crossings = []
    previous = samples[start] - mean
    for i in range(start + 1, end):
        current = samples[i] - mean
        if previous <= 0.0 < current:
            fraction = -previous / (current - previous) if current != previous else 0.0
            crossings.append((i - 1) + fraction)
        previous = current
    if len(crossings) < 3:
        return 0.0
    # Average across the whole burst. An individual period is quantized to a
    # handful of samples at the 6 kHz markers; using its median would collapse
    # distinct 20 Hz metadata tones onto the same apparent frequency.
    period = (crossings[-1] - crossings[0]) / float(len(crossings) - 1)
    return rate / period if period > 0.0 else 0.0


def calibrate_frame(frame: list[float], group: int) -> list[float]:
    """Remove the affine clock error between Plaits and the recorder."""
    count = len(MODEL_NAMES[group])
    marker_indices = [0, 1, 2]
    marker_indices.extend(3 + engine * 11 for engine in range(count))
    marker_indices.append(len(frame) - 1)
    expected = [6000.0, 6100.0 + group * 20.0, 6200.0 + count]
    expected.extend(5000.0 + engine * 50.0 for engine in range(count))
    expected.append(6500.0)

    measured = [frame[index] for index in marker_indices]
    measured_mean = sum(measured) / len(measured)
    expected_mean = sum(expected) / len(expected)
    denominator = sum((value - measured_mean) ** 2 for value in measured)
    if denominator == 0.0:
        return frame
    scale = sum(
        (value - measured_mean) * (target - expected_mean)
        for value, target in zip(measured, expected)
    ) / denominator
    offset = expected_mean - scale * measured_mean
    return [scale * value + offset for value in frame]


def decode_frequencies(frequencies: list[float]) -> tuple[int, list[dict[str, int]]]:
    candidates = []
    for start, frequency in enumerate(frequencies):
        # A recorder runs from its own crystal. The ES-8 capture that prompted
        # this tolerance measured the 6 kHz sync tone near 5.983 kHz.
        if abs(frequency - 6000.0) > 100.0:
            continue
        for group in MODEL_NAMES:
            count = len(MODEL_NAMES[group])
            items = 3 + count * 11 + 1
            if start + items > len(frequencies):
                continue
            frame = calibrate_frame(frequencies[start:start + items], group)
            if abs(frame[0] - 6000.0) > 12.0:
                continue
            if abs(frame[1] - (6100.0 + group * 20.0)) > 12.0:
                continue
            if abs(frame[2] - (6200.0 + count)) > 12.0:
                continue
            if abs(frame[-1] - 6500.0) > 12.0:
                continue
            if any(
                abs(frame[3 + engine * 11] - (5000.0 + engine * 50.0))
                > 12.0
                for engine in range(count)
            ):
                continue
            candidates.append((start, group, frame))
    if not candidates:
        raise ValueError("no complete diagnostic frame found")

    # Reports repeat forever. Median all complete copies of the most frequently
    # observed group, rejecting short capture boundaries and individual edge
    # timing errors in the recorder.
    group = max(
        MODEL_NAMES,
        key=lambda candidate_group: sum(
            item_group == candidate_group
            for _, item_group, _ in candidates
        ),
    )
    frames = [frame for _, item_group, frame in candidates if item_group == group]
    frame = [statistics.median(values) for values in zip(*frames)]
    # Use fields that are mathematically guaranteed to be zero to remove the
    # last few hertz of low-frequency estimator bias. (If synthetic test data
    # does not obey those invariants, retain the documented 2500 Hz floor.)
    known_zero_tones = []
    for engine in range(len(MODEL_NAMES[group])):
        engine_offset = 3 + engine * 11
        slow_peak = int(round(frame[engine_offset + 1] - 2500.0))
        fast_peak = int(round(frame[engine_offset + 2] - 2500.0))
        if slow_peak < 900:
            known_zero_tones.extend(
                (frame[engine_offset + 3], frame[engine_offset + 5])
            )
        elif slow_peak < 1000:
            known_zero_tones.append(frame[engine_offset + 5])
        if fast_peak < 900:
            known_zero_tones.extend(
                (frame[engine_offset + 4], frame[engine_offset + 6])
            )
        elif fast_peak < 1000:
            known_zero_tones.append(frame[engine_offset + 6])
    payload_floor = 2500.0
    if known_zero_tones:
        estimated_floor = statistics.median(known_zero_tones)
        if abs(estimated_floor - payload_floor) <= 6.0:
            payload_floor = estimated_floor

    results = []
    offset = 3
    for engine, name in enumerate(MODEL_NAMES[group]):
        marker = frame[offset]
        expected_marker = 5000.0 + engine * 50.0
        if abs(marker - expected_marker) > 35.0:
            raise ValueError(
                f"engine {engine + 1} marker was {marker:.1f} Hz; "
                f"expected {expected_marker:.1f} Hz"
            )
        values = [max(0, int(round(hz - payload_floor)))
                  for hz in frame[offset + 1:offset + 11]]
        result = {"name": name}
        result.update(zip(FIELD_NAMES, values))
        for key in ("slow_over90", "fast_over90",
                    "slow_deadline", "fast_deadline"):
            result[key] = min(result[key], 1000)
        if result["slow_peak"] < 900:
            result["slow_over90"] = result["slow_deadline"] = 0
        elif result["slow_peak"] < 1000:
            result["slow_deadline"] = 0
        if result["fast_peak"] < 900:
            result["fast_over90"] = result["fast_deadline"] = 0
        elif result["fast_peak"] < 1000:
            result["fast_deadline"] = 0
        results.append(result)
        offset += 11
    return group, results


def verdict(result: dict[str, int]) -> str:
    faults = max(result[key] for key in
                 ("overruns", "resyncs", "underflows", "excess_lag"))
    if result["slow_deadline"] or result["fast_deadline"]:
        return "FAIL deadline"
    if faults > COUNTER_NOISE_FLOOR:
        return "FAIL FM transport"
    if result["slow_over90"] or result["fast_over90"]:
        return "RISKY >90%"
    if max(result["slow_peak"], result["fast_peak"]) >= 750:
        return "CAUTION"
    return "PASS"


def print_report(group: int, results: list[dict[str, int]]) -> None:
    label = "Fast exponential FM" if group >= 8 else "TZFM"
    print(f"{label} diagnostic group {group}")
    print("| # | Model | Slow peak | Fast peak | Slow >90 | Fast >90 | "
          "Slow miss | Fast miss | O/R | R/S | U/F | Lag | Verdict |")
    print("|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|")
    for index, result in enumerate(results, 1):
        percent = lambda key: f'{result[key] / 10.0:.1f}%'
        print(
            f"| {index} | {result['name']} | {percent('slow_peak')} | "
            f"{percent('fast_peak')} | {percent('slow_over90')} | "
            f"{percent('fast_over90')} | {percent('slow_deadline')} | "
            f"{percent('fast_deadline')} | {result['overruns']} | "
            f"{result['resyncs']} | {result['underflows']} | "
            f"{result['excess_lag']} | {verdict(result)} |"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", help="WAV recorded from the module's AUX output")
    args = parser.parse_args()
    try:
        rate, samples = read_loudest_channel(args.wav)
        runs = active_runs(samples, rate)
        frequencies = [estimate_frequency(samples, rate, run) for run in runs]
        group, results = decode_frequencies(frequencies)
        print_report(group, results)
    except (ValueError, wave.Error, OSError) as error:
        print(f"decode failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
