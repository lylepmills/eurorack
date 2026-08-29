#!/usr/bin/env python3
"""Decode a direct Core Audio capture from the autonomous terrain probe."""

from __future__ import annotations

import argparse
import array
import statistics
import wave


CASE_NAMES = (
    "Original terrain 1",
    "4 KB sampled grid",
    "Soft rings",
    "Lone island",
    "Tilted terraces",
    "River bend",
    "Rippled saddle",
    "Four chambers",
    "Spiral current",
    "Twin pulses",
    "Log crater",
    "Pinched diamond",
    "Saturated saddle",
    "Warped fault",
    "Four-sine stress",
    "Eight-sine stress",
    "Terraces + crater",
    "Spiral + pulses",
    "theta/mu field",
)

LEADER = 6.0
GAP = 1.5
CASE = 4.0
SLOT = GAP + CASE
CYCLE = LEADER + len(CASE_NAMES) * SLOT + 6.0


def load_mono(path):
    with wave.open(path, "rb") as source:
        if source.getnchannels() != 1 or source.getsampwidth() != 2:
            raise SystemExit("expected mono 16-bit PCM WAV")
        rate = source.getframerate()
        samples = array.array("h")
        samples.frombytes(source.readframes(source.getnframes()))
    return rate, samples


def frequency(samples, rate, start_seconds, duration_seconds):
    start = max(0, int(start_seconds * rate))
    end = min(len(samples), int((start_seconds + duration_seconds) * rate))
    if end - start < rate // 10:
        return float("nan")
    rising = 0
    previous = samples[start]
    for current in samples[start + 1:end]:
        if previous <= 0 < current:
            rising += 1
        previous = current
    return rising * rate / float(end - start)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("wav")
    args = parser.parse_args()
    rate, samples = load_mono(args.wav)
    duration = len(samples) / rate
    print(f"{duration:.3f} s, {rate} Hz")

    hop = 0.25
    tracked = []
    t = 0.0
    while t + hop <= duration:
        tracked.append((t, frequency(samples, rate, t, hop)))
        t += hop
    finite = [hz for _, hz in tracked if hz == hz]
    print(f"tracked range: {min(finite):.1f}..{max(finite):.1f} Hz")

    # Recover the observed cycle length by matching the repeating frequency
    # contour. This remains valid if Core Audio drops buffers or its timestamps
    # run at a different rate than the nominal 48 kHz schedule.
    expected_frames = int(CYCLE / hop)
    candidates = []
    for lag in range(int(expected_frames * 0.75), int(expected_frames * 1.1)):
        differences = []
        for index in range(len(tracked) - lag):
            a = tracked[index][1]
            b = tracked[index + lag][1]
            differences.append(min(abs(a - b), 300.0))
        if differences:
            candidates.append((statistics.median(differences),
                               statistics.mean(differences), lag))
    candidates.sort()
    _, _, best_lag = candidates[0]
    observed_cycle = best_lag * hop
    time_scale = observed_cycle / CYCLE
    print(f"observed cycle: {observed_cycle:.3f} s "
          f"(time scale {time_scale:.6f})")

    # Gaps contain only scan-path overhead and form the lowest-frequency mode.
    # Estimate that mode from the bottom decile and find candidate long runs.
    ordered = sorted(finite)
    low = statistics.median(ordered[:max(1, len(ordered) // 10)])
    threshold = low + 18.0
    runs = []
    run_start = None
    for index, (_, hz) in enumerate(tracked + [(duration, threshold + 1.0)]):
        is_low = hz <= threshold
        if is_low and run_start is None:
            run_start = index
        elif not is_low and run_start is not None:
            runs.append((run_start * hop, index * hop))
            run_start = None
    print(f"gap mode: {low:.1f} Hz; threshold: {threshold:.1f} Hz")

    sync_runs = [(start, end) for start, end in runs
                 if end - start >= 7.0 * time_scale]
    if not sync_runs:
        raise SystemExit("could not find the autosweep sync gap")

    # A sync run can merge with a baseline-cost case. Test candidate positions
    # inside each long run and choose the one whose scheduled gaps are lowest
    # and whose case windows have the strongest contrast.
    def alignment_score(start):
        gap_values = []
        case_values = []
        scaled_leader = LEADER * time_scale
        scaled_slot = SLOT * time_scale
        scaled_gap = GAP * time_scale
        scaled_case = CASE * time_scale
        for index in range(len(CASE_NAMES)):
            slot = start + scaled_leader + index * scaled_slot
            gap_values.append(frequency(samples, rate,
                                        slot + 0.25 * scaled_gap,
                                        0.5 * scaled_gap))
            case_values.append(frequency(samples, rate,
                                         slot + scaled_gap + 0.65 * scaled_case,
                                         0.25 * scaled_case))
        contrast = statistics.mean(
            abs(case - gap) for case, gap in zip(case_values, gap_values))
        return statistics.median(gap_values), -contrast

    starts = []
    for sync_start, sync_end in sync_runs:
        center = 0.5 * (sync_start + sync_end)
        for offset_steps in range(-12, 13):
            candidate = center + offset_steps * 0.25
            while candidate < 0:
                candidate += observed_cycle
            while candidate + observed_cycle > duration:
                candidate -= observed_cycle
            if candidate >= 0 and candidate + observed_cycle <= duration:
                starts.append(candidate)
    cycle_start = min(starts, key=alignment_score)
    print(f"decoded cycle start: {cycle_start:.3f} s")

    results = []
    for index, name in enumerate(CASE_NAMES):
        case_start = cycle_start + (LEADER + index * SLOT + GAP) * time_scale
        # Measure the stabilized tail after the CPU probe's slow-release EMA.
        measure_duration = 1.25 * time_scale
        measure_start = case_start + CASE * time_scale - measure_duration
        hz = frequency(samples, rate, measure_start, measure_duration)
        results.append((index, name, hz))
    print("\ncase readings")
    for index, name, hz in results:
        print(f"{index:2d}  {hz:7.1f} Hz  {hz/10.0:6.2f}%  {name}")

    comparisons = []
    for alternate_start in (cycle_start - observed_cycle,
                            cycle_start + observed_cycle):
        for index, _, hz in results:
            alternate_case = alternate_start + (
                LEADER + index * SLOT + GAP) * time_scale
            measure_duration = 1.25 * time_scale
            measure_start = (alternate_case + CASE * time_scale -
                             measure_duration)
            if (measure_start >= 0 and
                    measure_start + measure_duration <= duration):
                alternate_hz = frequency(samples, rate, measure_start,
                                         measure_duration)
                comparisons.append((index, alternate_hz - hz))
    if comparisons:
        alternate = {index: delta for index, delta in comparisons}
        absolute = [abs(delta) for _, delta in comparisons]
        print(f"\nrepeat check: {len(comparisons)} duplicate readings, "
              f"median delta {statistics.median(absolute):.1f} Hz, "
              f"max delta {max(absolute):.1f} Hz")
        print("\nconservative two-pass readings")
        for index, name, hz in results:
            other = hz + alternate[index]
            worst = max(hz, other)
            print(f"{index:2d}  {hz:7.1f} / {other:7.1f} Hz  "
                  f"max {worst/10.0:6.2f}%  {name}")


if __name__ == "__main__":
    main()
