#!/usr/bin/env python3
"""Create the autonomous three-bank Carrier Tilt hardware diagnostic.

python3 alt_firmwares/plaits_lab_builder/export_fm_carrier_diagnostic.py \
  /path/to/website/prototypes/dx7-controls/generated/patches.json \
  build/fm-carrier-diagnostic

The generated firmware ignores panel/CV values while it runs an exhaustive
on-target sweep, then holds a pass/fail LED result and plays a repeating audible
audition. It never erases an existing runtime-transferred bank.
"""

import argparse
import base64
import copy
import json
import subprocess
from pathlib import Path

from export_recipe_source import export_recipe_source
from generate_engine_config import DEFAULT_CHORD_TABLES, DEFAULT_CONFIGURATION


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("patches", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    rows = json.loads(args.patches.read_text(encoding="utf-8"))
    if not 65 <= len(rows) <= 96:
        raise ValueError("The diagnostic requires three populated banks (65-96 patches)")

    slots = [None] * 24
    assignments = []
    patch_counts = []
    for bank in range(3):
        voices = []
        selected = rows[bank * 32:(bank + 1) * 32]
        patch_counts.append(len(selected))
        for offset, row in enumerate(selected):
            packed = list(base64.b64decode(row["b64"], validate=True))
            if len(packed) != 128 or (packed[110] & 31) != 31:
                raise ValueError("Only complete algorithm-32 voices belong in this diagnostic")
            if row["bank"] != bank or row["position"] != offset + 1:
                raise ValueError("Patch positions do not match the browser bank map")
            voices.append({"packed": packed})
        slot = bank * 8
        slots[slot] = "dx7-bank-a"
        assignments.append({"slot": slot, "bank": {"voices": voices}})

    options = copy.deepcopy(DEFAULT_CONFIGURATION)
    options["preferences"]["navigationMode"] = "banked"
    options["initialOptions"]["holdOnTrigger"] = False
    options["initialOptions"]["attenuverterMode"] = "stock"
    recipe = {
        "schemaVersion": 22,
        "target": "mutable-instruments-plaits",
        "firmware": "rubato-plaits",
        "output": "audio-wav",
        "slots": slots,
        **options,
        "resources": {
            "chordTables": DEFAULT_CHORD_TABLES,
            "userDataBanks": assignments,
        },
    }

    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], text=True).strip())
    export_recipe_source(
        recipe, args.output, source_revision=revision, source_dirty=dirty)

    build = args.output / "build.sh"
    source = build.read_text(encoding="utf-8")
    anchor = '  "CC=arm-none-eabi-gcc"'
    if source.count(anchor) != 1:
        raise ValueError("Exporter build template changed; recheck diagnostic flag injection")
    defines = (
        '  "PROJECT_CONFIGURATION=-DPLAITS_FM_CARRIER_DIAGNOSTIC=1 '
        '-DPLAITS_CPU_PROBE=1" \\\n'
        '  RESOURCES= \\\n'
    )
    build.write_text(source.replace(anchor, defines + anchor), encoding="utf-8")

    metadata_path = args.output / "build-metadata.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    metadata["diagnostic"] = {
        "compileDefines": ["PLAITS_FM_CARRIER_DIAGNOSTIC=1", "PLAITS_CPU_PROBE=1"],
        "patchCounts": patch_counts,
        "automaticSweep": {
            "timbre": [0.02, 0.25, 0.5, 0.75, 0.98],
            "notes": [12, 48, 84, 108],
            "articulation": ["drone", "triggered"],
            "passCriteria": "peak render cost below 90%, no missed deadline, no patch numerically silent at centre in both drone and triggered modes",
        },
        "afterSweep": "LED pass/fail held while a six-scene automatic audible loop repeats",
        "runtimeUploadedBanks": "read-bypassed, not erased",
    }
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    (args.output / "patches.json").write_text(
        json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    (args.output / "DIAGNOSTIC.txt").write_text(
        "Carrier Tilt hardware diagnostic\n\n"
        "1. Flash the generated WAV through Plaits' normal audio bootloader.\n"
        "2. Leave every input unpatched. The first 22.72 seconds are an automatic, muted stress sweep. A green progress bar fills across the LEDs. Do not touch the controls during this pass.\n"
        "3. When the sweep finishes, animated green is PASS. On failure, separated red pairs identify every cause: the top pair means peak synthesis cost reached 90%; the middle pair means a render actually missed its deadline; the bottom pair means at least one patch produced no real sample at the centre in either drone or triggered operation. Report which pair or pairs blink.\n"
        "4. At the same time, a 24-second six-scene loop begins on OUT/AUX and repeats. The automated verdict does not require an output cable. For the optional listening check, patch OUT to a monitor, start quietly, and let the loop repeat once. It changes patches, pitch, triggered/drone operation, and sweeps TIMBRE from centre to both ends automatically. Listen for clicks, dropouts, near-silence, or a strong level collapse at an end. No knob movement or input patching is required.\n\n"
        "This diagnostic ignores but does not erase any bank previously transferred over TIMBRE. Reflash normal firmware afterwards.\n",
        encoding="utf-8",
    )
    print(
        f"Prepared {len(rows)} algorithm-32 patches in {patch_counts}; "
        f"autonomous diagnostic build: {build}")


if __name__ == "__main__":
    main()
