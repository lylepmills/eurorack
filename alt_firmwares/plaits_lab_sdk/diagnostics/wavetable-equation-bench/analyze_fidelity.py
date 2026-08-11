#!/usr/bin/env python3
"""Compare direct equations with Mutable's 15-wave mapped bank approximation."""

from __future__ import annotations

import math


SIZE = 128
GRID = tuple(i / 16 for i in range(17))
SPECTRAL_GRID = (0.0, 0.25, 0.5, 0.75, 1.0)


def sign(value: float) -> float:
    return 1.0 if value > 0 else -1.0 if value < 0 else 0.0


def round_js(value: float) -> float:
    return math.floor(value + 0.5)


def glass(phi: float, x: float, y: float) -> float:
    return math.sin(phi + (1 + 7 * x) * (0.2 + 0.65 * y) * math.sin(2 * phi))


def evaluate(case: int, phi: float, x: float, y: float) -> float:
    if case == 1:
        return math.sin(phi + 3 * (x + 0.125) * math.sin((1 + math.floor(4 * y)) * phi))
    if case == 2:
        return glass(phi, x, y)
    if case == 3:
        return (math.sin(phi) + 0.45 * math.sin((2 + math.floor(5 * x)) * phi)
                + 0.28 * math.sin((3 + math.floor(8 * y)) * phi))
    if case == 4:
        return math.sin(phi + (0.3 + 2.8 * x) * math.sin(phi + math.pi * y))
    if case == 5:
        return sign(math.sin(phi) - (0.75 * x - 0.35)) + 0.18 * math.sin((2 + math.floor(5 * y)) * phi)
    if case == 6:
        return (math.sin(phi) + 0.65 * x * math.sin(2 * phi)
                + 0.55 * y * math.sin(3 * phi) + 0.3 * x * y * math.sin(5 * phi))
    if case == 7:
        return glass(phi, x, y) + 0.24 * math.sin((2 + math.floor(6 * x)) * phi)
    if case == 8:
        return glass(phi, x, y) + 0.22 * math.sin((3 + 7 * y) * phi + math.pi * x)
    if case == 9:
        return math.sin(math.pi * glass(phi, x, y))
    if case == 10:
        return math.sin(phi + (1 + 4 * y) * glass(phi, x, y))
    if case == 11:
        return glass(phi, x, y) * math.sin((2 + math.floor(4 * y)) * phi)
    if case == 12:
        return round_js(5 * glass(phi, x, y)) / 5
    if case == 13:
        return math.atan(2.5 * glass(phi, x, y))
    if case == 14:
        return max(-0.6, min(0.6, glass(phi, x, y)))
    if case == 15:
        row = glass(phi, x, y) + 0.22 * math.sin((3 + 7 * y) * phi + math.pi * x)
        return math.atan(2.5 * math.sin(math.pi * row))
    if case == 16:
        return 0.125 * sum((
            math.sin(phi), math.sin(2 * phi + x), math.sin(3 * phi + y),
            math.sin(4 * phi + x + y), math.sin(5 * phi + 2 * x),
            math.sin(6 * phi + 2 * y), math.sin(7 * phi + x - y),
            math.sin(8 * phi + 2 * x - y),
        ))
    raise ValueError(case)


NAMES = (
    "mutable-fm", "glass-fm", "harmonic-grid", "phase-warp", "pulse-matrix",
    "parity-weave", "glass-upper-partial", "glass-row-motion", "glass-folded",
    "glass-fm-wrapped", "glass-ring-grid", "glass-terraced",
    "glass-soft-clipped", "glass-hard-clipped", "three-transform-stack",
    "eight-sine-stress",
)


def normalize(wave: list[float]) -> list[float]:
    mean = sum(wave) / len(wave)
    centered = [value - mean for value in wave]
    peak = max((abs(value) for value in centered), default=1.0) or 1.0
    return [value / peak for value in centered]


def direct_wave(case: int, x: float, y: float) -> list[float]:
    return normalize([evaluate(case, math.tau * i / SIZE, x, y) for i in range(SIZE)])


def legacy_waves(case: int) -> list[list[float]]:
    waves = []
    for wave in range(15):
        column = wave % 4
        row = wave // 4
        waves.append(direct_wave(case, column / 3, row / 3))
    return waves


def map_index(column: int, row: int) -> int:
    return min(math.floor(column * 4 / 8) + math.floor(row * 4 / 8) * 4, 14)


def legacy_wave(waves: list[list[float]], x: float, y: float) -> list[float]:
    gx = x * 7
    gy = y * 7
    x0 = math.floor(gx)
    y0 = math.floor(gy)
    x1 = min(7, x0 + 1)
    y1 = min(7, y0 + 1)
    tx = gx - x0
    ty = gy - y0
    a = waves[map_index(x0, y0)]
    b = waves[map_index(x1, y0)]
    c = waves[map_index(x0, y1)]
    d = waves[map_index(x1, y1)]
    return [
        (a[i] + (b[i] - a[i]) * tx) * (1 - ty)
        + (c[i] + (d[i] - c[i]) * tx) * ty
        for i in range(SIZE)
    ]


COSINES = tuple(tuple(math.cos(math.tau * k * i / SIZE) for i in range(SIZE)) for k in range(65))
SINES = tuple(tuple(math.sin(math.tau * k * i / SIZE) for i in range(SIZE)) for k in range(65))


def alias_risk(wave: list[float], midi_note: float = 84.0) -> float:
    frequency = 440 * 2 ** ((midi_note - 69) / 12)
    highest_safe = math.floor(24000 / frequency)
    energies = []
    for harmonic in range(1, 65):
        real = sum(value * COSINES[harmonic][i] for i, value in enumerate(wave))
        imag = sum(value * SINES[harmonic][i] for i, value in enumerate(wave))
        energies.append(real * real + imag * imag)
    total = sum(energies) or 1.0
    return sum(energies[highest_safe:]) / total


def main() -> int:
    print("case                     rel-RMSE   min corr   native>Nyq@C6  sampled>Nyq@C6")
    for case, name in enumerate(NAMES, start=1):
        waves = legacy_waves(case)
        error = 0.0
        reference = 0.0
        minimum_correlation = 1.0
        for y in GRID:
            for x in GRID:
                direct = direct_wave(case, x, y)
                sampled = legacy_wave(waves, x, y)
                error += sum((a - b) ** 2 for a, b in zip(direct, sampled))
                reference += sum(value * value for value in direct)
                dot = sum(a * b for a, b in zip(direct, sampled))
                denom = math.sqrt(sum(a * a for a in direct) * sum(b * b for b in sampled)) or 1.0
                minimum_correlation = min(minimum_correlation, dot / denom)
        native_alias = 0.0
        sampled_alias = 0.0
        for y in SPECTRAL_GRID:
            for x in SPECTRAL_GRID:
                native_alias = max(native_alias, alias_risk(direct_wave(case, x, y)))
                sampled_alias = max(sampled_alias, alias_risk(legacy_wave(waves, x, y)))
        print(f"{name:24} {math.sqrt(error / reference):8.1%}   {minimum_correlation:8.3f}"
              f"   {native_alias:12.1%}   {sampled_alias:13.1%}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
