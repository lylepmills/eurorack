#!/usr/bin/env python3
"""Resumable parameter-extreme CPU audit for every stereo catalog model.

Each model is measured on the emulated Cortex-M4 in stereo, under both an
unpatched trigger and periodic patched trigger edges. Results are written after
every model so a long catalog run can be resumed safely.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
from pathlib import Path

QEMU_DIR = Path(__file__).resolve().parent
REPO_ROOT = QEMU_DIR.parents[2]
CATALOG_PATH = REPO_ROOT / "alt_firmwares/plaits_lab_catalog/catalog.json"
ESTIMATE_PATH = QEMU_DIR / "estimate.py"


def source_revision() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, text=True,
        capture_output=True, check=True,
    )
    return result.stdout.strip()


def stereo_catalog_ids() -> list[str]:
    """Catalog entries whose engine class explicitly advertises stereo.

    Today every public model is stereo-capable, but deriving the list from the
    implementation keeps a future mono-only contribution out of this gate.
    """
    catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    ids = []
    for engine in catalog["engines"]:
        header = REPO_ROOT / engine["source"]["header"]
        source = header.read_text(encoding="utf-8", errors="replace")
        if re.search(r"\bstereo_capable\s*\(\s*\)", source):
            ids.append(engine["id"])
    return ids


def estimate_command(engine_id: str, sweep: str, image: str,
                     blocks_a: int, blocks_b: int) -> list[str]:
    return [
        sys.executable, str(ESTIMATE_PATH), "--builtin", engine_id,
        "--stereo", "--sweep", sweep, "--trigger", "both", "--json",
        "--quiet", "--image", image,
        "--blocks-a", str(blocks_a), "--blocks-b", str(blocks_b),
    ]


def measure(engine_id: str, args: argparse.Namespace) -> dict:
    command = estimate_command(
        engine_id, args.sweep, args.image, args.blocks_a, args.blocks_b)
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    result = subprocess.run(
        command, cwd=REPO_ROOT, text=True, capture_output=True,
        check=False, env=environment,
    )
    if result.returncode:
        detail = (result.stderr or result.stdout)[-4000:]
        raise RuntimeError(detail.strip() or f"estimate exited {result.returncode}")
    try:
        payload = json.loads(result.stdout.strip().splitlines()[-1])
    except (IndexError, json.JSONDecodeError) as error:
        raise RuntimeError(f"estimate returned no JSON: {result.stdout[-1000:]}") from error
    return payload


def risk(result: dict, hardware_threshold: float = 0.85) -> str:
    estimate = result["estimate"]
    if estimate["usage"] >= 1.0:
        return "FAIL"
    if estimate["usage"] >= hardware_threshold or estimate["usage_high"] >= 1.0:
        return "HARDWARE"
    return "OK"


def empty_state(revision: str, args: argparse.Namespace) -> dict:
    return {
        "schemaVersion": 1,
        "sourceRevision": revision,
        "mode": "stereo",
        "sweep": args.sweep,
        "triggers": ["unpatched", "periodic"],
        "blocks": {"a": args.blocks_a, "b": args.blocks_b},
        "results": {},
        "errors": {},
    }


def load_state(path: Path, revision: str, args: argparse.Namespace) -> dict:
    if not path.exists():
        return empty_state(revision, args)
    state = json.loads(path.read_text(encoding="utf-8"))
    expected = (revision, args.sweep, args.blocks_a, args.blocks_b)
    found = (
        state.get("sourceRevision"), state.get("sweep"),
        state.get("blocks", {}).get("a"), state.get("blocks", {}).get("b"),
    )
    if found != expected:
        raise SystemExit(
            f"refusing to mix audit workloads in {path}\n"
            f"  found:    revision={found[0]} sweep={found[1]} blocks={found[2:] }\n"
            f"  expected: revision={expected[0]} sweep={expected[1]} blocks={expected[2:]}"
        )
    return state


def save_state(path: Path, state: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def print_summary(state: dict, hardware_threshold: float) -> None:
    rows = sorted(
        state["results"].items(),
        key=lambda item: item[1]["estimate"]["usage_high"], reverse=True,
    )
    print("\nStereo CPU audit (sorted by conservative upper bound)")
    print(f"{'risk':9} {'model':28} {'mid':>6} {'band':>13} {'worst workload'}")
    for engine_id, result in rows:
        estimate = result["estimate"]
        worst = result["worst"]
        band = f"{100*estimate['usage_low']:.0f}-{100*estimate['usage_high']:.0f}%"
        workload = f"{worst['trigger']}/{worst['position']}"
        print(f"{risk(result, hardware_threshold):9} {engine_id:28} "
              f"{100*estimate['usage']:5.0f}% {band:>13} {workload}")
    if state["errors"]:
        print("\nErrors:")
        for engine_id, error in sorted(state["errors"].items()):
            print(f"  {engine_id}: {error.splitlines()[-1]}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", action="append", default=[],
                        help="audit only this catalog id (repeatable)")
    parser.add_argument("--sweep", choices=("quick", "extreme"), default="extreme")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--image", default="plaits-lab-builder:local")
    parser.add_argument("--blocks-a", type=int, default=200)
    parser.add_argument("--blocks-b", type=int, default=400)
    parser.add_argument("--hardware-threshold", type=float, default=0.85,
                        help="midpoint usage that requires a hardware probe")
    parser.add_argument("--output", type=Path,
                        help="resumable JSON result path (default: /tmp, revision-named)")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    if args.blocks_b <= args.blocks_a:
        parser.error("--blocks-b must be greater than --blocks-a")

    revision = source_revision()
    output = args.output or Path(f"/tmp/plaits-stereo-cpu-audit-{revision[:12]}.json")
    available = stereo_catalog_ids()
    unknown = sorted(set(args.engine) - set(available))
    if unknown:
        parser.error(f"unknown or non-stereo catalog ids: {', '.join(unknown)}")
    engine_ids = args.engine or available
    state = load_state(output, revision, args)
    pending = [engine_id for engine_id in engine_ids if engine_id not in state["results"]]

    print(f"source {revision[:12]}: {len(engine_ids)} stereo models, "
          f"{len(pending)} pending, {args.jobs} workers", flush=True)
    print(f"resumable results: {output}", flush=True)
    if pending:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = {executor.submit(measure, engine_id, args): engine_id
                       for engine_id in pending}
            for future in concurrent.futures.as_completed(futures):
                engine_id = futures[future]
                try:
                    result = future.result()
                except Exception as error:  # noqa: BLE001 - preserve all catalog failures
                    state["errors"][engine_id] = str(error)
                    print(f"ERROR     {engine_id}: {str(error).splitlines()[-1]}", flush=True)
                else:
                    state["results"][engine_id] = result
                    state["errors"].pop(engine_id, None)
                    estimate = result["estimate"]
                    print(f"{risk(result, args.hardware_threshold):9} {engine_id:28} "
                          f"{100*estimate['usage']:5.0f}% "
                          f"({100*estimate['usage_low']:.0f}-{100*estimate['usage_high']:.0f}%)",
                          flush=True)
                save_state(output, state)

    print_summary(state, args.hardware_threshold)
    failures = [result for result in state["results"].values()
                if risk(result, args.hardware_threshold) == "FAIL"]
    return 1 if state["errors"] else 2 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
