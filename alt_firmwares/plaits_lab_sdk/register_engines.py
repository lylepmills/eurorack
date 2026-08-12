#!/usr/bin/env python3
"""Register ported engine packages into the shared build and catalog files.

Phase 2 ports 22 engines in parallel, each owning only its own engine sources
and package directory. The five files every engine would otherwise have to edit
are shared, so they are written here instead, in one serial pass:

    alt_firmwares/plaits_lab_catalog/catalog.json   engine record + manual
    plaits/test/makefile                            the .cc in the test build
    plaits/test/cpu_bench.cc                        include + bench<> line
    plaits/test/plaits_test.cc                      include + audition + validators

Everything written here is derived from the package's own plaits-engine.json,
so the package stays the single source of truth and this script stays a
mechanical projection of it. Idempotent: registering an engine twice is a no-op,
which matters because engines land in waves.

    python3 register_engines.py --list          what is unregistered
    python3 register_engines.py <id> [<id>...]  register these
    python3 register_engines.py --all           register everything unregistered
    python3 register_engines.py --sync-metadata project Braids package metadata
    python3 register_engines.py --check         non-zero if missing or out of sync
"""

import argparse
import json
import sys
from pathlib import Path

SDK_DIR = Path(__file__).resolve().parent
REPO_ROOT = SDK_DIR.parents[1]
PACKAGES = SDK_DIR / "packages" / "mutable-instruments"
CATALOG = REPO_ROOT / "alt_firmwares" / "plaits_lab_catalog" / "catalog.json"
TEST_MAKEFILE = REPO_ROOT / "plaits" / "test" / "makefile"
CPU_BENCH = REPO_ROOT / "plaits" / "test" / "cpu_bench.cc"
PLAITS_TEST = REPO_ROOT / "plaits" / "test" / "plaits_test.cc"


def load_manifest(engine_id: str) -> dict:
    path = PACKAGES / engine_id / "plaits-engine.json"
    if not path.exists():
        raise SystemExit(f"no package for {engine_id!r} at {path}")
    return json.loads(path.read_text())


def is_braids_manifest(manifest: dict) -> bool:
    return "braids" in manifest.get("tags", [])


def projected_metadata(manifest: dict) -> tuple[dict, dict]:
    """Return the catalog fields mechanically owned by a package manifest."""
    if is_braids_manifest(manifest) and "trigger" not in manifest:
        raise SystemExit(
            f'{manifest["id"]}: Braids reference packages must declare trigger metadata')
    engine = {
        "description": manifest["description"],
        "tags": manifest["tags"],
        "controls": [control["label"] for control in manifest["controls"]],
        "outputs": [manifest["outputs"]["main"], manifest["outputs"]["aux"]],
    }
    manual = {
        "controls": {
            control["id"]: control["description"]
            for control in manifest["controls"]
        },
        "trigger": manifest.get("trigger", "Restarts the engine."),
    }
    return engine, manual


def metadata_drift(manifests: list[dict]) -> list[str]:
    catalog = json.loads(CATALOG.read_text())
    engines = {engine["packageId"]: engine for engine in catalog["engines"]}
    differences = []
    for manifest in manifests:
        if not is_braids_manifest(manifest):
            continue
        engine_expected, manual_expected = projected_metadata(manifest)
        engine = engines.get(manifest["id"])
        if engine is None:
            continue
        for field, expected in engine_expected.items():
            if engine.get(field) != expected:
                differences.append(f'{manifest["catalogId"]}: engine.{field}')
        if catalog["manuals"].get(manifest["catalogId"]) != manual_expected:
            differences.append(f'{manifest["catalogId"]}: manual')
    return differences


