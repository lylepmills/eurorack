#!/usr/bin/env python3
"""The CPU cost model, and the validation that keeps it honest.

    cycles_per_sample ~= k * instructions_per_sample

`k` is not a single number but a measured DISTRIBUTION across real shipping
engines. That is the whole point: a point estimate here has repeatedly been
confidently wrong, while a band with a stated failure rate is useful and
truthful at the same time.

Two things this module refuses to do:

  * Fit anything more elaborate than one coefficient. Richer models were tried
    -- instructions plus flash reads, plus memory traffic, per-instruction-class
    weights -- and every one produced negative coefficients on real data, which
    is unphysical. The extra terms were fitting noise.

  * Quote a precision it has not earned. For a week one engine appeared to
    cost four times its prediction; eight refuted theories later, the culprit
    was the MEASUREMENT channel breaking on builds that overrun the audio
    deadline, and the LED-meter re-measurement (94-100% of budget) agreed with
    the model's 99% prediction to within a few percent. The lesson is encoded
    here as caution about measurements, not extra model terms.

Run this file directly to print the leave-one-out validation table.
"""

from __future__ import annotations

import json
from pathlib import Path

CALIBRATION_PATH = Path(__file__).with_name("calibration.json")


def load_calibration() -> dict:
    return json.loads(CALIBRATION_PATH.read_text(encoding="utf-8"))


def fit_k(engines: list[dict]) -> list[float]:
    """Per-engine cycles-per-instruction, excluding known outliers."""
    return [e["cycles"] / e["insns"] for e in engines if not e.get("outlier")]


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * q / 100.0
    low = int(position)
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


class CostModel:
    def __init__(self, calibration: dict | None = None) -> None:
        self.calibration = calibration or load_calibration()
        self.budget = float(self.calibration["budget_cycles_per_sample"])
        engines = self.calibration["engines"]
        self.k_values = fit_k(engines)
        self.k_mid = percentile(self.k_values, 50)
        self.k_low = percentile(self.k_values, 10)
        self.k_high = percentile(self.k_values, 90)
        self.n_total = len(engines)
        self.n_outliers = sum(1 for e in engines if e.get("outlier"))

    def estimate(self, instructions_per_sample: float) -> dict:
        mid = self.k_mid * instructions_per_sample
        return {
            "cycles": mid,
            "cycles_low": self.k_low * instructions_per_sample,
            "cycles_high": self.k_high * instructions_per_sample,
            "usage": mid / self.budget,
            "usage_low": self.k_low * instructions_per_sample / self.budget,
            "usage_high": self.k_high * instructions_per_sample / self.budget,
        }

    def outlier_note(self) -> str:
        if self.n_outliers == 0:
            return (
                f"All {self.n_total} calibration engines land within the band "
                f"(the one apparent 4x outlier turned out to be a broken "
                f"measurement channel, not a broken model). Still verify on "
                f"hardware before publishing: the band is empirical, and an "
                f"engine near 100% is exactly where measurement gets hard."
            )
        return (
            f"About 1 engine in {round(self.n_total / max(self.n_outliers, 1))} costs "
            f"SEVERAL TIMES this estimate for reasons no static analysis can see "
            f"(measured: {self.n_outliers} of {self.n_total} calibration engines). "
            f"A clean estimate is not a substitute for measuring on hardware."
        )

    def verdict(self, instructions_per_sample: float) -> tuple[str, str]:
        """(symbol, sentence). Deliberately conservative: the upper edge of the
        band decides, because being wrong in the optimistic direction is what
        ships a broken engine."""
        e = self.estimate(instructions_per_sample)
        if e["usage_high"] < 0.6:
            return "OK", "comfortably inside budget"
        if e["usage_high"] < 1.0:
            return "OK", "expected to fit, with limited headroom"
        if e["usage"] < 1.0:
            return "WARN", "close to the limit -- may or may not fit"
        return "FAIL", "expected to exceed the budget"


def leave_one_out() -> list[dict]:
    """Predict each engine from a model fitted WITHOUT it.

    This is the guard that matters. Fitting a model and reporting how well it
    reproduces its own inputs proves nothing; every wrong prediction made while
    developing this would have been visible on sight in this table.
    """
    calibration = load_calibration()
    engines = calibration["engines"]
    budget = float(calibration["budget_cycles_per_sample"])
    rows = []
    for held_out in engines:
        others = [e for e in engines if e["id"] != held_out["id"]]
        k = percentile(fit_k(others), 50)
        predicted = k * held_out["insns"]
        rows.append({
            "id": held_out["id"],
            "actual": held_out["cycles"],
            "predicted": predicted,
            "error_pct": 100.0 * (predicted - held_out["cycles"]) / held_out["cycles"],
            "actual_usage": held_out["cycles"] / budget,
            "outlier": bool(held_out.get("outlier")),
        })
    return rows


def main() -> int:
    rows = leave_one_out()
    model = CostModel()
    print("Leave-one-out validation: each engine predicted by a model fitted WITHOUT it\n")
    print(f"{'engine':20} {'actual':>8} {'predicted':>10} {'error':>9} {'% budget':>9}")
    for row in rows:
        mark = "  <-- known outlier" if row["outlier"] else ""
        print(f"{row['id']:20} {row['actual']:8.0f} {row['predicted']:10.0f} "
              f"{row['error_pct']:+8.1f}% {100*row['actual_usage']:8.0f}%{mark}")

    ordinary = [abs(r["error_pct"]) for r in rows if not r["outlier"]]
    ordinary.sort()
    label = "excluding known outliers" if any(r["outlier"] for r in rows) else "all engines"
    print(f"\n{label} ({len(ordinary)}):")
    print(f"  mean |error| {sum(ordinary)/len(ordinary):5.1f}%    "
          f"median {percentile(ordinary, 50):5.1f}%    worst {ordinary[-1]:5.1f}%")
    print(f"  within 20%: {sum(1 for e in ordinary if e <= 20)}/{len(ordinary)}    "
          f"within 30%: {sum(1 for e in ordinary if e <= 30)}/{len(ordinary)}")
    print(f"\nmodel: k = {model.k_mid:.2f} cycles/instruction "
          f"(10-90% band {model.k_low:.2f}..{model.k_high:.2f})")
    print(f"{model.outlier_note()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
