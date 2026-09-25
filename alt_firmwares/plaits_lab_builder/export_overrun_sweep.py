#!/usr/bin/env python3
"""Export the on-module overrun-sweep diagnostic firmware, one group per flash.

python3 alt_firmwares/plaits_lab_builder/export_overrun_sweep.py \
  --order risk_order.json --group 1 build/overrun-sweep-1

The catalog is split into 24-engine groups (three banks, the flash-safe size).
Each group's registry slot 0 -- the first amber slot -- holds a very cheap
engine, because the sweep's calibration ladder runs on engine 0 and adds a
busy-wait on top of it. The remaining 23 slots take the next engines from
--order (most CPU-risky first, so group 1 answers the open questions).

The recipe is schema 2, whose global-stereo back-compat compiles every
engine's stereo path, and whose defaults leave the Experimental features
(Sync In, linear TZFM, Fast FM) out -- the normal-use firmware the sweep is
meant to certify. The build is the exported recipe's own build.sh with
PLAITS_OVERRUN_SWEEP=1 injected; see plaits/overrun_sweep.h.

Writes manifest.json (registry-order engine ids) for
plaits/tools/overrun_sweep_host.py.
"""

import argparse
import json
import re
import subprocess
from pathlib import Path

import copy

from container_server import STEREO_MACROS
from export_recipe_source import export_recipe_source
from generate_engine_config import (
    DEFAULT_CHORD_TABLES,
    DEFAULT_CONFIGURATION,
    DEFAULT_SCALE_BANK,
    MAX_RECIPE_SCHEMA_VERSION,
)

CATALOG = json.loads((Path(__file__).resolve().parents[1] /
                      "plaits_lab_catalog/catalog.json").read_text())
GROUP_SIZE = 24
# Cheapest engines in the 2026-09-23 QEMU audit (<= 23% in stereo and mono):
# one hosts the ladder in each group.
LADDER_HOSTS = ["csaw", "saw-square", "digital-modulation", "sub-oscillator"]
# Recipe slots are green, red, amber; the three-bank registry runs amber,
# green, red. Registry index 0 is therefore recipe slot 16.
REGISTRY_ZERO_SLOT = 16


def groups(order: list[str]) -> list[list[str]]:
    rest = [e for e in order if e not in LADDER_HOSTS]
    size = GROUP_SIZE - 1
    chunks = [rest[i:i + size] for i in range(0, len(rest), size)]
    if len(chunks) > len(LADDER_HOSTS):
        raise ValueError("more groups than ladder hosts")
    return [[LADDER_HOSTS[i]] + chunk for i, chunk in enumerate(chunks)]