def sync_metadata(manifests: list[dict]) -> int:
    catalog = json.loads(CATALOG.read_text())
    engines = {engine["packageId"]: engine for engine in catalog["engines"]}
    updated = []
    for manifest in manifests:
        if not is_braids_manifest(manifest):
            continue
        engine = engines.get(manifest["id"])
        if engine is None:
            raise SystemExit(f'{manifest["id"]}: package is not registered')
        engine_expected, manual_expected = projected_metadata(manifest)
        changed = False
        for field, expected in engine_expected.items():
            if engine.get(field) != expected:
                engine[field] = expected
                changed = True
        if catalog["manuals"].get(manifest["catalogId"]) != manual_expected:
            catalog["manuals"][manifest["catalogId"]] = manual_expected
            changed = True
        if changed:
            updated.append(manifest["catalogId"])
    if updated:
        CATALOG.write_text(json.dumps(catalog, indent=2) + "\n")
        print("metadata synchronized: " + ", ".join(updated))
    else:
        print("Braids metadata already synchronized")
    return 0


def source_stem(manifest: dict) -> str:
    return Path(manifest["source"]["files"][0]).name


def member_name(manifest: dict) -> str:
    return source_stem(manifest).replace(".cc", "_")


def is_registered(engine_id: str) -> bool:
    return f'"id": "{engine_id}"' in CATALOG.read_text()


def next_audition_index() -> int:
    """One past the highest NN- prefix already used by an audition render."""
    text = PLAITS_TEST.read_text()
    import re
    used = [int(m) for m in re.findall(r'RenderAuditionEngine<\w+>\("(\d+)-', text)]
    return max(used) + 1 if used else 1


def register_catalog(manifest: dict) -> None:
    text = CATALOG.read_text()
    engine_id = manifest["catalogId"]

    controls = [c["label"] for c in manifest["controls"]]
    outputs = [manifest["outputs"]["main"], manifest["outputs"]["aux"]]
    post = manifest["postProcessing"]

    record = (
        '    },\n'
        '    {\n'
        f'      "id": "{engine_id}", "packageId": "{manifest["id"]}", '
        f'"name": "{manifest["name"]}", "author": "{manifest["author"]}", '
        f'"origin": "{manifest["origin"]}", "family": "{manifest["family"]}",\n'
        f'      "description": {json.dumps(manifest["description"])}, '
        f'"tags": {json.dumps(manifest["tags"])},\n'
        f'      "controls": {json.dumps(controls)}, "outputs": {json.dumps(outputs)},\n'
        f'      "source": {{ "header": "{manifest["source"]["header"]}", '
        f'"className": "{manifest["source"]["className"]}", '
        f'"member": "{member_name(manifest)}", '
        f'"files": {json.dumps(manifest["source"]["files"])} }},\n'
        f'      "postProcessing": {{ "alreadyEnveloped": {str(post["alreadyEnveloped"]).lower()}, '
        f'"outGain": {post["outGain"]}, "auxGain": {post["auxGain"]} }}\n'
    )

    anchor = '    },\n    {\n      "id": "bowed",'
    if text.count(anchor) != 1:
        raise SystemExit("catalog.json: could not find the insertion anchor")
    text = text.replace(anchor, record + anchor, 1)

    manual_controls = {c["id"]: c["description"] for c in manifest["controls"]}
    manual = (
        f'    "{engine_id}": {{\n'
        '      "controls": {\n'
        + ",\n".join(
            f'        "{key}": {json.dumps(manual_controls[key])}'
            for key in ("harmonics", "timbre", "morph", "macro")
            if key in manual_controls
        )
        + "\n      },\n"
        f'      "trigger": {json.dumps(manifest.get("trigger", "Restarts the engine."))}\n'
        '    },\n'
    )
    manual_anchor = '    "bowed": {'
    if text.count(manual_anchor) != 1:
        raise SystemExit("catalog.json: could not find the manual anchor")
    text = text.replace(manual_anchor, manual + manual_anchor, 1)

    json.loads(text)
    CATALOG.write_text(text)


