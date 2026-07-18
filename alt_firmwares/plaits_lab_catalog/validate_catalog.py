#!/usr/bin/env python3
"""Validate the authoritative Plaits Lab package catalog."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any


CATALOG_DIR = Path(__file__).resolve().parent
REPO_ROOT = CATALOG_DIR.parents[1]
CATALOG_PATH = CATALOG_DIR / "catalog.json"
ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*$")
PACKAGE_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9-]*$")
CONTROL_IDS = ("harmonics", "timbre", "morph", "macro")


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


def validate_catalog(catalog: dict[str, Any]) -> None:
    engines = catalog.get("engines")
    if not isinstance(engines, list) or not engines:
        raise ValueError("catalog engines must be a non-empty array")
    ids: set[str] = set()
    packages: set[str] = set()
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

    for name, slots in catalog.get("presets", {}).items():
        if len(slots) != 24 or any(engine_id not in ids for engine_id in slots):
            raise ValueError(f"preset {name} must contain 24 approved engine IDs")

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
        "presets": catalog["presets"],
    }


def main() -> None:
    catalog = load_catalog()
    validate_catalog(catalog)
    print(f"catalog ok: {len(catalog['engines'])} immutable packages")


if __name__ == "__main__":
    main()
