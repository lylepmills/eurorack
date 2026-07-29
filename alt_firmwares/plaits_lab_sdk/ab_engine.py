#!/usr/bin/env python3
"""Run a ported engine's A/B against the Braids model it comes from.

The Braids → Plaits Palette port claims per-engine fidelity numbers. Wave 1
produced those by hand in a scratchpad, so they are unreproducible and cannot
be re-run when a shared header changes underneath an engine. This driver makes
the A/B a committed, one-command artefact:

    python3 ab_engine.py packages/mutable-instruments/csaw

It reads `tests/ab.json` from the package, and for each case renders BOTH sides
and compares them:

  * the Braids side, via `braids/test/render_braids_model` (built on demand),
    always at `--rate 48000` so the reference is decimated to the port's rate
    (BRAIDS_PORT_SPEC.md R7 — comparing against the native 96 kHz render
    charges everything above 24 kHz to the port);
  * the port side, via the same `render_model.cc` host renderer `plaits_lab.py
    render` uses, so the numbers describe the engine as the SDK builds it.

`tests/ab.json` is a list of cases:

    [
      {
        "id": "stock",
        "note": 45,
        "seconds": 3,
        "triggerHz": 0,
        "braids": {"p1": 0.5, "p2": 0.2},
        "engine": {"timbre": 0.5, "harmonics": 0.2, "morph": 0.5, "macro": 0.5},
        "tolerance": {"acRmsDb": 1.0, "cents": 10.0, "spectrumDb": 3.0}
      }
    ]

Values are either a constant or a two-element [start, end] sweep. `braids.p1`
is TIMBRE and `braids.p2` is COLOR — Braids' only two knobs. The `engine` side
carries all four Plaits macros; the two the port adds beyond Braids belong at
their stock positions (usually 0.5) or the comparison is not an A/B of the same
sound.

`tolerance` is optional. When present the case FAILS if any metric exceeds it,
so an engine's fidelity claim becomes a regression test rather than a sentence
in a document. Exit status is non-zero if any case fails.
"""

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

SDK_DIR = Path(__file__).resolve().parent
REPO_ROOT = SDK_DIR.parents[1]

# Plaits derives pitch from kCorrectedSampleRate (47872.34 Hz, plaits/dsp/dsp.h)
# rather than a nominal 48 kHz, because the I2S clock is a divider (SPEC R6).
# render_braids_model has no such correction -- it renders at a nominal rate --
# so every ported engine reads sharp against it by
#
#     1200 * log2(48000 / 47872.34) = 4.61 cents
#
# regardless of whether the port is correct. Both csaw, which is landed and
# verified, and fold measured +4.5 to +4.7 against it. The raw figure is kept
# and the corrected one printed beside it: subtracting silently would hide the
# R5 rate-constant errors this metric exists to catch, and those are whole
# semitones, not cents.
CORRECTED_SAMPLE_RATE_CENTS = 1200.0 * __import__("math").log2(48000.0 / 47872.34)
BRAIDS_RENDERER = REPO_ROOT / "braids" / "test" / "render_braids_model"
BRAIDS_MAKEFILE = REPO_ROOT / "braids" / "test" / "makefile.render"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


plaits_lab = load_module("plaits_lab", SDK_DIR / "plaits_lab.py")
ab_compare = load_module("ab_compare", SDK_DIR / "ab_compare.py")


def ensure_braids_renderer() -> Path:
    """Build the reference renderer if it is missing or out of date."""
    source = REPO_ROOT / "braids" / "test" / "render_braids_model.cc"
    if BRAIDS_RENDERER.exists() and \
            BRAIDS_RENDERER.stat().st_mtime >= source.stat().st_mtime:
        return BRAIDS_RENDERER
    result = subprocess.run(
        ["make", "-f", str(BRAIDS_MAKEFILE)],
        cwd=str(REPO_ROOT), text=True, capture_output=True, check=False)
    if result.returncode or not BRAIDS_RENDERER.exists():
        raise SystemExit("could not build the Braids reference renderer:\n" +
                         (result.stderr or result.stdout))
    return BRAIDS_RENDERER


def as_sweep(value) -> tuple[float, float]:
    if isinstance(value, (list, tuple)):
        if len(value) != 2:
            raise SystemExit(f"sweep {value!r} must have exactly two values")
        return float(value[0]), float(value[1])
    return float(value), float(value)


def sweep_argument(value) -> str:
    start, end = as_sweep(value)
    return f"{start}:{end}" if start != end else f"{start}"


def render_braids(renderer: Path, case: dict, shape: str, output: Path) -> None:
    braids = case.get("braids", {})
    command = [
        str(renderer),
        "--shape", shape,
        "--note", str(case.get("note", 48)),
        "--seconds", str(case.get("seconds", 3)),
        "--trigger-hz", str(case.get("triggerHz", 0)),
        "--rate", "48000",
        "--p1", sweep_argument(braids.get("p1", 0.5)),
        "--p2", sweep_argument(braids.get("p2", 0.5)),
        "--out", str(output),
    ]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        raise SystemExit("Braids render failed: " +
                         (result.stderr or result.stdout).strip())


def render_engine(package: dict, renderer: Path, case: dict, output: Path) -> None:
    engine = case.get("engine", {})
    scenario = {
        "id": case.get("id", "ab"),
        "durationSeconds": case.get("seconds", 3),
        "note": case.get("note", 48),
        "triggerHz": case.get("triggerHz", 0),
        "controls": {
            name: list(as_sweep(engine.get(name, 0.5)))
            for name in ("harmonics", "timbre", "morph", "macro")
        },
    }
    plaits_lab.run_scenario(package, renderer, scenario, output)