def registry_order(engine_config: str, slots: list[str]) -> list[str]:
    """Engine ids in registry order: the amber, green, red rotation, checked
    member by member against the generated registration."""
    names = {e["id"]: e for e in CATALOG["engines"]}
    engines = [s for s in slots[16:24] + slots[0:8] + slots[8:16] if s]
    if len(set(engines)) != len(engines):
        raise ValueError("duplicate slots collapse; registry order would shift")
    members = re.findall(r"\(registry\)\.RegisterInstance\(\s*&(\w+)",
                         engine_config)
    expected = [names[e]["source"]["member"] for e in engines]
    if members != expected:
        raise ValueError("registration order does not match the bank rotation")
    return engines


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--order", type=Path,
                        help="JSON list of every catalog engine id, riskiest first")
    parser.add_argument("--group", type=int, required=True, help="1-based")
    parser.add_argument("--stereo", action="store_true",
                        help="stereo AUX with every engine's stereo path "
                             "compiled in (a schema-2 recipe compiles the "
                             "per-engine stereo paths out)")
    parser.add_argument("--threshold", action="store_true",
                        help="build plaits/threshold_ladder.h instead of the "
                             "sweep (production signal path, no sweep code)")
    parser.add_argument("--engines", nargs="+",
                        help="an explicit follow-up group instead of --order: "
                             "the first id hosts the ladder, so make it cheap")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    catalog_ids = {e["id"] for e in CATALOG["engines"]}
    if args.engines:
        unknown = sorted(set(args.engines) - catalog_ids)
        if unknown or len(args.engines) > GROUP_SIZE:
            raise ValueError(f"bad --engines (unknown {unknown}, or > {GROUP_SIZE})")
        group = list(args.engines)
    else:
        order = json.loads(args.order.read_text())
        missing = catalog_ids - set(order)
        if missing:
            raise ValueError(f"--order is missing catalog engines: {sorted(missing)}")
        group = groups([e for e in order if e in catalog_ids])[args.group - 1]

    slots = [None] * GROUP_SIZE
    slots[REGISTRY_ZERO_SLOT] = group[0]
    free = [i for i in range(GROUP_SIZE) if i != REGISTRY_ZERO_SLOT]
    for slot, engine_id in zip(free, group[1:]):
        slots[slot] = engine_id
    # A short group: the stereo (latest-schema) recipe leaves the spare slots
    # empty; schema 2 cannot, so it repeats the ladder host, and the registry
    # check below refuses that because duplicates would shift the order.
    if not args.stereo:
        slots = [s if s is not None else group[0] for s in slots]

    recipe = {
        "schemaVersion": 2,
        "target": "mutable-instruments-plaits",
        "firmware": "rubato-plaits",
        "output": "audio-wav",
        "slots": slots,
    }
    if args.stereo:
        # The latest schema, spelled the way the builder's own tests spell it,
        # with the Experimental features explicitly off.
        options = copy.deepcopy(DEFAULT_CONFIGURATION["initialOptions"])
        options.update({"auxOutput": "stereo", "attenuverterMode": "stock",
                        "trigResponse": "trigger"})
        recipe.update({
            "schemaVersion": MAX_RECIPE_SCHEMA_VERSION,
            "preferences": {
                "navigationMode": "linear", "calibration": False,
                "colorBlindMode": False, "replaceableFmBanks": False,
                "syncInput": False, "linearTzfm": False, "fastFm": False,
                "simplifiedPitchRanges": False, "envelopeContour": False,
            },
            "initialOptions": options,
            "stereoEngines": sorted({s for s in slots if s in STEREO_MACROS}),
            "resources": {"chordTables": DEFAULT_CHORD_TABLES,
                          "scaleBank": DEFAULT_SCALE_BANK},
        })
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(
        ["git", "status", "--porcelain"], text=True).strip())
    export_recipe_source(recipe, args.output,
                         source_revision=revision, source_dirty=dirty)

    build = args.output / "build.sh"
    source = build.read_text(encoding="utf-8")
    anchor = '  "CC=arm-none-eabi-gcc"'
    if source.count(anchor) != 1:
        raise ValueError("Exporter build template changed; recheck flag injection")
    flags = ("-DPLAITS_THRESHOLD_LADDER=1" if args.threshold else
             f"-DPLAITS_OVERRUN_SWEEP=1 -DPLAITS_OVERRUN_SWEEP_GROUP={args.group}")
    defines = (
        f'  "PROJECT_CONFIGURATION={flags}" \\\n'
        '  RESOURCES= \\\n'
    )
    build.write_text(source.replace(anchor, defines + anchor), encoding="utf-8")

    engine_config = (args.output / "engine_config.h").read_text(encoding="utf-8")
    engines = registry_order(engine_config, slots)
    if engines[0] != group[0]:
        raise ValueError(f"registry slot 0 is {engines[0]}, not {group[0]}")
    manifest = {"group": args.group, "engines": engines,
                "sourceRevision": revision, "sourceDirty": dirty}
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"group {args.group}: {len(engines)} engines, ladder on {engines[0]}")
    for i, e in enumerate(engines):
        print(f"  {i:2d} {e}")


if __name__ == "__main__":
    main()
