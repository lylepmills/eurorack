#!/usr/bin/env python3
"""Validate the authoritative Plaits Lab package catalog."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any


CATALOG_DIR = Path(__file__).resolve().parent
REPO_ROOT = CATALOG_DIR.parents[1]
CATALOG_PATH = CATALOG_DIR / "catalog.json"
SHARED_MODULES_PATH = CATALOG_DIR / "shared_modules.json"
ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*$")
PACKAGE_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9-]*$")
CONTROL_IDS = ("harmonics", "timbre", "morph", "macro")

# These panel-order mappings were audited against Mutable Instruments' Plaits
# manual and the corresponding engine source. Keep them explicit: a generic
# four-item length check cannot detect a TIMBRE/MORPH swap.
AUDITED_STOCK_CONTROLS = {
    "virtual-analog": ["Detune", "Variable square", "Variable saw", "Oscillator balance"],
    "waveshaping": ["Waveshaper waveform", "Fold amount", "Waveform asymmetry", "Wavefolder symmetry"],
    "granular-formant": ["Formant ratio", "Formant frequency", "Formant width and shape", "Carrier bleed"],
    "harmonic": ["Spectral bumps", "Prominent harmonic", "Bump shape", "Odd/even balance"],
    "chords": ["Chord", "Inversion and transposition", "Waveform", "Voice-group balance"],
    "speech": ["Model and word bank", "Species", "Phoneme or word", "Spectral emphasis"],
    "swarm": ["Pitch randomization", "Grain density", "Grain duration and overlap", "Size dispersion"],
    "filtered-noise": ["Filter response", "Clock frequency", "Resonance", "Filter position"],
    "particle-noise": ["Frequency randomization", "Density", "Filter type", "Diffusion"],
    "analog-bass-drum": ["Attack and overdrive", "Brightness", "Decay", "Pitch-envelope punch"],
    "analog-snare": ["Harmonic/noise balance", "Mode balance", "Decay", "Shell inharmonicity"],
    "analog-hi-hat": ["Metal/noise balance", "High-pass cutoff", "Decay", "Oscillator spacing"],
    "virtual-analog-vcf": ["Resonance and filter character", "Filter cutoff", "Waveform and sub level", "Drive"],
    "phase-distortion": ["Distortion frequency", "Distortion amount", "Asymmetry", "Modulator ratio"],
    "wave-terrain": ["Terrain", "Path radius", "Path offset", "Y offset"],
    "string-machine": ["Chord", "Filter and chorus", "Waveform and registration", "Ensemble amount"],
    "chiptune": ["Chord", "Arpeggio or inversion", "Pulse width and sync", "Register preset"],
    "z-filter": ["Model", "Cutoff", "Shape", "Bend"],
}

AUDITED_STOCK_OUTPUTS = {
    "two-op-fm": ["FM voice", "Sub-oscillator"],
    "filtered-noise": [
        "Filtered noise",
        "Mono: two separated band-pass voices. Stereo: decorrelated matching filter.",
    ],
    "dx7-bank-a": [
        "Six-operator FM voice; left channel in stereo",
        "Same voice mix in mono; right channel in stereo",
    ],
    "dx7-bank-b": [
        "Six-operator FM voice; left channel in stereo",
        "Same voice mix in mono; right channel in stereo",
    ],
    "dx7-bank-c": [
        "Six-operator FM voice; left channel in stereo",
        "Same voice mix in mono; right channel in stereo",
    ],
    "string-machine": ["Voices 1 and 3 predominantly", "Voices 2 and 4 predominantly"],
    "chords": [
        "Mono: full chord. Stereo: left side of the chord spread.",
        "Mono: root or alternate voice group. Stereo: right side of the chord spread.",
    ],
    "chiptune": ["Chiptune chord or arpeggio", "NES triangle bass"],
}

AUDITED_STOCK_TRIGGERS = {
    "chiptune": "Advances the arpeggiator and restarts its envelope on each patched TRIG pulse.",
}


def load_catalog() -> dict[str, Any]:
    value = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if value.get("schemaVersion") != 1 or value.get("sdk") != "plaits-engine-cpp-v1":
        raise ValueError("unsupported catalog schema")
    return value


def package_digest(engine: dict[str, Any]) -> str:
    digest = hashlib.sha256()
    package_record = {key: value for key, value in engine.items() if key != "digest"}
    digest.update(json.dumps(package_record, sort_keys=True, separators=(",", ":")).encode("utf-8"))
    source_paths = [engine["source"]["header"], *engine["source"]["files"]]
    for relative in sorted(set(source_paths)):
        path = (REPO_ROOT / relative).resolve()
        path.relative_to(REPO_ROOT)
        digest.update(relative.encode("utf-8"))
        digest.update(path.read_bytes())
    return "sha256:" + digest.hexdigest()


def documentation_digest(engine: dict[str, Any], manual: dict[str, Any]) -> str:
    record = {
        **{key: value for key, value in engine.items() if key not in {"source", "postProcessing"}},
        "manual": manual,
    }
    payload = json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return "sha256:" + hashlib.sha256(payload).hexdigest()


def load_shared_modules() -> dict[str, Any]:
    value = json.loads(SHARED_MODULES_PATH.read_text(encoding="utf-8"))
    modules = value.get("modules")
    if not isinstance(modules, dict):
        raise ValueError("shared_modules.json must contain a modules object")
    for module_id, module in modules.items():
        if not ID_PATTERN.fullmatch(module_id):
            raise ValueError(f"invalid shared module ID: {module_id}")
        if not module.get("sources"):
            raise ValueError(f"shared module {module_id} must declare at least one source")
        for relative in [*module.get("headers", []), *module["sources"]]:
            path = (REPO_ROOT / relative).resolve()
            path.relative_to(REPO_ROOT)
            if not path.is_file():
                raise ValueError(f"shared module {module_id} file does not exist: {relative}")
    return modules


def validate_catalog(catalog: dict[str, Any]) -> None:
    engines = catalog.get("engines")
    if not isinstance(engines, list) or not engines:
        raise ValueError("catalog engines must be a non-empty array")
    known_modules = set(load_shared_modules())
    ids: set[str] = set()
    packages: set[str] = set()
    engines_by_id: dict[str, dict[str, Any]] = {}
    for engine in engines:
        engine_id = engine.get("id")
        package_id = engine.get("packageId")
        if not isinstance(engine_id, str) or not ID_PATTERN.fullmatch(engine_id):
            raise ValueError(f"invalid engine ID: {engine_id}")
        if engine_id in ids:
            raise ValueError(f"duplicate engine ID: {engine_id}")
        if not isinstance(package_id, str) or not PACKAGE_PATTERN.fullmatch(package_id):
            raise ValueError(f"invalid package ID: {package_id}")
        if package_id in packages:
            raise ValueError(f"duplicate package ID: {package_id}")
        ids.add(engine_id)
        packages.add(package_id)
        engines_by_id[engine_id] = engine
        if len(engine.get("controls", [])) != 4 or len(engine.get("outputs", [])) != 2:
            raise ValueError(f"{engine_id} must declare four controls and two outputs")
        source = engine.get("source", {})
        for key in ("header", "className", "member", "files"):
            if key not in source:
                raise ValueError(f"{engine_id} source is missing {key}")
        for relative in [source["header"], *source["files"]]:
            path = (REPO_ROOT / relative).resolve()
            path.relative_to(REPO_ROOT)
            if not path.is_file():
                raise ValueError(f"{engine_id} source does not exist: {relative}")
        post = engine.get("postProcessing", {})
        if set(post) != {"alreadyEnveloped", "outGain", "auxGain"}:
            raise ValueError(f"{engine_id} has invalid post-processing metadata")
        modules = engine.get("sharedModules")
        if modules is not None:
            if (not isinstance(modules, list)
                    or len(modules) != len(set(modules))
                    or any(module_id not in known_modules for module_id in modules)):
                raise ValueError(f"{engine_id} references an unknown or duplicate shared module")

    for engine_id, expected in AUDITED_STOCK_CONTROLS.items():
        actual = engines_by_id.get(engine_id, {}).get("controls")
        if actual != expected:
            raise ValueError(
                f"{engine_id} controls no longer match the audited "
                "HARMONICS/TIMBRE/MORPH/Macro order")
    for engine_id, expected in AUDITED_STOCK_OUTPUTS.items():
        actual = engines_by_id.get(engine_id, {}).get("outputs")
        if actual != expected:
            raise ValueError(f"{engine_id} outputs no longer match the audited behavior")

    fm_capabilities = catalog.get("fmCapabilities")
    if not isinstance(fm_capabilities, dict) or set(fm_capabilities) != {
            "linearTzfm", "fastFm"}:
        raise ValueError("fmCapabilities must declare linearTzfm and fastFm")
    for capability, engine_ids in fm_capabilities.items():
        if (not isinstance(engine_ids, list)
                or len(engine_ids) != len(set(engine_ids))
                or any(engine_id not in ids for engine_id in engine_ids)):
            raise ValueError(
                f"fmCapabilities.{capability} must list unique approved engine IDs")

    for name, slots in catalog.get("presets", {}).items():
        if len(slots) not in (24, 32) or any(engine_id not in ids for engine_id in slots):
            raise ValueError(f"preset {name} must contain 24 or 32 approved engine IDs")

    manuals = catalog.get("manuals")
    if not isinstance(manuals, dict) or set(manuals) != ids:
        raise ValueError("manuals must contain exactly one entry for every engine")
    for engine_id, manual in manuals.items():
        if not isinstance(manual, dict) or set(manual) != {"controls", "trigger"}:
            raise ValueError(f"{engine_id} manual must contain controls and trigger")
        controls = manual["controls"]
        if not isinstance(controls, dict) or tuple(controls) != CONTROL_IDS:
            raise ValueError(f"{engine_id} manual controls must be in panel order")
        for control_id, description in controls.items():
            if not isinstance(description, str) or not 12 <= len(description) <= 180:
                raise ValueError(f"{engine_id} manual {control_id} description must contain 12-180 characters")
        trigger = manual["trigger"]
        if not isinstance(trigger, str) or not 12 <= len(trigger) <= 180:
            raise ValueError(f"{engine_id} manual trigger description must contain 12-180 characters")
        expected_trigger = AUDITED_STOCK_TRIGGERS.get(engine_id)
        if expected_trigger is not None and trigger != expected_trigger:
            raise ValueError(f"{engine_id} trigger no longer matches the audited behavior")


def web_catalog(catalog: dict[str, Any]) -> dict[str, Any]:
    return {
        "schemaVersion": catalog["schemaVersion"],
        "sdk": catalog["sdk"],
        "packageVersion": catalog["packageVersion"],
        "engines": [
            {
                **{key: value for key, value in engine.items() if key not in {"source", "postProcessing"}},
                "version": catalog["packageVersion"],
                "digest": package_digest(engine),
                "documentationDigest": documentation_digest(engine, catalog["manuals"][engine["id"]]),
                "manual": catalog["manuals"][engine["id"]],
                "implementation": {
                    "className": engine["source"]["className"],
                    **({"userDataBank": engine["source"]["userDataBank"]} if "userDataBank" in engine["source"] else {}),
                },
            }
            for engine in catalog["engines"]
        ],
        "fmCapabilities": catalog["fmCapabilities"],
        "presets": catalog["presets"],
    }


DIGESTS_PATH = Path(__file__).with_name("digests.json")


def current_digests(catalog: dict[str, Any]) -> dict[str, str]:
    return {
        engine["id"]: package_digest(engine)
        for engine in catalog["engines"]
    }


def digest_drift(catalog: dict[str, Any]) -> tuple[list[str], list[str], list[str]]:
    """Engines whose digest moved, plus those added and removed.

    package_digest hashes the catalog record AND the raw bytes of
    source.header and every source.file -- so editing a COMMENT in an engine's
    header moves it, and the engine stops matching what the builder shipped.
    Nothing detected that until 2026-07: validate_catalog recomputes digests on
    export and never compares them to anything, so eight shipped engines were
    invalidated by comment corrections without a single check failing.

    digests.json is the committed snapshot that makes it visible. A moved
    digest is not automatically wrong -- it is exactly what landing a real fix
    looks like -- but it MUST be deliberate, because it means the builder needs
    a rollout before the website can serve that engine.
    """
    if not DIGESTS_PATH.exists():
        return [], [], []
    recorded = json.loads(DIGESTS_PATH.read_text(encoding="utf-8"))
    current = current_digests(catalog)
    moved = sorted(k for k in recorded.keys() & current.keys()
                   if recorded[k] != current[k])
    added = sorted(current.keys() - recorded.keys())
    removed = sorted(recorded.keys() - current.keys())
    return moved, added, removed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-digests", action="store_true",
                        help="fail if any engine's digest drifted from digests.json")
    parser.add_argument("--snapshot-digests", action="store_true",
                        help="rewrite digests.json from the current sources")
    arguments = parser.parse_args()

    catalog = load_catalog()
    validate_catalog(catalog)
    print(f"catalog ok: {len(catalog['engines'])} immutable packages")

    if arguments.snapshot_digests:
        DIGESTS_PATH.write_text(
            json.dumps(current_digests(catalog), indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        print(f"digests snapshotted: {DIGESTS_PATH.name}")
        return

    if arguments.check_digests:
        moved, added, removed = digest_drift(catalog)
        if not DIGESTS_PATH.exists():
            raise SystemExit(f"{DIGESTS_PATH.name} is missing; run --snapshot-digests")
        for engine_id in moved:
            print(f"  DRIFT  {engine_id}: digest moved -- needs a builder rollout")
        for engine_id in added:
            print(f"  new    {engine_id}")
        for engine_id in removed:
            print(f"  gone   {engine_id}")
        if moved:
            raise SystemExit(
                f"{len(moved)} shipped engine(s) no longer match digests.json. "
                "If the change was deliberate, re-run with --snapshot-digests "
                "and roll the builder.")
        print("digests match the snapshot")


if __name__ == "__main__":
    main()
