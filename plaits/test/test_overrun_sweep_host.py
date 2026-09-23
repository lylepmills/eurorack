#!/usr/bin/env python3
"""End-to-end check of the overrun sweep's host decoder against a simulation.

Builds plaits/test/overrun_sweep_test.cc (the firmware state machine driven by
a fake cycle counter and a DAC model that replays stale frames when a fill is
late), then decodes the WAV it writes with plaits/tools/overrun_sweep_host.py
and requires:

  * every packet the module sent decodes, and its numbers match the model;
  * every stale block has a pilot glitch within 16 samples, and every pilot
    glitch during the sweep is a stale block (none missed, none invented);
  * the host's per-engine and per-ladder-step attribution matches the
    firmware's own late-fill counts.

Needs numpy + soundfile and a host C++ compiler.
    python3 plaits/test/test_overrun_sweep_host.py
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plaits/tools"))
import overrun_sweep_host as host  # noqa: E402


def nearest(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    i = np.searchsorted(b, a)
    lo = b[np.clip(i - 1, 0, len(b) - 1)]
    hi = b[np.clip(i, 0, len(b) - 1)]
    return np.minimum(np.abs(a - lo), np.abs(a - hi))


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        binary = tmp / "overrun_sweep_test"
        subprocess.run([
            "g++", "-std=c++98", "-O2", "-DTEST",
            "-DPLAITS_OVERRUN_SWEEP_ENGINES=3",
            "-DPLAITS_OVERRUN_SWEEP_TAIL_SECONDS=1", f"-I{ROOT}",
            str(ROOT / "plaits/test/overrun_sweep_test.cc"),
            str(ROOT / "plaits/resources.cc"),
            str(ROOT / "stmlib/dsp/units.cc"),
            "-o", str(binary)], check=True, stderr=subprocess.DEVNULL)
        wav, truth_path = tmp / "sim.wav", tmp / "sim.json"
        subprocess.run([str(binary), str(wav), str(truth_path)], check=True)
        truth = json.loads(truth_path.read_text())

        decoded = host.decode_capture(wav)
        report = host.build_report(decoded, None, tail_seconds=1)

    failures = []
    end = [p for p in decoded["packets"] if p.type == host.PACKET_END]
    if not end:
        failures.append("no END packet")
    sweep_end = end[0].burst if end else 1 << 62
    # OUT goes silent when the report starts, which is a (correct) break in
    # the pilot right at the END burst.
    glitches = np.array([g for g in decoded["glitches"] if g < sweep_end - 48])
    stale = np.array([s[0] for s in truth["stale_frames"]])
    invented = int(np.count_nonzero(nearest(glitches, stale) > 16))
    missed = int(np.count_nonzero(nearest(stale, glitches) > 16))
    if invented or missed:
        failures.append(f"pilot: {invented} invented, {missed} missed")

    fw_ladder = [s["late"] for s in report["ladder"]]
    if fw_ladder != truth["ladder_late"]:
        failures.append(f"ladder packet {fw_ladder} != {truth['ladder_late']}")
    for step in report["ladder"]:
        host_count = step["host_glitches"]
        if host_count is None or (host_count > 0) != (step["late"] > 0):
            failures.append(f"ladder {step['load']}: host {host_count} vs "
                            f"firmware {step['late']}")

    for e in report["engines"]:
        k = e["engine"]
        fw = e["grid"]["late"] + e["random"]["late"]
        if fw != truth["engine_late"][k]:
            failures.append(f"engine {k}: packet late {fw} != model")
        if e["tail"]["late"] != truth["tail_late"][k]:
            failures.append(f"engine {k}: tail late mismatch")
        for phase in ("grid", "random", "tail"):
            fw_phase = e[phase]["late"]
            host_phase = e["host_glitches"][phase]
            if (fw_phase == 0) != (host_phase == 0):
                failures.append(f"engine {k} {phase}: firmware {fw_phase} "
                                f"vs host {host_phase}")
        # Each engine's captured duration must match its schedule -- which
        # also proves engines without a stereo path skipped the stereo states.
        if not e.get("anchored") or abs(e["stretch"] - 1.0) > 1e-3:
            failures.append(f"engine {k}: timeline {e.get('stretch')}")
    # Engine 1 is late in exactly the gated / note-108 conditions, every
    # block of their 81 parameter states, in all three AUX modes.
    hot = {c["condition"]: c["late"]
           for c in report["engines"][1]["conditions"] if c["late"]}
    expected = {f"gated/{o}/note 108": 81 * 48
                for o in ("regular", "stereo", "sub-osc")}
    if hot != expected:
        failures.append(f"engine 1 conditions {hot}")
    tails = report["engines"][2]["tails"]
    if tails[0]["late"] != 989 or any(t["late"] for t in tails[1:]):
        failures.append("engine 2 tail cases")

    if failures:
        for f in failures:
            print("FAIL:", f)
        sys.exit(1)
    print(f"PASS test_overrun_sweep_host: {len(decoded['packets'])} packets, "
          f"{len(stale)} stale blocks all found, none invented")


if __name__ == "__main__":
    main()