def register_build_files(manifest: dict, audition_index: int) -> None:
    stem = source_stem(manifest)
    header = manifest["source"]["header"]
    class_name = manifest["source"]["className"]
    name = manifest["name"]
    engine_id = manifest["catalogId"]

    text = TEST_MAKEFILE.read_text()
    anchor = "\t\tbowed_engine.cc \\"
    if stem not in text:
        text = text.replace(anchor, f"{anchor}\n\t\t{stem} \\", 1)
        TEST_MAKEFILE.write_text(text)

    text = CPU_BENCH.read_text()
    if header not in text:
        text = text.replace(
            '#include "plaits/dsp/engine2/bowed_engine.h"',
            f'#include "plaits/dsp/engine2/bowed_engine.h"\n#include "{header}"', 1)
        text = text.replace(
            '  bench<BowedEngine>("bowed");',
            f'  bench<BowedEngine>("bowed");\n  bench<{class_name}>("{engine_id}");', 1)
        CPU_BENCH.write_text(text)

    text = PLAITS_TEST.read_text()
    if header not in text:
        text = text.replace(
            '#include "plaits/dsp/engine2/bowed_engine.h"',
            f'#include "plaits/dsp/engine2/bowed_engine.h"\n#include "{header}"', 1)
        text = text.replace(
            '  RenderAuditionEngine<BowedEngine>("16-bowed.wav");',
            f'  RenderAuditionEngine<BowedEngine>("16-bowed.wav");\n'
            f'  RenderAuditionEngine<{class_name}>("{audition_index:02d}-{engine_id}.wav");', 1)
        text = text.replace(
            '  ValidateExperimentalEngineExtremes<BowedEngine>();',
            f'  ValidateExperimentalEngineExtremes<BowedEngine>();\n'
            f'  ValidateExperimentalEngineExtremes<{class_name}>();', 1)
        text = text.replace(
            '  ValidateExperimentalControlResponse<BowedEngine>("Bowed");',
            f'  ValidateExperimentalControlResponse<BowedEngine>("Bowed");\n'
            f'  ValidateExperimentalControlResponse<{class_name}>("{name}");', 1)
        PLAITS_TEST.write_text(text)


def discover() -> list[str]:
    return sorted(
        path.name for path in PACKAGES.iterdir()
        if path.is_dir() and (path / "plaits-engine.json").exists()
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ids", nargs="*")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--sync-metadata", action="store_true")
    arguments = parser.parse_args()

    unregistered = [i for i in discover() if not is_registered(i)]
    manifests = [load_manifest(engine_id) for engine_id in discover()]

    if arguments.sync_metadata:
        selected = manifests
        if arguments.ids:
            selected_ids = set(arguments.ids)
            selected = [
                manifest for manifest in manifests
                if manifest["catalogId"] in selected_ids
                or manifest["id"] in selected_ids
                or manifest["id"].split("/", 1)[-1] in selected_ids
            ]
            if len(selected) != len(selected_ids):
                raise SystemExit("one or more requested package IDs were not found")
        return sync_metadata(selected)

    if arguments.list or arguments.check:
        drift = metadata_drift(manifests) if arguments.check else []
        if unregistered:
            print("unregistered: " + ", ".join(unregistered))
        else:
            print("every package is registered")
        if drift:
            print("metadata drift:\n  " + "\n  ".join(drift))
        elif arguments.check:
            print("Braids package metadata matches the catalog")
        return 1 if (arguments.check and (unregistered or drift)) else 0

    targets = unregistered if arguments.all else arguments.ids
    if not targets:
        print("nothing to do (pass ids, or --all)")
        return 0

    for engine_id in targets:
        if is_registered(engine_id):
            print(f"  {engine_id}: already registered, skipping")
            continue
        manifest = load_manifest(engine_id)
        index = next_audition_index()
        register_catalog(manifest)
        register_build_files(manifest, index)
        print(f"  {engine_id}: registered as {index:02d}-{engine_id}.wav")

    print("\nNow run, from the repo root:")
    print("  make -f plaits/test/makefile && ./plaits_test")
    print("  python3 alt_firmwares/plaits_lab_catalog/validate_catalog.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