def check_tolerance(results: dict, tolerance: dict) -> list[str]:
    failures = []
    limit = tolerance.get("acRmsDb")
    if limit is not None and abs(results["ac_rms_delta_db"]) > limit:
        failures.append(
            f"AC RMS {results['ac_rms_delta_db']:+.2f} dB exceeds ±{limit} dB")
    limit = tolerance.get("cents")
    if limit is not None:
        measured = results["pitch_delta_cents"]
        if measured is None:
            failures.append("pitch tolerance declared but no usable f0 was found")
        elif abs(measured) > limit:
            failures.append(f"pitch {measured:+.1f} cents exceeds ±{limit} cents")
    limit = tolerance.get("spectrumDb")
    if limit is not None:
        measured = results["spectral_delta_db"]
        if measured is None:
            failures.append("spectrum tolerance declared but no spectrum was computed")
        elif measured > limit:
            failures.append(f"spectrum {measured:.2f} dB exceeds {limit} dB")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description="A/B a ported engine against its Braids model.")
    parser.add_argument("package", help="path to the engine package")
    parser.add_argument("--case", help="run only this case id")
    parser.add_argument("--bands", action="store_true",
                        help="print the per-octave-band breakdown")
    parser.add_argument("--keep", metavar="DIR",
                        help="keep the rendered WAVs in DIR for listening")
    parser.add_argument("--compiler")
    arguments = parser.parse_args()

    package_dir = Path(arguments.package).resolve()
    ab_path = package_dir / "tests" / "ab.json"
    if not ab_path.exists():
        raise SystemExit(f"{ab_path} not found — this package declares no A/B")

    document = json.loads(ab_path.read_text())
    if isinstance(document, dict):
        shape = document.get("shape")
        cases = document.get("cases", [])
    else:
        shape, cases = None, document
    if arguments.case:
        cases = [case for case in cases if case.get("id") == arguments.case]
        if not cases:
            raise SystemExit(f"no case with id {arguments.case!r}")

    package = plaits_lab.load_package(str(package_dir), autodeclare=True)
    braids_renderer = ensure_braids_renderer()

    keep_dir = Path(arguments.keep).resolve() if arguments.keep else None
    if keep_dir:
        keep_dir.mkdir(parents=True, exist_ok=True)

    failures = 0
    with tempfile.TemporaryDirectory(prefix="plaits-lab-ab-") as temp:
        temp_dir = Path(temp)
        engine_renderer = plaits_lab.host_binary(temp_dir, "render-model")
        plaits_lab.compile_renderer(package, engine_renderer, arguments.compiler)

        print(f"A/B {package['manifest']['id']}  vs Braids "
              f"{shape or '(per case)'}\n")

        for case in cases:
            case_shape = case.get("shape", shape)
            if not case_shape:
                raise SystemExit(f"case {case.get('id')!r} declares no shape")
            case_id = case.get("id", "case")

            destination = keep_dir or temp_dir
            reference_wav = destination / f"{case_id}-braids.wav"
            engine_wav = destination / f"{case_id}-port.wav"

            render_braids(braids_renderer, case, case_shape, reference_wav)
            render_engine(package, engine_renderer, case, engine_wav)

            reference, rate_a = ab_compare.read_wav(str(reference_wav))
            port, rate_b = ab_compare.read_wav(str(engine_wav))

            # run_scenario applies the catalog's out_gain, which is a headroom
            # choice for the Plaits voice and not a property of the ported
            # algorithm. Undo it so the AC RMS figure describes the ENGINE.
            # A negative gain means voice.h routes the engine through
            # stmlib::Limiter (SPEC R1) — that path is non-linear, so the level
            # comparison stops being meaningful and is reported as such.
            out_gain = float(package["manifest"]["postProcessing"]["outGain"])
            limited = out_gain < 0.0
            if out_gain:
                port = [s / abs(out_gain) for s in port]

            length = min(len(reference), len(port))
            reference, port = reference[:length], port[:length]
            reference_ac = ab_compare.remove_dc(reference)
            port_ac = ab_compare.remove_dc(port)

            f0_a = ab_compare.autocorrelation_f0(reference, rate_a)
            f0_b = ab_compare.autocorrelation_f0(port, rate_b)
            overall, bands = ab_compare.spectral_difference(
                reference_ac, port_ac, rate_a)
            results = {
                "ac_rms_delta_db": ab_compare.rms_db(port_ac) -
                                   ab_compare.rms_db(reference_ac),
                "pitch_delta_cents": ab_compare.cents(f0_a, f0_b),
                "spectral_delta_db": overall,
            }

            if results["pitch_delta_cents"] is None:
                pitch = "no f0"
            else:
                corrected = results["pitch_delta_cents"] - CORRECTED_SAMPLE_RATE_CENTS
                results["pitch_delta_cents"] = corrected
                pitch = f"{corrected:+.1f} cents"
            spectrum = ("n/a" if overall is None else f"{overall:.2f} dB")
            print(f"  {case_id:<20} {case.get('name', '')}")
            print(f"    AC RMS {results['ac_rms_delta_db']:+6.2f} dB   "
                  f"pitch {pitch:>14}   spectrum {spectrum:>9}")
            if limited:
                print("    note   out_gain is negative, so the render passed "
                      "through the limiter; the level figure is not linear")

            if arguments.bands and bands:
                for band in bands:
                    print(f"      {band['low_hz']:8.0f} - {band['high_hz']:8.0f} Hz"
                          f"  {band['delta_db']:+6.2f} dB"
                          f"  ({band['share'] * 100.0:4.1f}% of energy)")

            problems = check_tolerance(results, case.get("tolerance", {}))
            for problem in problems:
                print(f"    FAIL  {problem}")
            failures += len(problems)
            print()

    if failures:
        print(f"{failures} tolerance failure(s)")
        return 1
    print("all cases within declared tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main())
