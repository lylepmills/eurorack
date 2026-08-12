#!/usr/bin/env python3
"""Measure factory and custom Wave Terrain marginal flash costs.

Run inside the plaits-lab-builder container. The control is the stock-24
palette with all eight factory terrains made explicit. Each measurement removes
one terrain and reports full - removed, so shared engine code stays paid for and
the result matches the per-entry estimate the browser should show.
"""

import hashlib
import base64
import json
import sys

sys.path.insert(0, "/work/alt_firmwares/plaits_lab_builder")

import container_server as cs
from generate_engine_config import DEFAULT_CHORD_TABLES, DEFAULT_CONFIGURATION


BASE = json.load(open("/work/alt_firmwares/plaits_lab_builder/default_recipe.json"))
CATALOG = json.load(open("/work/alt_firmwares/plaits_lab_catalog/catalog.json"))
BASE["slots"] = list(CATALOG["presets"]["stock"])
# The explicit v24 bank adds a little dispatch code to a stock palette that is
# already near the limit. Replace Speech with a duplicate of Virtual Analog in
# every arm to create link room without changing Wave Terrain or Wavetable's
# shared resources. Remove the three DX7 slots too: this diagnostic is measuring
# built-in terrain code, not the large shared FM core or its factory banks.
BASE["slots"][BASE["slots"].index("speech")] = "virtual-analog"
for bank in ("dx7-bank-a", "dx7-bank-b", "dx7-bank-c"):
    BASE["slots"][BASE["slots"].index(bank)] = "virtual-analog"
FACTORIES = [f"factory-{number}" for number in range(1, 9)]


def terrain_recipe(factory_ids):
    recipe = json.loads(json.dumps(BASE))
    recipe["schemaVersion"] = 24
    recipe["preferences"] = {
        "navigationMode": "linear",
        "calibration": False,
        "colorBlindMode": False,
        "replaceableFmBanks": False,
        "syncInput": False,
    }
    recipe["initialOptions"] = {
        **DEFAULT_CONFIGURATION["initialOptions"],
        "attenuverterMode": "stock",
    }
    recipe["resources"] = {
        "chordTables": DEFAULT_CHORD_TABLES,
        "terrainBank": [
            {"kind": "factory", "id": factory_id}
            for factory_id in factory_ids
        ],
    }
    return recipe


def with_custom(factory_ids, equation, *, native):
    recipe = terrain_recipe(factory_ids)
    model = {
        "kind": "wave-terrain",
        "name": "Flash diagnostic",
        "equation": equation,
        "data": base64.b64encode(bytes(4096)).decode("ascii"),
    }
    if native:
        model["representation"] = "native"
    recipe["resources"]["terrainBank"].append({"kind": "custom", "model": model})
    return recipe


def build_size(tag, recipe):
    key = hashlib.sha256(json.dumps(recipe, sort_keys=True).encode()).hexdigest()
    try:
        artifact_path, _output, _metadata = cs.build_firmware({
            "buildKey": key,
            "recipe": recipe,
        })
    except Exception as error:
        detail = getattr(error, "detail", "")
        if detail:
            print(detail[-4000:], flush=True)
        raise
    elf = next(artifact_path.parent.rglob("*.elf"))
    text, data, _bss = cs.parse_size(elf)
    total = text + data
    print(f"{tag:<24} {total:>8}", flush=True)
    return total


def main():
    full = build_size("all-eight", terrain_recipe(FACTORIES))
    results = {}
    for factory_id in FACTORIES:
        without = [candidate for candidate in FACTORIES if candidate != factory_id]
        total = build_size(f"without-{factory_id}", terrain_recipe(without))
        results[factory_id] = full - total
    custom = {
        "prebaked": build_size(
            "plus-prebaked", with_custom(FACTORIES, "x + y", native=False)
        ) - full,
    }
    native_equations = {
        "linear": "x + y",
        "soft-rings": "sin(10 * r)",
        "four-chambers": "sign(x * y) * (1 - 0.55 * r) + 0.2 * x",
        "lone-island": "max(0, 1 - 2.2 * sqrt((x + 0.25)^2 + (y - 0.15)^2))",
    }
    for name, equation in native_equations.items():
        custom[f"native-{name}"] = build_size(
            f"native-{name}", with_custom(FACTORIES, equation, native=True)
        ) - full
    print(json.dumps({"factory": results, "custom": custom}, indent=2), flush=True)


if __name__ == "__main__":
    main()
