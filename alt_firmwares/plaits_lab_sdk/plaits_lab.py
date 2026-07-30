#!/usr/bin/env python3
"""Local validation and preview renderer for Plaits Lab engine packages."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import secrets
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
import time
import wave
import zipfile
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import urllib.error
import urllib.request
from urllib.parse import urlparse
from pathlib import Path
from typing import Any


SDK_VERSION = "plaits-engine-cpp-v1"
SDK_DIR = Path(__file__).resolve().parent
REPO_ROOT = SDK_DIR.parents[1]
CATALOG_PATH = SDK_DIR.parent / "plaits_lab_catalog/catalog.json"
PUBLIC_CATALOG_PATH = SDK_DIR.parent / "plaits_lab_catalog/public_catalog.json"
SHARED_MODULES_PATH = SDK_DIR.parent / "plaits_lab_catalog/shared_modules.json"
PACKAGES_DIR = SDK_DIR / "packages"
# Every engine is statically compiled into ONE firmware image beside Mutable's
# MIT-licensed Plaits code and shipped as a single audio-installable WAV. So a
# package's license has to be NOTICE-ONLY: dischargeable by carrying a copyright
# line in the firmware's attribution list, with no copyleft reaching the rest of
# the image and no source-disclosure duty riding on the distributed binary.
# MIT / BSD-2-Clause / BSD-3-Clause / ISC are exactly that set.
#
# Apache-2.0 is DELIBERATELY excluded despite being permissive: §4(d) makes the
# NOTICE file travel with every derivative, and the §3 patent grant carries a
# termination condition — per-package obligations a flashed firmware blob has no
# way to honor. GPL / LGPL / MPL are excluded outright (copyleft, or a per-file
# source-disclosure duty). Revisit only alongside a real story for shipping
# per-package notices with the firmware.
ALLOWED_LICENSES = {"MIT", "BSD-2-Clause", "BSD-3-Clause", "ISC"}
DEFAULT_LICENSE = "MIT"
CONTROL_IDS = ["harmonics", "timbre", "morph", "macro"]
ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9-]*$")
CATALOG_ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*$")
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
CLASS_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
ALLOWED_COMMUNITY_SYSTEM_HEADERS = {
    "algorithm", "cmath", "cstddef", "limits", "stdint.h",
}
# The firmware builds as C++98 (arm-none-eabi 4.8), so <cstdint> — the C++11
# header — doesn't compile there even though the host check (C++11) accepts it.
# Point contributors at the C++98-safe C header rather than a bare "non-SDK
# header" rejection.
CXX11_HEADER_REPLACEMENTS = {
    "cstdint": "stdint.h",
}
FORBIDDEN_SOURCE_PATTERNS = {
    "inline assembly": re.compile(r"\b(?:asm|__asm__)\b"),
    "dynamic allocation": re.compile(
        r"\b(?:malloc|calloc|realloc|free)\s*\(|\bnew\s+[A-Za-z_]|\bdelete(?:\[\])?\s+[A-Za-z_]"
    ),
    "direct hardware access": re.compile(r"\b(?:HAL_|NVIC_|FLASH_|RCC_|GPIO|SysTick)"),
}
# What to do about each forbidden category, appended to the failure so a
# contributor sees the fix, not just the rule.
FORBIDDEN_SOURCE_HINTS = {
    "inline assembly": "write portable C++ instead",
    "dynamic allocation": "preallocate in Init() with the BufferAllocator — no malloc/new/free/delete at audio rate",
    "direct hardware access": "engines read only EngineParameters; they never touch peripherals or registers",
}
# libm transcendentals: the host compiler links them, but the bare-metal firmware
# can't — they pull in __errno and bloat flash — so an engine using them passes
# `check`'s host compile and then fails at the hardware LINK. Catch them here and
# point at the shared LUT replacements. (\b-anchored, so std::log doesn't shadow
# std::log2/std::log10 and std::exp doesn't shadow std::exp2.)
NON_PORTABLE_STD = (
    "std::sin", "std::cos", "std::tan",
    "std::exp2", "std::exp", "std::log2", "std::log10", "std::log", "std::pow",
)
NON_PORTABLE_STD_HINT = (
    "the firmware can't link libm — use the shared LUTs: plaits::Sine(phase) for "
    "sin/cos (phase in [0,1); plaits/dsp/oscillator/sine_oscillator.h), and "
    "stmlib::SemitonesToRatio(x*12) for 2^x and pow (stmlib/dsp/units.h). log2 has "
    "no shared helper — roll a bit-trick approximation (see the helix example)"
)


class PackageError(Exception):
    pass


def strip_cpp_comments(source: str) -> str:
    """Remove C++ comments while preserving strings and line positions."""
    result: list[str] = []
    index = 0
    state = "code"
    quote = ""
    while index < len(source):
        character = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if character == "/" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "line-comment"
                continue
            if character == "/" and following == "*":
                result.extend((" ", " "))
                index += 2
                state = "block-comment"
                continue
            result.append(character)
            if character in {'"', "'"}:
                state = "literal"
                quote = character
        elif state == "line-comment":
            result.append("\n" if character == "\n" else " ")
            if character == "\n":
                state = "code"
        elif state == "block-comment":
            if character == "*" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "code"
                continue
            result.append("\n" if character == "\n" else " ")
        else:
            result.append(character)
            if character == "\\" and following:
                result.append(following)
                index += 2
                continue
            if character == quote:
                state = "code"
        index += 1
    return "".join(result)


def load_builtin_catalog() -> tuple[dict[str, Any], dict[str, Any]]:
    catalog = read_json(CATALOG_PATH)
    public = read_json(PUBLIC_CATALOG_PATH)
    return (
        {item["id"]: item for item in catalog["engines"]},
        {item["id"]: item for item in public["engines"]},
    )


def builtin_engine(identifier: str) -> tuple[dict[str, Any], dict[str, Any]]:
    catalog, public = load_builtin_catalog()
    by_package = {item["packageId"]: engine_id for engine_id, item in catalog.items()}
    engine_id = identifier if identifier in catalog else by_package.get(identifier)
    if not engine_id:
        raise PackageError(f"unknown built-in model {identifier!r}; "
                           f"run `{cli_invocation()} catalog`")
    return catalog[engine_id], public[engine_id]


def builtin_package(identifier: str) -> dict[str, Any]:
    engine, _ = builtin_engine(identifier)
    source = engine["source"]
    return {
        "directory": REPO_ROOT,
        "manifest": {
            "packageType": "builtin-reference",
            "source": {"className": source["className"]},
            "postProcessing": engine["postProcessing"],
            "sharedModules": list(engine.get("sharedModules", [])),
        },
        "repo_root": REPO_ROOT,
        "source_root": REPO_ROOT,
        "header": REPO_ROOT / source["header"],
        "source_files": [REPO_ROOT / path for path in source["files"]],
        "scenarios": default_scenarios(),
    }


def load_shared_modules() -> dict[str, Any]:
    """Return the shared-module registry (module id -> {headers, sources, ...})."""
    data = read_json(SHARED_MODULES_PATH)
    require(isinstance(data, dict) and isinstance(data.get("modules"), dict),
            "shared_modules.json must contain a modules object")
    return data["modules"]


def shared_module_header_owners() -> dict[str, str]:
    """Map each shared-module header (e.g. plaits/dsp/chords/chord_bank.h) to its module id."""
    return {
        header: module_id
        for module_id, module in load_shared_modules().items()
        for header in module.get("headers", [])
    }


def validate_shared_modules(module_ids: Any) -> list[str]:
    """Validate a declared sharedModules list against the registry; return it."""
    registry = load_shared_modules()
    require(isinstance(module_ids, list), "sharedModules must be an array")
    require(all(isinstance(item, str) for item in module_ids),
            "sharedModules entries must be strings")
    require(len(module_ids) == len(set(module_ids)), "sharedModules must be unique")
    for module_id in module_ids:
        require(module_id in registry,
                f"unknown shared module {module_id!r}; "
                f"run `{cli_invocation()} modules`")
    return module_ids


def shared_module_sources(module_ids: list[str], repo_root: Path) -> list[Path]:
    """Resolve declared shared modules to the repo .cc files that must be linked."""
    registry = load_shared_modules()
    sources: list[Path] = []
    for module_id in module_ids:
        for relative in registry[module_id].get("sources", []):
            sources.append(repo_root / relative)
    return sources


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise PackageError(f"missing {path.name}: {path}") from error
    except json.JSONDecodeError as error:
        raise PackageError(f"invalid JSON in {path}: {error}") from error


def require(condition: bool, message: str) -> None:
    if not condition:
        raise PackageError(message)


def find_repo_root(package_dir: Path) -> Path:
    for candidate in [package_dir, *package_dir.parents]:
        if (candidate / "plaits").is_dir() and (candidate / "stmlib").is_dir():
            return candidate
    if (REPO_ROOT / "plaits").is_dir() and (REPO_ROOT / "stmlib").is_dir():
        return REPO_ROOT
    raise PackageError("could not locate the Mutable Instruments eurorack SDK checkout")


def resolve_within(base: Path, relative: str, label: str) -> Path:
    require(bool(relative) and not Path(relative).is_absolute(), f"{label} must be a relative path")
    resolved = (base / relative).resolve()
    try:
        resolved.relative_to(base.resolve())
    except ValueError as error:
        raise PackageError(f"{label} escapes its allowed directory: {relative}") from error
    return resolved


def validate_control(value: Any, expected_id: str, index: int) -> None:
    require(isinstance(value, dict), f"controls[{index}] must be an object")
    require(set(value) == {"id", "label", "description"}, f"controls[{index}] has unsupported fields")
    require(value.get("id") == expected_id, f"controls[{index}].id must be {expected_id}")
    require(isinstance(value.get("label"), str) and 1 <= len(value["label"]) <= 32,
            f"controls[{index}].label must contain 1-32 characters")
    require(isinstance(value.get("description"), str) and 1 <= len(value["description"]) <= 120,
            f"controls[{index}].description must contain 1-120 characters")


def validate_scenario(value: Any, index: int) -> None:
    require(isinstance(value, dict), f"scenarios[{index}] must be an object")
    required = {"id", "name", "durationSeconds", "note", "triggerHz", "controls"}
    require(set(value) == required, f"scenarios[{index}] must contain exactly {sorted(required)}")
    require(isinstance(value["id"], str) and CATALOG_ID_PATTERN.fullmatch(value["id"]) is not None,
            f"scenarios[{index}].id is invalid")
    require(isinstance(value["name"], str) and bool(value["name"]), f"scenarios[{index}].name is required")
    require(isinstance(value["durationSeconds"], int) and 1 <= value["durationSeconds"] <= 30,
            f"scenarios[{index}].durationSeconds must be an integer from 1 to 30")
    require(isinstance(value["note"], (int, float)) and -24 <= value["note"] <= 120,
            f"scenarios[{index}].note must be between -24 and 120")
    require(isinstance(value["triggerHz"], (int, float)) and 0 <= value["triggerHz"] <= 100,
            f"scenarios[{index}].triggerHz must be between 0 and 100")
    controls = value["controls"]
    require(isinstance(controls, dict) and set(controls) == set(CONTROL_IDS),
            f"scenarios[{index}].controls must declare {CONTROL_IDS}")
    for control_id in CONTROL_IDS:
        sweep = controls[control_id]
        require(isinstance(sweep, list) and len(sweep) == 2,
                f"scenarios[{index}].controls.{control_id} must be [start, end]")
        require(all(isinstance(item, (int, float)) and 0 <= item <= 1 for item in sweep),
                f"scenarios[{index}].controls.{control_id} values must be between 0 and 1")


def validate_community_source(
    paths: list[Path], declared_modules: frozenset[str] = frozenset(),
) -> None:
    module_owners = shared_module_header_owners()
    for path in paths:
        source = path.read_text(encoding="utf-8")
        policy_source = strip_cpp_comments(source)
        for description, pattern in FORBIDDEN_SOURCE_PATTERNS.items():
            match = pattern.search(policy_source)
            if match is not None:
                line = policy_source.count("\n", 0, match.start()) + 1
                raise PackageError(
                    f"{path.name}:{line} uses forbidden {description} "
                    f"({match.group(0).strip()!r}) — {FORBIDDEN_SOURCE_HINTS[description]}"
                )
        for symbol in NON_PORTABLE_STD:
            match = re.search(re.escape(symbol) + r"\b", policy_source)
            if match is not None:
                line = policy_source.count("\n", 0, match.start()) + 1
                raise PackageError(
                    f"{path.name}:{line} uses {symbol}, which the host check compiles but "
                    f"the hardware build can't link — {NON_PORTABLE_STD_HINT}."
                )
        for match in re.finditer(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', source, re.MULTILINE):
            delimiter, include = match.groups()
            require(".." not in Path(include).parts, f"{path.name} include escapes the package: {include}")
            if delimiter == "<":
                if include in CXX11_HEADER_REPLACEMENTS:
                    raise PackageError(
                        f"{path.name} includes <{include}>, which needs C++11 — the firmware "
                        f"is C++98, so the host check compiles it but the hardware build won't. "
                        f"Use <{CXX11_HEADER_REPLACEMENTS[include]}>."
                    )
                require(include in ALLOWED_COMMUNITY_SYSTEM_HEADERS,
                        f"{path.name} uses non-SDK system header <{include}>")
            else:
                allowed = include.startswith(("plaits/dsp/", "stmlib/")) \
                    or include == "plaits/resources.h" or "/" not in include
                require(allowed, f"{path.name} uses non-SDK include \"{include}\"")
                # A header backed by a shared module carries out-of-line symbols
                # that only link when its module is declared; catch it here with
                # an actionable message instead of a raw linker error.
                owner = module_owners.get(include)
                require(owner is None or owner in declared_modules,
                        f'{path.name} includes "{include}" — add "{owner}" to '
                        f'sharedModules in plaits-engine.json to link it')


def autodeclare_shared_modules(paths: list[Path], declared: list[str]) -> list[str]:
    """Add any shared module whose header is #included but not yet in `declared`.
    Mutates `declared` in place; returns the module ids that were added. This lets
    a contributor simply #include a module header — check/dev/render write the
    matching sharedModules entry for them instead of erroring."""
    owners = shared_module_header_owners()
    seen = set(declared)
    added: list[str] = []
    for path in paths:
        for match in re.finditer(r'^\s*#\s*include\s*"([^"]+)"',
                                 path.read_text(encoding="utf-8"), re.MULTILINE):
            module_id = owners.get(match.group(1))
            if module_id and module_id not in seen:
                seen.add(module_id)
                declared.append(module_id)
                added.append(module_id)
    return added


HEX_COLOR_PATTERN = re.compile(r"#[0-9a-fA-F]{6}")


def load_package(package_arg: str, autodeclare: bool = False) -> dict[str, Any]:
    package_dir = Path(package_arg).resolve()
    manifest_path = package_dir / "plaits-engine.json"
    manifest = read_json(manifest_path)
    require(isinstance(manifest, dict), "plaits-engine.json must contain an object")

    required = {
        "schemaVersion", "sdk", "packageType", "id", "catalogId", "version",
        "name", "author", "origin", "license", "description", "family", "tags",
        "controls", "outputs", "source", "postProcessing", "scenarios",
    }
    optional = {"upstream", "forkedFrom", "sharedModules", "artwork"}
    require(required <= set(manifest), f"manifest is missing {sorted(required - set(manifest))}")
    require(set(manifest) <= required | optional,
            f"manifest has unsupported fields {sorted(set(manifest) - required - optional)}")
    require(manifest["schemaVersion"] == 1, "schemaVersion must be 1")
    # The colour a contributor picks for their model. It used to live only in
    # the browser's localStorage, so it never reached the package and every
    # community engine rendered in the catalog's grey fallback — the choice was
    # decorative. Carrying it here is what makes it survive submission.
    if "artwork" in manifest:
        artwork = manifest["artwork"]
        require(isinstance(artwork, dict) and set(artwork) <= {"color"},
                "artwork may only contain a color")
        if "color" in artwork:
            require(isinstance(artwork["color"], str)
                    and HEX_COLOR_PATTERN.fullmatch(artwork["color"]) is not None,
                    "artwork color must be a #RRGGBB hex string")
    require(manifest["sdk"] == SDK_VERSION, f"sdk must be {SDK_VERSION}")
    require(manifest["packageType"] in {"builtin-reference", "community"},
            "packageType must be builtin-reference or community")
    require(isinstance(manifest["id"], str) and ID_PATTERN.fullmatch(manifest["id"]) is not None,
            "id must use the namespace/slug form")
    require(isinstance(manifest["catalogId"], str)
            and CATALOG_ID_PATTERN.fullmatch(manifest["catalogId"]) is not None,
            "catalogId must be a lowercase slug")
    require(isinstance(manifest["version"], str)
            and VERSION_PATTERN.fullmatch(manifest["version"]) is not None,
            "version must be numeric semantic versioning such as 1.0.0")
    require(manifest["license"] in ALLOWED_LICENSES,
            f"license must be one of {sorted(ALLOWED_LICENSES)}")
    require(isinstance(manifest["name"], str) and 1 <= len(manifest["name"]) <= 48,
            "name must contain 1-48 characters")
    require(isinstance(manifest["author"], str) and 1 <= len(manifest["author"]) <= 80,
            "author must contain 1-80 characters")
    require(manifest["origin"] in {"Mutable Instruments", "Rubato Lab", "Community"},
            "origin is unsupported")
    if manifest["packageType"] == "community":
        require(manifest["origin"] == "Community", "community packages must use Community origin")
    require(isinstance(manifest["description"], str) and 20 <= len(manifest["description"]) <= 240,
            "description must contain 20-240 characters")
    require(isinstance(manifest["family"], str) and 1 <= len(manifest["family"]) <= 32,
            "family must contain 1-32 characters")
    require(isinstance(manifest["tags"], list) and 1 <= len(manifest["tags"]) <= 12,
            "tags must contain 1-12 entries")
    require(all(isinstance(tag, str) and CATALOG_ID_PATTERN.fullmatch(tag.replace(" ", "-"))
                for tag in manifest["tags"]), "tags must be lowercase words")
    require(len(manifest["tags"]) == len(set(manifest["tags"])), "tags must be unique")
    if "upstream" in manifest:
        require(isinstance(manifest["upstream"], str) and len(manifest["upstream"]) <= 240,
                "upstream must contain at most 240 characters")
    if "forkedFrom" in manifest:
        require(isinstance(manifest["forkedFrom"], str)
                and CATALOG_ID_PATTERN.fullmatch(manifest["forkedFrom"]) is not None,
                "forkedFrom must be a built-in catalog ID")
        builtin_engine(manifest["forkedFrom"])
    if "sharedModules" in manifest:
        validate_shared_modules(manifest["sharedModules"])
    require((package_dir / "LICENSE").is_file(), "package must contain LICENSE")
    require((package_dir / "README.md").is_file(), "package must contain README.md")
    # The LICENSE file is the whole evidentiary record for a contribution — a
    # submission carries no other statement of terms — so its TEXT has to agree
    # with the manifest, not merely exist. Without this, declaring "ISC" over
    # MIT text (or pasting GPL in) passed `check` green.
    license_text = (package_dir / "LICENSE").read_text(encoding="utf-8")
    detected_license = identify_license_text(license_text)
    require(detected_license is not None,
            "LICENSE does not contain the text of a recognized license; it must carry "
            f"the full text of one of {sorted(ALLOWED_LICENSES)}")
    require(detected_license == manifest["license"],
            f"LICENSE contains {detected_license} text but the manifest declares "
            f"{manifest['license']} — make the two agree")
    require(bool(extract_copyright_notices(license_text)),
            "LICENSE must carry a copyright line naming the rights holder "
            "(for example: Copyright (c) 2026 Your Name)")

    controls = manifest["controls"]
    require(isinstance(controls, list) and len(controls) == 4, "controls must contain exactly four entries")
    for index, control_id in enumerate(CONTROL_IDS):
        validate_control(controls[index], control_id, index)

    outputs = manifest["outputs"]
    require(isinstance(outputs, dict) and set(outputs) == {"main", "aux"},
            "outputs must contain exactly main and aux descriptions")
    require(all(isinstance(value, str) and bool(value) for value in outputs.values()),
            "output descriptions must not be empty")

    source = manifest["source"]
    require(isinstance(source, dict) and set(source) == {"root", "header", "files", "className"},
            "source must contain exactly root, header, files, and className")
    require(isinstance(source["className"], str)
            and CLASS_PATTERN.fullmatch(source["className"]) is not None,
            "source.className is invalid")
    source_root = (package_dir / source["root"]).resolve()
    repo_root = find_repo_root(package_dir)
    if manifest["packageType"] == "community":
        try:
            source_root.relative_to(package_dir)
        except ValueError as error:
            raise PackageError("community source.root must remain within the package") from error
    else:
        try:
            source_root.relative_to(repo_root)
        except ValueError as error:
            raise PackageError("built-in source.root must remain within the repository") from error

    header = resolve_within(source_root, source["header"], "source.header")
    require(header.is_file(), f"source header does not exist: {header}")
    require(isinstance(source["files"], list) and bool(source["files"]),
            "source.files must be a non-empty array")
    source_files = []
    for item in source["files"]:
        require(isinstance(item, str) and item.endswith(".cc"), "source files must end in .cc")
        source_file = resolve_within(source_root, item, "source.files entry")
        require(source_file.is_file(), f"source file does not exist: {source_file}")
        source_files.append(source_file)
    autodeclared: list[str] = []
    if manifest["packageType"] == "community":
        # Every vendored file states its own terms. Built-in references are
        # exempt: they point at firmware sources that carry Emilie Gillet's full
        # MIT header instead of an SPDX tag, and those aren't ours to restamp.
        for path in [header, *source_files]:
            tag = source_spdx_id(path.read_text(encoding="utf-8"))
            require(tag is not None,
                    f"{path.name} has no SPDX-License-Identifier header; add "
                    f"`// SPDX-License-Identifier: {manifest['license']}` at the top of the file")
            require(tag == manifest["license"],
                    f"{path.name} declares SPDX-License-Identifier: {tag} but the manifest "
                    f"declares {manifest['license']} — make the two agree")
        declared = list(manifest.get("sharedModules", []))
        if autodeclare:
            autodeclared = autodeclare_shared_modules([header, *source_files], declared)
            if autodeclared:
                manifest["sharedModules"] = declared
                manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        validate_community_source([header, *source_files], frozenset(declared))

    post = manifest["postProcessing"]
    require(isinstance(post, dict) and set(post) == {"alreadyEnveloped", "outGain", "auxGain"},
            "postProcessing must contain alreadyEnveloped, outGain, and auxGain")
    require(isinstance(post["alreadyEnveloped"], bool), "postProcessing.alreadyEnveloped must be boolean")
    for gain in ("outGain", "auxGain"):
        require(isinstance(post[gain], (int, float)) and -4 <= post[gain] <= 4,
                f"postProcessing.{gain} must be between -4 and 4")

    scenarios_path = resolve_within(package_dir, manifest["scenarios"], "scenarios")
    scenarios = read_json(scenarios_path)
    require(isinstance(scenarios, list) and bool(scenarios), "scenarios file must contain a non-empty array")
    for index, scenario in enumerate(scenarios):
        validate_scenario(scenario, index)
    scenario_ids = [item["id"] for item in scenarios]
    require(len(scenario_ids) == len(set(scenario_ids)), "scenario IDs must be unique")

    return {
        "directory": package_dir,
        "manifest": manifest,
        "repo_root": repo_root,
        "source_root": source_root,
        "header": header,
        "source_files": source_files,
        "scenarios": scenarios,
        "autodeclared": autodeclared,
    }


def compiler_path(requested: str | None) -> str:
    if requested:
        compiler = shutil.which(requested)
    else:
        compiler = shutil.which("c++") or shutil.which("g++")
    if not compiler:
        raise PackageError("no host C++ compiler found; pass --compiler")
    return compiler


# Windows compilers append .exe when -o names no extension, so a path the SDK
# builds by hand never matches the file actually written. Every is_file() check
# on a compiled binary must use the name the compiler produces — above all the
# dev loop's recompile guard, which otherwise rebuilds on every single request.
HOST_EXE_SUFFIX = ".exe" if os.name == "nt" else ""


def host_binary(directory: Path, name: str) -> Path:
    """Path of a compiled host executable, carrying the platform's suffix."""
    return directory / f"{name}{HOST_EXE_SUFFIX}"


# The Plaits DSP headers use M_PI, which is POSIX rather than ISO C++: under
# strict -std=c++11 the MinGW/UCRT <cmath> hides it, while glibc and macOS
# expose it regardless. Defining this is a no-op off Windows, so it goes on
# every compile rather than behind a platform branch.
MATH_CONSTANTS_DEFINE = "-D_USE_MATH_DEFINES"

SANITIZER_FLAGS = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]

# LeakSanitizer runs as part of ASan on Linux but does not exist on macOS (Apple's
# ASan ships no LSan). Left on, the SAME package passes `check --full` on macOS and
# fails on Linux — including inside the builder image a Windows contributor
# delegates to — which would make the submission gate depend on the reviewer's OS.
#
# It also cannot report anything about contributor code: FORBIDDEN_SOURCE_PATTERNS
# rejects malloc/calloc/realloc/free/new/delete before a package ever compiles, so
# an engine has no way to leak. The only reachable allocations are in the SDK's own
# harness — stmlib's test WavWriter callocs a scratch buffer per Write() and never
# frees it (~48 B per render block, upstream in the pinned submodule).
#
# So: same verdict everywhere, and nothing of value lost. ASan and UBSan — which
# are what this gate is actually for — stay fully on.
SANITIZER_RUNTIME_ENV = {"ASAN_OPTIONS": "detect_leaks=0"}

_SANITIZER_SUPPORT: dict[str, bool] = {}


def host_sanitizers_available(requested_compiler: str | None) -> bool:
    """Whether the host compiler can COMPILE AND LINK with ASan/UBSan.

    MinGW-w64 — the compiler a Windows contributor most likely has, and the one
    `c++`/`g++` autodetection finds there — ships no sanitizer runtime: it
    accepts the flags and then fails at link with `cannot find -lasan`. Only
    building a trivial program tells us for sure, so probe once per compiler and
    cache. When this is False the sanitized commands run in the builder image
    instead, which is Linux and always has the runtimes.
    """
    compiler = compiler_path(requested_compiler)
    if compiler not in _SANITIZER_SUPPORT:
        with tempfile.TemporaryDirectory(prefix="plaits-lab-sanprobe-") as temp_dir:
            probe = Path(temp_dir) / "probe.cc"
            probe.write_text("int main() { return 0; }\n", encoding="utf-8")
            result = subprocess.run(
                [compiler, *SANITIZER_FLAGS, str(probe),
                 "-o", str(host_binary(Path(temp_dir), "probe"))],
                text=True, capture_output=True, check=False,
            )
        _SANITIZER_SUPPORT[compiler] = result.returncode == 0
    return _SANITIZER_SUPPORT[compiler]


def sdk_docker_command(
    docker: str, image: str, package: dict[str, Any], sdk_args: list[str],
    extra_mounts: list[str] | None = None,
) -> list[str]:
    """A `docker run` of this same CLI inside the builder image, with the repo at
    /workspace and the package at /contributor (both read-only). Every delegating
    command shares this shape, so it lives in one place — the mounts and the
    `--platform linux/amd64` pin are easy to get subtly wrong per copy."""
    return [
        docker, "run", "--rm", "--platform", "linux/amd64", "--entrypoint", "python3",
        "-v", f"{package['repo_root']}:/workspace:ro",
        "-v", f"{package['directory']}:/contributor:ro",
        *(extra_mounts or []),
        "-w", "/workspace", image,
        "alt_firmwares/plaits_lab_sdk/plaits_lab.py", *sdk_args,
    ]


DOCKER_IMAGE_HINT = (
    "Build the builder image once first:\n"
    "  git submodule update --init stmlib stm_audio_bootloader\n"
    "  docker build --platform linux/amd64 -t {image} -f Dockerfile.plaits-builder ."
)

NO_SANITIZER_HELP = (
    "{command} runs the sanitizers, and this host's C++ compiler ({compiler}) has no\n"
    "sanitizer runtime — MinGW-w64 does not ship one. Install Docker Desktop\n"
    "(https://docs.docker.com/get-docker/) and the SDK will run this step in the\n"
    "builder image automatically.\n" + DOCKER_IMAGE_HINT
)


def run_sanitized_in_docker(
    package: dict[str, Any], args: argparse.Namespace, label: str,
    sdk_args: list[str], extra_mounts: list[str] | None = None,
) -> int:
    """Re-run a sanitizer-dependent command inside the builder image. Output is
    streamed rather than captured: these compile and render for tens of seconds,
    and the container prints exactly the progress the contributor wants to see."""
    docker = shutil.which("docker")
    if not docker:
        # Built only on the failure path: naming the compiler means resolving it.
        raise PackageError(NO_SANITIZER_HELP.format(
            command=label, compiler=compiler_path(args.compiler),
            image=args.docker_image,
        ))
    # flush: the container writes straight to the terminal, so an unflushed note
    # would surface after the output it is meant to introduce.
    print(f"note: host compiler cannot link the sanitizers — running {label} "
          f"in {args.docker_image}", flush=True)
    command = sdk_docker_command(docker, args.docker_image, package, sdk_args, extra_mounts)
    result = subprocess.run(command, check=False)
    if result.returncode:
        raise PackageError(
            f"{label} failed in the builder image. If the {args.docker_image} image is "
            f"missing, " + DOCKER_IMAGE_HINT.format(image=args.docker_image)
        )
    return 0


def engine_header_define(package: dict[str, Any]) -> str:
    manifest = package["manifest"]
    return (
        package["header"].name
        if manifest["packageType"] == "community"
        else package["header"].relative_to(package["repo_root"]).as_posix()
    )


def engine_translation_units(package: dict[str, Any], entry: Path) -> list[str]:
    """De-duplicated .cc list for a package build: the given entry harness, the
    package's own source, fork support files, declared shared modules, and the
    always-linked base set — each resolved path handed to the compiler once."""
    manifest = package["manifest"]
    support_files: list[Path] = []
    if "forkedFrom" in manifest:
        upstream, _ = builtin_engine(manifest["forkedFrom"])
        primary_stem = Path(upstream["source"]["header"]).stem
        support_files = [
            package["repo_root"] / item
            for item in upstream["source"]["files"]
            if Path(item).stem != primary_stem
        ]
    shared_sources = shared_module_sources(
        manifest.get("sharedModules", []), package["repo_root"]
    )
    units = [
        entry,
        *package["source_files"],
        *support_files,
        *shared_sources,
        package["repo_root"] / "plaits/resources.cc",
        package["repo_root"] / "stmlib/dsp/units.cc",
        package["repo_root"] / "stmlib/utils/random.cc",
    ]
    return dedupe_units(units)


def dedupe_units(units: list[Path]) -> list[str]:
    """Resolved-path de-duplication: each translation unit handed to the
    compiler exactly once, in first-seen order."""
    seen: set[str] = set()
    compiled: list[str] = []
    for unit in units:
        key = str(unit.resolve())
        if key not in seen:
            seen.add(key)
            compiled.append(str(unit))
    return compiled


# --- CPU reference-ratio check -------------------------------------------
#
# The module gives an engine roughly 1500 CPU cycles per sample (72 MHz / 48
# kHz) for EVERYTHING — synthesis, the LPG, output post-processing, the UI and
# the ADCs. Nothing on the host measures that directly: a development machine
# is far faster than a 72 MHz Cortex-M4, so an engine several times over the
# hardware budget still renders in a small fraction of real time here. (That is
# exactly how a community engine costing ~8x a stock engine passed every check
# and then starved the module — glitched audio, and a UI loop so short of
# cycles the LEDs stopped refreshing.)
#
# What DOES carry over is the ratio between two engines built by the same
# harness with the same flags: the machine cancels out, and stock engines are
# known to fit. So the check times the package against a stock engine and reads
# the ratio.
# The reference is the HEAVIEST stock engine, since that is the known-good
# ceiling: Mutable shipped it, so the module demonstrably has room for it. Every
# catalog engine was timed by this harness to pick it (ns/sample, this host):
#
#   two-op-fm 95.6 | particle-noise 89.8 | inharmonic-string 49.3 | lockstep 47.2
#   analog-hi-hat 46.7 | analog-bass-drum 45.2 | granular-formant 37.0
#   chords 33.1 | modal-resonator 32.6 | string-machine 28.4 | swarm 25.4
#   virtual-analog 25.2 | waveshaping 16.8 | pulsar 11.5
#   (speech/chiptune/dx7 measure 4-8 because they idle without user data)
#
# Re-run that survey if the engine set changes. Note swarm — the first reference
# used here — is near the MEDIAN, not the ceiling; measuring against it made
# engines look ~3.8x more expensive than they are.
CPU_REFERENCE_ENGINE = "two-op-fm"
CPU_BENCH_BLOCKS = 200000               # ~2.4M samples; well past timer noise
CPU_BENCH_REPEATS = 3
# Thresholds bracket the two known points: the reference itself ships and works
# (1.0x), and a community engine measured at 2.3x the reference demonstrably
# overran the module. So at or under the reference is proven fine; above it is
# heavier than anything Mutable shipped and unproven; at 2x we are at the
# confirmed-failure point. Heuristics — retune as hardware data accumulates.
CPU_RATIO_WARN = 1.0
CPU_RATIO_FAIL = 2.0


def compile_cpu_bench(
    units: list[str], header: str, class_name: str, includes: list[Path],
    output: Path, requested_compiler: str | None,
) -> None:
    """Build one engine against cpu_bench.cc. Optimized and WITHOUT sanitizers —
    this build is timed, and the sanitized renderer used elsewhere in `check`
    runs orders of magnitude slower than the real code."""
    command = [
        compiler_path(requested_compiler),
        "-std=c++11", MATH_CONSTANTS_DEFINE, "-DTEST", "-O2",
        "-Wno-unused-variable", "-Wno-unused-parameter",
        "-Wno-unused-local-typedefs", "-Wno-deprecated-declarations",
        f'-DPLAITS_LAB_ENGINE_HEADER="{header}"',
        f"-DPLAITS_LAB_ENGINE_CLASS={class_name}",
    ]
    for include in includes:
        command += ["-I", str(include)]
    command += [*units, "-o", str(output)]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        details = (result.stderr or result.stdout).strip()
        raise PackageError(f"CPU benchmark compilation failed\n{details[-2000:]}")


def measure_cpu_cost(binary: Path) -> float:
    """Nanoseconds per output sample, best of CPU_BENCH_REPEATS runs. Best rather
    than mean: the fastest run is the one least disturbed by other load."""
    best: float | None = None
    for _ in range(CPU_BENCH_REPEATS):
        result = subprocess.run(
            [str(binary), str(CPU_BENCH_BLOCKS)], text=True, capture_output=True, check=False,
        )
        if result.returncode:
            raise PackageError(
                f"CPU benchmark failed: {(result.stderr or result.stdout).strip()}"
            )
        try:
            value = float(result.stdout.split()[0])
        except (IndexError, ValueError):
            raise PackageError(f"CPU benchmark produced no timing: {result.stdout.strip()!r}")
        if best is None or value < best:
            best = value
    return best if best is not None else 0.0


def cpu_reference_ratio(
    package: dict[str, Any], requested_compiler: str | None, temp_dir: Path,
) -> dict[str, Any]:
    """Time this engine and a stock engine under the same harness; return both
    costs and their ratio."""
    entry = Path(__file__).with_name("cpu_bench.cc")
    repo_root = package["repo_root"]
    manifest = package["manifest"]

    package_binary = host_binary(temp_dir, "cpu-bench-package")
    compile_cpu_bench(
        engine_translation_units(package, entry),
        engine_header_define(package),
        f'plaits::{manifest["source"]["className"]}',
        [repo_root, package["source_root"]],
        package_binary,
        requested_compiler,
    )

    reference, _ = builtin_engine(CPU_REFERENCE_ENGINE)
    # Honour the reference's declared shared modules — an engine that uses one
    # (e.g. chords -> chord-bank) does not link without it.
    reference_units = dedupe_units([
        entry,
        *(repo_root / item for item in reference["source"]["files"]),
        *shared_module_sources(reference.get("sharedModules", []), repo_root),
        repo_root / "plaits/resources.cc",
        repo_root / "stmlib/dsp/units.cc",
        repo_root / "stmlib/utils/random.cc",
    ])
    reference_binary = host_binary(temp_dir, "cpu-bench-reference")
    compile_cpu_bench(
        reference_units,
        reference["source"]["header"],
        f'plaits::{reference["source"]["className"]}',
        [repo_root],
        reference_binary,
        requested_compiler,
    )

    package_ns = measure_cpu_cost(package_binary)
    reference_ns = measure_cpu_cost(reference_binary)
    return {
        "reference": CPU_REFERENCE_ENGINE,
        "packageNs": package_ns,
        "referenceNs": reference_ns,
        "ratio": package_ns / reference_ns if reference_ns > 0 else float("inf"),
    }


# What a host timing can and cannot tell you. Stated at every opportunity
# because the earlier version of this check reported "0.6x a stock engine" for
# an engine that ran at 281% of the hardware budget -- a reassuring number is
# worse than no number.
_CPU_HOST_CAVEAT = (
    "A host timing does NOT predict hardware cost: this machine's memory system\n"
    "  and pipeline differ in kind from a 72 MHz Cortex-M4. This catches only\n"
    "  pathologically expensive engines. For a real estimate run\n"
    "    qemu/estimate.py <package> --sweep\n"
    "  and before publishing, measure on the module itself:\n"
    "    build --hardware --cpu-probe   (the LEDs become a CPU meter)"
)


def report_cpu_reference_ratio(cost: dict[str, Any] | None) -> None:
    """Print the CPU verdict; raise when an engine cannot plausibly fit."""
    if cost is None:
        return
    ratio = cost["ratio"]
    detail = (
        f"{cost['packageNs']:.1f} ns/sample vs {cost['referenceNs']:.1f} for stock "
        f"{cost['reference']} — {ratio:.1f}x"
    )
    if ratio >= CPU_RATIO_FAIL:
        raise PackageError(
            f"CPU cost: {detail}\n"
            f"  The module allows ~1500 cycles per sample for synthesis, the LPG, the\n"
            f"  output stage and the UI combined. At {ratio:.1f}x a stock engine this cannot\n"
            f"  fit: the audio callback overruns (glitched, distorted output) and the\n"
            f"  starved UI loop stops refreshing the LEDs.\n"
            f"  Usual cause: work done per SAMPLE that only needs doing per BLOCK.\n"
            f"  Hoist anything that depends only on the parameters — envelopes, filter\n"
            f"  coefficients, per-voice gains and frequencies, and every exp/log/pow —\n"
            f"  above the per-sample loop, leaving it the oscillator itself."
        )
    if ratio > CPU_RATIO_WARN:
        print(f"⚠ CPU cost: {detail}")
        print(
            "  Heavier than any stock engine measured on this host.\n"
            "  " + _CPU_HOST_CAVEAT
        )
    else:
        print(f"✓ CPU cost (host smoke test only): {detail}")
        print("  " + _CPU_HOST_CAVEAT)


def compile_renderer(
    package: dict[str, Any], output: Path, requested_compiler: str | None,
    sanitizers: bool = False,
) -> None:
    manifest = package["manifest"]
    compiled = engine_translation_units(package, Path(__file__).with_name("render_model.cc"))
    command = [
        compiler_path(requested_compiler),
        "-std=c++11", MATH_CONSTANTS_DEFINE, "-DTEST", "-O2", "-Wall", "-Werror",
        "-Wno-unused-variable", "-Wno-unused-parameter",
        "-Wno-unused-local-typedefs", "-Wno-deprecated-declarations",
        f'-DPLAITS_LAB_ENGINE_HEADER="{engine_header_define(package)}"',
        f'-DPLAITS_LAB_ENGINE_CLASS=plaits::{manifest["source"]["className"]}',
        "-I", str(package["repo_root"]),
        "-I", str(package["source_root"]),
        *compiled,
        "-o", str(output),
    ]
    if sanitizers:
        # Same list host_sanitizers_available() probes for, so the capability
        # check can never test different flags than the real build uses.
        command[4:4] = SANITIZER_FLAGS
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        details = (result.stderr or result.stdout).strip()
        raise PackageError(f"host compilation failed\n{details}")


def wasm_compiler_path() -> str | None:
    return shutil.which("emcc")


# Emscripten exports the audition harness surface the AudioWorklet drives.
WASM_EXPORTS = ('["_init","_render","_set_params","_set_modulation_targets","_restart_modulation",'
               '"_trigger","_set_env_mode",'
               '"_set_stereo","_stereo_capable","_main_out","_aux_out",'
               '"_current_timbre","_current_morph"]')


def compile_wasm(package: dict[str, Any], output: Path) -> None:
    """Compile the package to a standalone .wasm for the browser live-audition
    AudioWorklet — same sources as the native renderer, but the STATEFUL
    wasm_audition.cc harness. Requires emscripten (emcc) on PATH; live audition
    is simply unavailable when it is not."""
    emcc = wasm_compiler_path()
    if emcc is None:
        raise PackageError("emscripten (emcc) not on PATH; run `source <emsdk>/emsdk_env.sh`")
    manifest = package["manifest"]
    compiled = engine_translation_units(package, Path(__file__).with_name("wasm_audition.cc"))
    command = [
        emcc,
        "-std=c++11", MATH_CONSTANTS_DEFINE, "-DTEST", "-O2",
        f'-DPLAITS_LAB_ENGINE_HEADER="{engine_header_define(package)}"',
        f'-DPLAITS_LAB_ENGINE_CLASS=plaits::{manifest["source"]["className"]}',
        "-I", str(package["repo_root"]),
        "-I", str(package["source_root"]),
        *compiled,
        "-sSTANDALONE_WASM=1",
        f"-sEXPORTED_FUNCTIONS={WASM_EXPORTS}",
        "--no-entry",
        "-o", str(output),
    ]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        details = (result.stderr or result.stdout).strip()
        raise PackageError(f"wasm compilation failed\n{details}")


def slug_to_class(slug: str) -> str:
    return "".join(part.capitalize() for part in slug.split("-")) + "Engine"


def default_scenarios() -> list[dict[str, Any]]:
    return [
        {
            "id": "hero",
            "name": "Main control sweep",
            "durationSeconds": 4,
            "note": 48,
            "triggerHz": 0,
            "controls": {
                "harmonics": [0.15, 0.85],
                "timbre": [0.2, 0.8],
                "morph": [0.1, 0.9],
                "macro": [0.0, 1.0],
            },
        },
        {
            "id": "triggered",
            "name": "Triggered extremes",
            "durationSeconds": 3,
            "note": 60,
            "triggerHz": 4,
            "controls": {
                "harmonics": [0.0, 1.0],
                "timbre": [0.0, 1.0],
                "morph": [0.0, 1.0],
                "macro": [0.0, 1.0],
            },
        },
    ]


MIT_BODY = """Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the \"Software\"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""

ISC_BODY = """Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED \"AS IS\" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
"""

_BSD_PREAMBLE = """Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
"""

_BSD_THIRD_CLAUSE = """
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.
"""

_BSD_DISCLAIMER = """
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""

LICENSE_TITLES = {
    "MIT": "MIT License",
    "ISC": "ISC License",
    "BSD-2-Clause": "BSD 2-Clause License",
    "BSD-3-Clause": "BSD 3-Clause License",
}

LICENSE_BODIES = {
    "MIT": MIT_BODY,
    "ISC": ISC_BODY,
    "BSD-2-Clause": _BSD_PREAMBLE + _BSD_DISCLAIMER,
    "BSD-3-Clause": _BSD_PREAMBLE + _BSD_THIRD_CLAUSE + _BSD_DISCLAIMER,
}


def current_year() -> int:
    return datetime.now(timezone.utc).year


def copyright_line(author: str, year: int | None = None) -> str:
    return f"Copyright (c) {year or current_year()} {author}"


def license_file_text(spdx: str, notices: list[str]) -> str:
    """Render a LICENSE file: a title, one copyright line per notice, one body.

    A fork carries TWO notices (upstream first, then the contributor) — the
    package is a derivative work, and the upstream notice has to survive under
    every license in ALLOWED_LICENSES.
    """
    require(spdx in ALLOWED_LICENSES, f"license must be one of {sorted(ALLOWED_LICENSES)}")
    require(bool(notices), "a LICENSE needs at least one copyright notice")
    return f"{LICENSE_TITLES[spdx]}\n\n" + "\n".join(notices) + f"\n\n{LICENSE_BODIES[spdx]}"


def mit_license(author: str, year: int | None = None) -> str:
    return license_file_text("MIT", [copyright_line(author, year)])


def _normalize_license_text(text: str) -> str:
    """Collapse a license to comparable words: lowercase, punctuation-free.

    Keeps "/" so ISC's "and/or distribute" stays distinguishable, and drops the
    comment markers a license pasted into a source header would carry.
    """
    return " ".join(re.sub(r"[^a-z0-9/]+", " ", text.lower()).split())


def identify_license_text(text: str) -> str | None:
    """Return the SPDX id whose canonical text `text` contains, or None.

    Matches on each license's operative grant rather than a whole-file digest,
    so a real LICENSE still identifies after a contributor reflows it, adds a
    second copyright holder, or edits the year.
    """
    normalized = _normalize_license_text(text)
    if "permission to use copy modify and/or distribute this software for any purpose" in normalized:
        return "ISC"
    if "permission is hereby granted free of charge" in normalized and "sublicense" in normalized:
        return "MIT"
    if "redistribution and use in source and binary forms" in normalized:
        if "neither the name of" in normalized:
            return "BSD-3-Clause"
        return "BSD-2-Clause"
    return None


SPDX_PATTERN = re.compile(r"SPDX-License-Identifier:\s*([A-Za-z0-9.\-+]+)")


def source_spdx_id(text: str) -> str | None:
    """Read the SPDX-License-Identifier tag out of a source file's header comment."""
    match = SPDX_PATTERN.search(text)
    return match.group(1) if match else None


def blank_source(slug: str, class_name: str, author: str,
                 spdx: str = DEFAULT_LICENSE, year: int | None = None) -> tuple[str, str]:
    guard = f"PLAITS_LAB_{slug.replace('-', '_').upper()}_ENGINE_H_"
    notice = f"// Copyright {year or current_year()} {author}.\n// SPDX-License-Identifier: {spdx}"
    header = f"""{notice}

#ifndef {guard}
#define {guard}

#include \"plaits/dsp/engine/engine.h\"

namespace plaits {{

class {class_name} : public Engine {{
 public:
  {class_name}() {{ }}
  ~{class_name}() {{ }}
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void LoadUserData(const uint8_t* user_data) {{ }}
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);

 private:
  float phase_;
  DISALLOW_COPY_AND_ASSIGN({class_name});
}};

}}  // namespace plaits

#endif  // {guard}
"""
    implementation = f"""{notice}

#include \"{slug}_engine.h\"

// The firmware is bare-metal — it can't link libm (std::sin/exp/log/pow), so use
// the shared LUTs: plaits::Sine here, stmlib::SemitonesToRatio for 2^x.
#include \"plaits/dsp/oscillator/sine_oscillator.h\"

namespace plaits {{

void {class_name}::Init(stmlib::BufferAllocator* allocator) {{ Reset(); }}
void {class_name}::Reset() {{ phase_ = 0.0f; }}

void {class_name}::Render(const EngineParameters& parameters, float* out,
    float* aux, size_t size, bool* already_enveloped) {{
  const float frequency = NoteToFrequency(parameters.note);
  for (size_t i = 0; i < size; ++i) {{
    phase_ += frequency;
    phase_ -= static_cast<int>(phase_);
    out[i] = 0.5f * Sine(phase_);   // sine LUT — phase in [0, 1)
    aux[i] = out[i] * (0.25f + 0.75f * parameters.macro);
  }}
}}

}}  // namespace plaits
"""
    return header, implementation


_COPYRIGHT_LINE = re.compile(r"^\s*(?://\s*)?(Copyright\s+(?:\(c\)\s*)?[0-9].*?)\s*$", re.MULTILINE)


def extract_copyright_notices(text: str) -> list[str]:
    """Pull the copyright lines out of a LICENSE or a source-header comment.

    Order-preserving and de-duplicated, so re-forking an already-forked package
    doesn't stack the same notice twice.
    """
    return list(dict.fromkeys(match.group(1) for match in _COPYRIGHT_LINE.finditer(text)))


def upstream_attribution(package_id: str, fallback_source: str) -> tuple[str, list[str]]:
    """Return (license id, copyright notices) for a built-in package being forked.

    Prefers the package's own LICENSE; falls back to the notices in the source it
    vendors, so a catalog entry with no package directory still attributes.
    """
    package_dir = PACKAGES_DIR / package_id
    manifest_path = package_dir / "plaits-engine.json"
    license_path = package_dir / "LICENSE"
    spdx = DEFAULT_LICENSE
    if manifest_path.is_file():
        declared = read_json(manifest_path).get("license")
        if declared in ALLOWED_LICENSES:
            spdx = declared
    notices: list[str] = []
    if license_path.is_file():
        notices = extract_copyright_notices(license_path.read_text(encoding="utf-8"))
    if not notices:
        notices = extract_copyright_notices(fallback_source)
    return spdx, notices


def init_command(args: argparse.Namespace) -> int:
    output = Path(args.output).resolve()
    require(not output.exists(), f"output already exists: {output}")
    slug = args.slug or output.name.lower().replace("_", "-")
    require(CATALOG_ID_PATTERN.fullmatch(slug) is not None, "package slug must be lowercase words joined by hyphens")
    package_id = args.package_id or f"community/{slug}"
    require(ID_PATTERN.fullmatch(package_id) is not None, "package ID must use namespace/slug form")
    class_name = slug_to_class(slug)
    # Older callers (and the tests predating --license) build args without it.
    spdx = getattr(args, "license", None) or DEFAULT_LICENSE
    require(spdx in ALLOWED_LICENSES,
            f"license must be one of {sorted(ALLOWED_LICENSES)}")
    year = current_year()
    source_dir = output / "src"
    tests_dir = output / "tests"
    source_dir.mkdir(parents=True)
    tests_dir.mkdir()

    if args.from_engine == "blank":
        name = args.name or " ".join(part.capitalize() for part in slug.split("-"))
        controls = ["Harmonics", "Timbre", "Morph", "Macro"]
        outputs = ["Primary model output", "Alternate model output"]
        description = "A new Plaits Lab synthesis model ready for contributor development."
        family = "Experimental"
        tags = ["community", "experimental"]
        post = {"alreadyEnveloped": False, "outGain": 0.8, "auxGain": 0.8}
        header, implementation = blank_source(slug, class_name, args.author, spdx, year)
        license_notices = [copyright_line(args.author, year)]
        forked_from = None
        forked_shared: list[str] = []
    else:
        upstream, public = builtin_engine(args.from_engine)
        name = args.name or f"{upstream['name']} Fork"
        controls = upstream["controls"]
        outputs = upstream["outputs"]
        description = f"A community fork of {upstream['name']} for Plaits Lab experimentation."
        family = upstream["family"]
        tags = list(dict.fromkeys([*upstream["tags"], "community"]))[:12]
        post = upstream["postProcessing"]
        original_class = upstream["source"]["className"]
        header_path = REPO_ROOT / upstream["source"]["header"]
        primary = next(
            REPO_ROOT / item
            for item in upstream["source"]["files"]
            if Path(item).stem == header_path.stem
        )
        header = header_path.read_text(encoding="utf-8").replace(original_class, class_name)
        guard_match = re.search(r"^#ifndef\s+([A-Z0-9_]+)", header, re.MULTILINE)
        if guard_match:
            new_guard = f"PLAITS_LAB_{slug.replace('-', '_').upper()}_ENGINE_H_"
            header = header.replace(guard_match.group(1), new_guard)
        implementation = primary.read_text(encoding="utf-8").replace(original_class, class_name)
        implementation = implementation.replace(upstream["source"]["header"], f"{slug}_engine.h")
        forked_from = upstream["id"]
        forked_shared = list(upstream.get("sharedModules", []))
        upstream_reference = f"{upstream['packageId']}@{public['version']} ({public['digest']})"

        # A fork is a DERIVATIVE WORK: the upstream notice has to survive, and
        # the contributor's own notice has to say it's a modification rather
        # than replace what it was modified from. Keeping the fork on upstream's
        # license also keeps each source file a single-license file, so the SPDX
        # tag `check` enforces below stays a plain id instead of an expression.
        upstream_spdx, upstream_notices = upstream_attribution(upstream["packageId"], header)
        require(spdx == upstream_spdx,
                f"a fork of {upstream['name']} must stay {upstream_spdx} (its upstream license), "
                f"not {spdx}; start from --from blank to choose your own license")
        vendored_notice = (
            f"// Copyright {year} {args.author}.\n"
            f"// SPDX-License-Identifier: {spdx}\n"
            "//\n"
            # The pinned digest lives in the manifest's `upstream` field; the
            # header keeps the human-readable half so it stays readable.
            f"// Modified from {upstream['name']} "
            f"({upstream['packageId']}@{public['version']}) for Plaits Lab.\n"
            "// The original copyright and license notice follow.\n\n"
        )
        header = vendored_notice + header
        implementation = vendored_notice + implementation
        license_notices = [*upstream_notices, copyright_line(args.author, year)]

    header_name = f"{slug}_engine.h"
    implementation_name = f"{slug}_engine.cc"
    (source_dir / header_name).write_text(header, encoding="utf-8")
    (source_dir / implementation_name).write_text(implementation, encoding="utf-8")
    manifest: dict[str, Any] = {
        "schemaVersion": 1,
        "sdk": SDK_VERSION,
        "packageType": "community",
        "id": package_id,
        "catalogId": slug,
        "version": "0.1.0",
        "name": name,
        "author": args.author,
        "origin": "Community",
        **({"artwork": {"color": args.color}} if getattr(args, "color", None) else {}),
        "license": spdx,
        "description": description,
        "family": family,
        "tags": tags,
        "controls": [
            {"id": control_id, "label": label, "description": f"Controls the model's {label.lower()} dimension."}
            for control_id, label in zip(CONTROL_IDS, controls)
        ],
        "outputs": {"main": outputs[0], "aux": outputs[1]},
        "source": {"root": "src", "header": header_name, "files": [implementation_name], "className": class_name},
        "postProcessing": post,
        "scenarios": "tests/scenarios.json",
    }
    if forked_from:
        manifest["forkedFrom"] = forked_from
        manifest["upstream"] = upstream_reference
        if forked_shared:
            manifest["sharedModules"] = forked_shared
    (output / "plaits-engine.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (tests_dir / "scenarios.json").write_text(json.dumps(default_scenarios(), indent=2) + "\n", encoding="utf-8")
    (output / "LICENSE").write_text(license_file_text(spdx, license_notices), encoding="utf-8")
    (output / "README.md").write_text(
        f"# {name}\n\nA Plaits Lab community engine package.\n\n"
        # NOT cli_invocation() here: this README is written INTO the package
        # and published, so it must not bake in the author's own checkout path.
        f"From your eurorack checkout:\n\n"
        f"    python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py check <path-to-this-package> --full\n",
        encoding="utf-8",
    )
    load_package(str(output))
    print(f"created {package_id}@0.1.0 in {output}")
    return 0


def catalog_command(args: argparse.Namespace) -> int:
    catalog, public = load_builtin_catalog()
    for engine_id, engine in catalog.items():
        item = public[engine_id]
        print(f"{engine_id:24} {engine['name']:24} {item['version']}  {engine['origin']}")
    return 0


def modules_command(args: argparse.Namespace) -> int:
    modules = load_shared_modules()
    if not modules:
        print("no shared modules are available")
        return 0
    for module_id, module in modules.items():
        print(f"{module_id:20} {module.get('name', module_id)}")
        headers = ", ".join(module.get("headers", []))
        if headers:
            print(f"  include: {headers}")
        description = module.get("description")
        if description:
            print(f"  {description}")
    return 0


def report_autodeclared(package: dict[str, Any]) -> None:
    for module_id in package["autodeclared"]:
        print(f"linked shared module '{module_id}' "
              "(added to sharedModules in plaits-engine.json)")


def check_command(args: argparse.Namespace) -> int:
    package = load_package(args.package, autodeclare=True)
    report_autodeclared(package)
    # --full is the submission gate, so it must be runnable everywhere. Where the
    # host cannot link the sanitizers, hand the whole check to the builder image.
    # Delegating here — before any output — keeps the container's own ✓ lines the
    # only ones printed, and the autodeclare pass above has already settled
    # plaits-engine.json, so mounting the package read-only is safe.
    if args.full and not args.no_compile and not args.native \
            and not host_sanitizers_available(args.compiler):
        # Forward --arm too, or asking for the hardware-toolchain compile would
        # be silently dropped on the way into the container — the run would look
        # clean while having skipped exactly the check that was requested. The
        # image carries the ARM toolchain, so --native runs it there directly.
        delegated = ["check", "/contributor", "--full", "--native"]
        if args.arm:
            delegated += ["--arm", "--toolchain", args.toolchain]
        return run_sanitized_in_docker(package, args, "check --full", delegated)
    print(f"✓ package {package['manifest']['id']}@{package['manifest']['version']}")
    print(f"✓ metadata, scenarios, source policy, and {package['manifest']['license']} "
          "licensing (LICENSE text and per-file SPDX tags agree)")
    cpu_cost: dict[str, Any] | None = None
    if not args.no_compile:
        with tempfile.TemporaryDirectory(prefix="plaits-lab-check-") as temp_dir:
            renderer = host_binary(Path(temp_dir), "render-model")
            compile_renderer(package, renderer, args.compiler, sanitizers=args.full)
            if args.full:
                for scenario in package["scenarios"]:
                    preview = Path(temp_dir) / f"{scenario['id']}.wav"
                    elapsed = run_scenario(package, renderer, scenario, preview)
                    metrics = analyze_wav(preview, scenario["durationSeconds"], elapsed)
                    print(
                        f"✓ {scenario['id']}: peak {metrics['peak']:.4f}, "
                        f"RMS {metrics['rms']:.4f}, DC {metrics['dcOffset']:.5f}, "
                        f"host {metrics['hostRealtimeRatio']:.2f}× realtime"
                    )
                # A toolchain quirk in the reference build must not block a
                # contributor, so an unavailable measurement degrades to a note.
                # A measurement that IS available is enforced below.
                try:
                    cpu_cost = cpu_reference_ratio(package, args.compiler, Path(temp_dir))
                except PackageError as error:
                    print(f"  note: CPU reference check unavailable ({error})")
        print("✓ host compilation")
        if args.full:
            print("✓ sanitizer execution and audio health")
            report_cpu_reference_ratio(cpu_cost)
            print("\n  CPU: hardware is the authority. `check` cannot measure it;\n"
                  "  qemu/estimate.py --sweep estimates it with a stated error band,\n"
                  "  and --cpu-probe measures it for real. Publication requires the\n"
                  "  hardware measurement.")
        if args.full and not args.arm:
            print("  tip: add --arm to also compile against the hardware (ARM) toolchain")
    if args.arm:
        arm_compile_check(package, args)
        print("✓ ARM (hardware-toolchain) compilation")
    return 0


def scenario_by_id(package: dict[str, Any], scenario_id: str) -> dict[str, Any]:
    for scenario in package["scenarios"]:
        if scenario["id"] == scenario_id:
            return scenario
    available = ", ".join(item["id"] for item in package["scenarios"])
    raise PackageError(f"unknown scenario {scenario_id!r}; available: {available}")


def run_scenario(
    package: dict[str, Any], renderer: Path, scenario: dict[str, Any], output: Path,
    *, out_gain: float | None = None, aux_gain: float | None = None,
) -> float:
    controls = scenario["controls"]
    post = package["manifest"]["postProcessing"]
    render_out_gain = post["outGain"] if out_gain is None else out_gain
    render_aux_gain = post["auxGain"] if aux_gain is None else aux_gain
    command = [
        str(renderer), str(output), str(scenario["durationSeconds"]), str(scenario["note"]),
        str(controls["harmonics"][0]), str(controls["harmonics"][1]),
        str(controls["timbre"][0]), str(controls["timbre"][1]),
        str(controls["morph"][0]), str(controls["morph"][1]),
        str(controls["macro"][0]), str(controls["macro"][1]),
        str(scenario["triggerHz"]), str(render_out_gain), str(render_aux_gain),
    ]
    started = time.monotonic()
    result = subprocess.run(
        command, text=True, capture_output=True, check=False,
        # Harmless for a non-sanitized build, which simply ignores it.
        env={**os.environ, **SANITIZER_RUNTIME_ENV},
    )
    elapsed = time.monotonic() - started
    if result.returncode:
        raise PackageError(f"scenario {scenario['id']} failed: {(result.stderr or result.stdout).strip()}")
    return elapsed


def analyze_wav(path: Path, expected_seconds: float, render_seconds: float) -> dict[str, float | int]:
    try:
        with wave.open(str(path), "rb") as audio:
            channels = audio.getnchannels()
            sample_width = audio.getsampwidth()
            sample_rate = audio.getframerate()
            frames = audio.getnframes()
            raw = audio.readframes(frames)
    except (wave.Error, EOFError) as error:
        raise PackageError(f"invalid WAV preview {path.name}: {error}") from error
    require(channels == 2 and sample_width == 2 and sample_rate == 48000,
            f"{path.name} must be 48 kHz, 16-bit stereo")
    require(abs(frames / sample_rate - expected_seconds) <= 0.02,
            f"{path.name} has the wrong duration")
    samples = struct.unpack(f"<{len(raw) // 2}h", raw)
    normalized = [sample / 32768.0 for sample in samples]
    peak = max((abs(sample) for sample in normalized), default=0.0)
    rms = math.sqrt(sum(sample * sample for sample in normalized) / max(1, len(normalized)))
    dc_offset = sum(normalized) / max(1, len(normalized))
    silent_fraction = sum(abs(sample) < 1.0 / 32768.0 for sample in normalized) / max(1, len(normalized))
    require(peak >= 0.001 and rms >= 0.0001,
            f"{path.name} is silent — Render() must write non-zero samples to out[]/aux[] "
            f"for this scenario's control settings (check tests/scenarios.json)")
    require(peak <= 1.0, f"{path.name} contains invalid PCM amplitude")
    require(abs(dc_offset) <= 0.2,
            f"{path.name} has excessive DC offset ({dc_offset:.4f}) — center the waveform around zero; "
            f"a constant added to every out[] sample shifts it (or lower postProcessing.outGain/auxGain)")
    return {
        "channels": channels,
        "sampleRate": sample_rate,
        "frames": frames,
        "peak": peak,
        "rms": rms,
        "dcOffset": dc_offset,
        "silentFraction": silent_fraction,
        "hostRealtimeRatio": render_seconds / max(expected_seconds, 0.001),
    }


def render_command(args: argparse.Namespace) -> int:
    package = load_package(args.package, autodeclare=True)
    report_autodeclared(package)
    scenario = scenario_by_id(package, args.scenario)
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="plaits-lab-render-") as temp_dir:
        renderer = host_binary(Path(temp_dir), "render-model")
        compile_renderer(package, renderer, args.compiler)
        elapsed = run_scenario(package, renderer, scenario, output)
        metrics = analyze_wav(output, scenario["durationSeconds"], elapsed)
        print(f"rendered {output} (peak {metrics['peak']:.4f}, RMS {metrics['rms']:.4f})")
    return 0


def package_content_digest(package_dir: Path) -> str:
    import hashlib

    digest = hashlib.sha256()
    # Order on the POSIX relative path, never on the Path object: Path ordering
    # is case-folded and backslash-separated on Windows, so the same package
    # would hash in a different file order there and produce a different digest.
    files = sorted(
        (path for path in package_dir.rglob("*")
         if path.is_file() and ".plaits-lab" not in path.parts),
        key=lambda path: path.relative_to(package_dir).as_posix(),
    )
    for path in files:
        relative = path.relative_to(package_dir).as_posix()
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return "sha256:" + digest.hexdigest()


def add_zip_file(archive: zipfile.ZipFile, name: str, data: bytes) -> None:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data)


# --- Contributor identity and submission upload -----------------------------
#
# A contributor is a bearer token and nothing else: the server stores only
# SHA-256(token) as the owner of a submission, which is what lets you check,
# re-download or withdraw your own drafts. The token is minted HERE rather than
# in the browser so a first submission needs no web visit at all; the
# contributor center accepts the same token pasted in to FOLLOW submissions,
# and `login` adopts an existing identity on a second machine.
#
# It lives in a USER-level config file, never inside a package: package files
# are zipped into the bundle and eventually vendored into a public repo, and
# one identity should span all of your packages anyway.

DEFAULT_API_BASE = "https://rubato.audio"
TOKEN_MIN, TOKEN_MAX = 32, 256


def cli_invocation() -> str:
    """How this tool was actually invoked, e.g.
    "python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py".

    There is no `plaits-lab` on anyone's PATH — the README defines it as a
    shell alias and prose uses it as the tool's name — so any command this
    program PRINTS has to spell out the real invocation, or it is not
    runnable for the person reading it."""
    return f"python3 {sys.argv[0]}"


def credentials_path() -> Path:
    """The per-user credential file, on each platform's own config path."""
    override = os.environ.get("PLAITS_LAB_CONFIG_DIR")
    if override:
        return Path(override).expanduser() / "credentials.json"
    if sys.platform == "win32":
        base = os.environ.get("APPDATA")
        root = Path(base) if base else Path.home() / "AppData/Roaming"
    elif sys.platform == "darwin":
        root = Path.home() / "Library/Application Support"
    else:
        base = os.environ.get("XDG_CONFIG_HOME")
        root = Path(base) if base else Path.home() / ".config"
    return root / "plaits-lab/credentials.json"


def read_token() -> str | None:
    path = credentials_path()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    token = data.get("token") if isinstance(data, dict) else None
    return token if isinstance(token, str) and token else None


def write_token(token: str) -> Path:
    require(TOKEN_MIN <= len(token) <= TOKEN_MAX,
            f"a contributor token must be {TOKEN_MIN}-{TOKEN_MAX} characters")
    path = credentials_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    # Written 0600 where the OS has POSIX modes; opened O_CREAT|O_WRONLY|O_TRUNC
    # so the mode applies at creation rather than after a readable window.
    descriptor = os.open(path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        json.dump({"token": token}, handle, indent=2)
        handle.write("\n")
    return path


def ensure_token() -> str:
    token = read_token()
    if token:
        return token
    # Same shape the contributor center mints, from the CSPRNG.
    token = f"{secrets.token_hex(16)}-{secrets.token_hex(16)}"
    write_token(token)
    return token


def token_fingerprint(token: str) -> str:
    """First 8 hex of the owner hash the server stores — safe to show."""
    return hashlib.sha256(token.encode("utf-8")).hexdigest()[:8]


def api_base(args: argparse.Namespace) -> str:
    base = getattr(args, "api", None) or os.environ.get("PLAITS_LAB_API") or DEFAULT_API_BASE
    return base.rstrip("/")


class ApiError(PackageError):
    """A failed API call, carrying the status so a caller can recover from it."""

    def __init__(self, message: str, status: int, code: str = "") -> None:
        super().__init__(message)
        self.status = status
        self.code = code


def api_call(base: str, path: str, *, token: str | None = None, method: str = "GET",
             payload: Any = None, body: bytes | None = None,
             content_type: str | None = None) -> dict[str, Any]:
    """One JSON API call. Raises PackageError with the server's own message."""
    data = body
    # A real User-Agent is REQUIRED, not politeness: Cloudflare's bot
    # protection 403s Python's default "Python-urllib/x.y" outright, which
    # surfaces as an inexplicable failure against production while curl and
    # local wrangler both work.
    headers = {
        "Accept": "application/json",
        "User-Agent": f"plaits-lab/{SDK_VERSION} (+https://rubato.audio/plaits-palette)",
    }
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    elif content_type:
        headers["Content-Type"] = content_type
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if data is not None:
        # The API requires a bounded Content-Length; urllib sets it for bytes.
        headers["Content-Length"] = str(len(data))
    request = urllib.request.Request(f"{base}{path}", data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            return json.loads(response.read().decode("utf-8") or "{}")
    except urllib.error.HTTPError as error:
        detail, code = "", ""
        try:
            body_json = json.loads(error.read().decode("utf-8"))
            detail = body_json.get("error", {}).get("message", "")
            code = body_json.get("error", {}).get("code", "")
        except (ValueError, OSError):
            pass
        raise ApiError(
            f"{method} {path} failed ({error.code}){': ' + detail if detail else ''}",
            error.code, code,
        ) from error
    except urllib.error.URLError as error:
        raise PackageError(
            f"could not reach {base}: {error.reason}. The bundle is already built, so "
            f"re-running submit once you are back online skips straight to uploading it."
        ) from error


def multipart_body(fields: dict[str, str], filename: str, file_bytes: bytes) -> tuple[bytes, str]:
    """Encode one file plus text fields as multipart/form-data."""
    boundary = f"----plaits-lab-{secrets.token_hex(16)}"
    parts: list[bytes] = []
    for name, value in fields.items():
        parts.append(
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"{name}\"\r\n\r\n"
            f"{value}\r\n".encode("utf-8")
        )
    parts.append(
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"bundle\"; "
        f"filename=\"{filename}\"\r\nContent-Type: application/zip\r\n\r\n".encode("utf-8")
    )
    parts.append(file_bytes)
    parts.append(f"\r\n--{boundary}--\r\n".encode("utf-8"))
    return b"".join(parts), f"multipart/form-data; boundary={boundary}"


def bundle_claims(bundle_path: Path) -> dict[str, Any]:
    """Read what the built bundle itself claims — the same bytes the server
    verifies, so the upload never asserts anything the zip does not contain."""
    with zipfile.ZipFile(bundle_path) as archive:
        try:
            submission = json.loads(archive.read("submission.json").decode("utf-8"))
            manifest_bytes = archive.read("package/plaits-engine.json")
        except KeyError as error:
            raise PackageError(
                f"{bundle_path.name} is not a plaits-lab submission bundle ({error})"
            ) from error
    manifest = json.loads(manifest_bytes.decode("utf-8"))
    return {
        "packageId": str(manifest["id"]),
        "version": str(manifest["version"]),
        "digest": str(submission["digest"]),
        "manifest": manifest_bytes.decode("utf-8"),
        "license": str(manifest.get("license", "unknown")),
        "author": str(manifest.get("author", "")),
    }


def confirm_submission(claims: dict[str, Any], contract: dict[str, Any],
                       bundle_path: Path, args: argparse.Namespace) -> dict[str, str]:
    """The loud step. Returns the affirmation to send, or raises."""
    size_kb = bundle_path.stat().st_size / 1024.0
    print()
    print("=" * 72)
    print("  SUBMITTING TO RUBATO AUDIO — this leaves your machine")
    print("=" * 72)
    print(f"  package   {claims['packageId']} {claims['version']}")
    print(f"  license   {claims['license']}")
    print(f"  digest    {claims['digest']}")
    print(f"  bundle    {bundle_path.name} ({size_kb:.0f} KB) — full source + preview renders")
    print()
    print("  Your engine's SOURCE is uploaded for maintainer review. If it is")
    print("  published it is compiled into firmware that other people flash.")
    print()
    for line in textwrap.wrap(contract["text"], 68):
        print(f"  {line}")
    print("=" * 72)
    print()

    author = (args.author or "").strip()
    contact = (args.contact or "").strip()
    interactive = sys.stdin.isatty() and not args.yes
    if interactive:
        default_author = author or claims["author"]
        prompt = f"Rights holder [{default_author}]: " if default_author else "Rights holder: "
        author = input(prompt).strip() or default_author
        if not contact:
            contact = input("Contact (optional, blank to stay anonymous): ").strip()
    require(author, "a rights holder is required — pass --author, or answer the prompt")

    if args.yes:
        print(f"--yes given: submitting as {author}.")
        return {"author": author, "contact": contact}
    require(sys.stdin.isatty(),
            "submitting needs a terminal to confirm — pass --yes with --author to "
            "submit non-interactively, or --bundle-only to build the zip without sending it")
    typed = input('Type "submit" to send it, anything else to stop: ').strip()
    require(typed == "submit", "not submitted — the bundle is built and unchanged")
    return {"author": author, "contact": contact}


def upload_submission(bundle_path: Path, args: argparse.Namespace) -> int:
    base = api_base(args)
    claims = bundle_claims(bundle_path)

    status = api_call(base, "/api/contributors/status")
    if not status.get("intakeEnabled"):
        raise PackageError(
            f"community intake is not open at {base} right now. The bundle is built "
            f"at {bundle_path} — keep it and run submit again when intake reopens."
        )
    contract = status.get("affirmation") or {}
    require(contract.get("version") and contract.get("text"),
            "the server did not offer an affirmation contract to agree to")

    affirmation = confirm_submission(claims, contract, bundle_path, args)
    token = ensure_token()

    body, content_type = multipart_body(
        {
            "packageId": claims["packageId"],
            "version": claims["version"],
            "digest": claims["digest"],
            "manifest": claims["manifest"],
        },
        bundle_path.name,
        bundle_path.read_bytes(),
    )
    print("uploading…", flush=True)
    try:
        created = api_call(base, "/api/contributors/submissions", token=token, method="POST",
                           body=body, content_type=content_type)
        submission_id = str(created["id"])
        print(f"draft created  {submission_id}")
    except ApiError as error:
        # Submitting is two calls — upload, then send to review — so a network
        # blip between them leaves a draft this package/version can never
        # re-upload (the pair is unique). Adopt that draft and finish it rather
        # than stranding the contributor on a version bump.
        if error.status != 409:
            raise
        mine = api_call(base, "/api/contributors/submissions", token=token)
        draft = next((row for row in mine.get("submissions", [])
                      if row.get("packageId") == claims["packageId"]
                      and row.get("version") == claims["version"]
                      and row.get("state") == "draft"), None)
        if not draft:
            raise PackageError(
                f"{claims['packageId']} {claims['version']} has already been submitted. "
                f"Bump the version in plaits-engine.json to submit a revision."
            ) from error
        submission_id = str(draft["id"])
        print(f"resuming the draft left by an interrupted submit  {submission_id}")

    sent = api_call(base, f"/api/contributors/submissions/{submission_id}/submit",
                    token=token, method="POST", payload={"affirmation": {
                        "agreed": True,
                        "version": contract["version"],
                        "author": affirmation["author"],
                        "contact": affirmation["contact"],
                    }})
    print(f"state          {sent.get('state', 'in-review')}")
    print()
    # Name the destination precisely, in the same words the page uses for it:
    # "paste this somewhere over there" sends people hunting.
    print("Your contributor token — the only thing identifying your submissions:")
    print(f"  {read_token()}")
    print(f"  (stored at {credentials_path()};"
          f" `{cli_invocation()} whoami --show` prints it again)")
    print()
    print(f"To follow this submission in a browser, open")
    print(f"  {base}/plaits-palette/contribute")
    print('and paste that token into "Follow a CLI submission" under step 9.')
    return 0


def login_command(args: argparse.Namespace) -> int:
    """Reuse one contributor identity elsewhere — a second machine, a rebuilt
    one, or the token an older contributor-center visit already made."""
    token = (args.token or "").strip()
    if not token:
        require(sys.stdin.isatty(), "pass --token when not running in a terminal")
        token = input(f"Contributor token (`{cli_invocation()} whoami --show` "
                      f"on the other machine): ").strip()
    path = write_token(token)
    print(f"stored contributor token {token_fingerprint(token)}… at {path}")
    return 0


def whoami_command(args: argparse.Namespace) -> int:
    token = read_token()
    path = credentials_path()
    if not token:
        print(f"no contributor token yet ({path})")
        print(f"one is minted on your first `{cli_invocation()} submit`, "
              f"or run `{cli_invocation()} login`.")
        return 0
    print(f"contributor {token_fingerprint(token)}…  ({path})")
    if args.show:
        print(token)
    else:
        print("run with --show to print the token itself (it identifies your submissions)")
    return 0


def submit_command(args: argparse.Namespace) -> int:
    package = load_package(args.package)
    default_name = f"{package['manifest'].get('catalogId', 'package')}.plaits-package.zip"
    output = Path(args.output or default_name).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    # A bundle is only accepted if it passed the sanitizers, so submit builds one
    # in the builder image when the host cannot. The output directory is the one
    # writable mount; the container writes the zip straight into it, and the
    # digest it records is identical to a host-built one (package_content_digest
    # orders on POSIX relative paths, so it does not vary by platform).
    #
    # The inner run BUILDS ONLY (--bundle-only): it has no credentials and no
    # terminal to confirm at, and letting it reach the upload would submit twice
    # — once from the container, once from here. Uploading is the outer
    # process's job, after the container hands back a bundle.
    if not args.native and not host_sanitizers_available(args.compiler):
        run_sanitized_in_docker(
            package, args, "submit",
            ["submit", "/contributor", "--output", f"/output/{output.name}",
             "--native", "--bundle-only"],
            extra_mounts=["-v", f"{output.parent}:/output"],
        )
        return finish_submit(output, args)
    with tempfile.TemporaryDirectory(prefix="plaits-lab-submit-") as temp_dir:
        preview_dir = Path(temp_dir) / "previews"
        preview_dir.mkdir()
        renderer = host_binary(Path(temp_dir), "render-model")
        compile_renderer(package, renderer, args.compiler, sanitizers=True)
        analyses: dict[str, dict[str, float | int]] = {}
        for scenario in package["scenarios"]:
            preview = preview_dir / f"{scenario['id']}.wav"
            elapsed = run_scenario(package, renderer, scenario, preview)
            analyses[scenario["id"]] = analyze_wav(preview, scenario["durationSeconds"], elapsed)

        submission = {
            "schemaVersion": 1,
            "package": package["manifest"]["id"],
            "version": package["manifest"]["version"],
            "digest": package_content_digest(package["directory"]),
            "state": "draft",
            "createdAt": datetime.now(timezone.utc).isoformat(),
            "checks": [
                "package", "license", "source-policy", "host-compile",
                "sanitizers", "preview-scenarios", "audio-health",
            ],
            "audioAnalysis": analyses,
            "nextStates": ["in-review", "withdrawn"],
        }
        with zipfile.ZipFile(output, "w") as archive:
            # Same POSIX-relative ordering as package_content_digest, so a bundle
            # built on Windows lays its entries out identically to one built here.
            # Same exclusion as package_content_digest: the reserved
            # .plaits-lab/ scratch directory is LOCAL state, so it must not
            # ride into the bundle either. Carrying a file the digest does not
            # cover would let un-reviewed content reach a vendored package,
            # and intake rejects such a bundle outright.
            for path in sorted(
                (item for item in package["directory"].rglob("*")
                 if item.is_file() and ".plaits-lab" not in item.parts),
                key=lambda item: item.relative_to(package["directory"]).as_posix(),
            ):
                add_zip_file(archive, f"package/{path.relative_to(package['directory']).as_posix()}", path.read_bytes())
            for path in sorted(preview_dir.glob("*.wav"), key=lambda item: item.name):
                add_zip_file(archive, f"previews/{path.name}", path.read_bytes())
            add_zip_file(archive, "submission.json", (json.dumps(submission, indent=2) + "\n").encode("utf-8"))
    print(f"created draft submission {output}")
    print(f"package digest {submission['digest']}")
    return finish_submit(output, args)


def finish_submit(output: Path, args: argparse.Namespace) -> int:
    """Upload the built bundle, unless the caller only wanted the zip."""
    if args.bundle_only:
        print(f"--bundle-only: not submitted. Run `{cli_invocation()} submit "
              f"{args.package}` without it to send this package for review — "
              f"submitting is the CLI's job, there is no browser upload.")
        return 0
    return upload_submission(output, args)


def cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def cpp_float(value: float) -> str:
    return f"{value:.1f}f"


# The STM32F373 application flash region (stmlib linker script). The whole
# firmware — base + every engine — must fit in this.
PLAITS_FLASH_BYTES = 224 * 1024


def arm_flash_footprint(size_tool: Path, paths: list[str]) -> int | None:
    """Sum of .text + .data (the flash footprint) across the given ARM object(s)
    or ELF, via arm-none-eabi-size; None if unavailable or unparseable."""
    if not size_tool.is_file():
        return None
    total = 0
    for path in paths:
        result = subprocess.run([str(size_tool), str(path)], text=True, capture_output=True, check=False)
        if result.returncode != 0:
            return None
        try:
            fields = result.stdout.strip().splitlines()[-1].split()
            total += int(fields[0]) + int(fields[1])  # text (code + rodata) + data
        except (IndexError, ValueError):
            return None
    return total


# A bench firmware carries many STOCK engines so their on-hardware cost can be
# measured in one flash, by turning the model knob, instead of one build-and-
# flash cycle per engine.
#
# Why stock engines specifically: the estimate that `check` gives a contributor
# has to be calibrated against the population it will be used on -- REAL engines,
# whose instruction mixes are blends. Synthetic workloads that each do one thing
# exclusively produced a 15x spread in cycles-per-instruction and calibrated
# nothing useful, while three Helix builds all landed near 4.3. These sixteen
# span additive, FM, granular, physical modelling, percussion, wavetable and
# noise, and roughly an 8x range of cost, so they map the real distribution.
#
# Engines needing user data (dx7 banks) or special host behaviour (speech,
# chiptune) are excluded: they idle without their data and would measure nothing.
STOCK_BENCH_ENGINES = (
    "two-op-fm", "particle-noise", "inharmonic-string", "modal-resonator",
    "analog-bass-drum", "granular-formant", "harmonic", "chords",
    "phase-distortion", "string-machine", "swarm", "virtual-analog",
    "wave-terrain", "waveshaping", "filtered-noise", "pulsar",
)


def render_stock_bench_config(engine_ids: tuple[str, ...] = STOCK_BENCH_ENGINES,
                              flush_to_zero: bool = False) -> str:
    """Config for a multi-engine measurement firmware: the AUX cycle readout, but
    NOT the LED meter -- the normal display has to keep showing which engine is
    selected so a sweep through them can be identified. The two readout channels
    are independent defines, and this build is the mirror image of a
    contributor's (LEDs, no tone)."""
    selected = []
    for identifier in engine_ids:
        entry, _ = builtin_engine(identifier)
        selected.append(entry)

    includes = "\n".join(
        sorted({f'#include "{item["source"]["header"]}"' for item in selected})
    )
    continuation = " " + "\\" + "\n  "
    members = continuation.join(
        f'{item["source"]["className"]} {item["source"]["member"]};' for item in selected
    )
    registrations = continuation.join(
        "(registry).RegisterInstance(&{member}, {enveloped}, {out_gain}, {aux_gain});".format(
            member=item["source"]["member"],
            enveloped=cpp_bool(item["postProcessing"]["alreadyEnveloped"]),
            out_gain=cpp_float(item["postProcessing"]["outGain"]),
            aux_gain=cpp_float(item["postProcessing"]["auxGain"]),
        )
        for item in selected
    )
    ftz = 1 if flush_to_zero else 0
    count = len(selected)
    # Whole banks of eight, so the model knob walks them in a predictable order
    # and the bank colour tells you which half you are in.
    full, remainder = divmod(count, 8)
    bank_sizes = [8] * full + ([remainder] if remainder else [])
    while len(bank_sizes) < 3:
        bank_sizes.append(0)
    rows = []
    for index in range(count):
        rows.append(index % 8)
    return f"""// Generated by Plaits Lab: multi-engine CPU bench firmware (UNREVIEWED).
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

{includes}

#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 0
#define PLAITS_CPU_PROBE_AUX 1
#define PLAITS_CPU_PROBE_FTZ {ftz}
#define PLAITS_ENGINE_COUNT {count}
#define PLAITS_BANK_SIZES {{ {", ".join(str(v) for v in bank_sizes)} }}
#define PLAITS_ENGINE_ROWS {{ {", ".join(str(v) for v in rows)} }}
#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_ENGINE_MEMBERS \\
  {members}
#define PLAITS_REGISTER_ENGINES(registry) do {{ \\
  {registrations} \\
}} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
"""


def render_local_hardware_config(
    package: dict[str, Any], cpu_probe: bool = False, memhunt: bool = False,
    cpu_probe_aux: bool = False,
) -> str:
    manifest = package["manifest"]
    source = manifest["source"]
    post = manifest["postProcessing"]
    # A probe build measures Voice::Render with the Cortex-M4 cycle counter; see
    # plaits/cpu_probe.h. Emitted through the generated config rather than as a
    # make flag so it travels into the containerised build with everything else.
    #
    # The measurement has two independent readout channels, and a contributor
    # build takes only the LED meter: it costs nothing they need (a one-model
    # firmware has nothing to select) and leaves their engine's AUX output
    # audible. The AUX tone is the precise readout and the internal one -- it
    # overwrites that output -- so it is opt-in, and memhunt, whose readout IS
    # the tone, turns it on.
    probe = cpu_probe or cpu_probe_aux
    tone = cpu_probe_aux or (probe and memhunt)
    cpu_probe_define = ""
    if probe:
        cpu_probe_define = "#define PLAITS_CPU_PROBE 1\n"
        cpu_probe_define += f"#define PLAITS_CPU_PROBE_AUX {1 if tone else 0}\n"
        if memhunt:
            cpu_probe_define += "#define PLAITS_CPU_PROBE_MEMHUNT 1\n"
    custom = {
        "source": {
            "header": package["header"].name,
            "className": source["className"],
            "member": f"community_{manifest['catalogId'].replace('-', '_')}_engine_",
        },
        "postProcessing": post,
    }
    # The local hardware build carries the contributor's engine ALONE. A full
    # 24-model palette already sits at the 224 KB flash ceiling, so a heavy engine
    # has no room; registering only this one lets the linker's --gc-sections drop
    # every stock engine and hand almost all of flash to the contributor. (The
    # hosted builder is where a full palette is arranged; the local build's job is
    # to prove THIS engine on hardware.)
    selected = [custom]
    unique: list[dict[str, Any]] = []
    seen: set[str] = set()
    for item in selected:
        member = item["source"]["member"]
        if member not in seen:
            seen.add(member)
            unique.append(item)
    includes = "\n".join(f'#include "{item["source"]["header"]}"' for item in unique)
    continuation = " " + "\\" + "\n  "
    members = continuation.join(
        f'{item["source"]["className"]} {item["source"]["member"]};'
        for item in unique
    )
    registrations = continuation.join(
        "(registry).RegisterInstance(&{member}, {enveloped}, {out_gain}, {aux_gain});".format(
            member=item["source"]["member"],
            enveloped=cpp_bool(item["postProcessing"]["alreadyEnveloped"]),
            out_gain=cpp_float(item["postProcessing"]["outGain"]),
            aux_gain=cpp_float(item["postProcessing"]["auxGain"]),
        )
        for item in selected
    )
    user_data = [item["source"].get("userDataBank", -1) for item in selected]
    speech_mask = sum(1 << index for index, item in enumerate(selected) if item["source"].get("behavior") == "speech")
    chiptune_mask = sum(1 << index for index, item in enumerate(selected) if item["source"].get("behavior") == "chiptune")
    # The firmware sizes its navigation from these — WITHOUT them a 1-engine build
    # keeps the default 24-slot / 3-bank layout (build_config.h) and exposes 23
    # null engines. Keep the 3-bank layout (a <3-bank PLAITS_BANK_SIZES array is
    # untested — bank_navigation.h only tests 3-4 banks): the engine sits in bank 0
    # (green), banks red/amber empty — the tested single-populated-bank pattern
    # ({N,0,0}). ENGINE_ROWS is one row per engine (bank 0, front to back).
    engine_count = len(selected)  # the local build carries the contributor's engine(s) alone
    bank_sizes = [engine_count, 0, 0]
    engine_rows = list(range(engine_count))
    return f"""// Generated by Plaits Lab for local, unreviewed hardware testing.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

{includes}

{cpu_probe_define}#define PLAITS_ENGINE_COUNT {engine_count}
#define PLAITS_BANK_SIZES {{ {", ".join(str(size) for size in bank_sizes)} }}
#define PLAITS_ENGINE_ROWS {{ {", ".join(str(row) for row in engine_rows)} }}
#define PLAITS_HAS_SPEECH_ENGINE {1 if speech_mask else 0}
#define PLAITS_HAS_CHIPTUNE_ENGINE {1 if chiptune_mask else 0}
#define PLAITS_HAS_USER_DATA_BANK {1 if any(value >= 0 for value in user_data) else 0}
#define PLAITS_ENGINE_MEMBERS \\
  {members}
#define PLAITS_REGISTER_ENGINES(registry) do {{ \\
  {registrations} \\
}} while (0)

namespace plaits {{
#if PLAITS_HAS_USER_DATA_BANK
static const int8_t kEngineUserDataBank[{engine_count}] = {{ {", ".join(str(value) for value in user_data)} }};
#endif
#if PLAITS_HAS_SPEECH_ENGINE
static const uint32_t kSpeechEngineMask = 0x{speech_mask:08x};
#endif
#if PLAITS_HAS_CHIPTUNE_ENGINE
static const uint32_t kChiptuneEngineMask = 0x{chiptune_mask:08x};
#endif
}}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
"""


def arm_compile_check(package: dict[str, Any], args: argparse.Namespace) -> None:
    """Compile the package's own source against the pinned ARM (hardware) toolchain
    to catch errors the host `check` compile can't — the host compiler has C99
    std:: math (std::log2/exp2, ...) that the ARM newlib doesn't, template quirks
    differ, etc. Objects only (no link, no firmware image), so it's fast. Uses a
    local ARM toolchain if present, otherwise the builder Docker image."""
    if package["manifest"]["packageType"] != "community":
        return  # reference packages already build portably on the ARM toolchain
    toolchain = Path(args.toolchain).resolve()
    compiler = toolchain / "bin/arm-none-eabi-g++"
    if compiler.is_file():
        _arm_compile_native(package, args, toolchain)
        return
    if args.native:
        raise PackageError(
            f"ARM toolchain not found at {compiler}; run inside the Plaits devcontainer or pass --toolchain"
        )
    docker = shutil.which("docker")
    if not docker:
        raise PackageError(
            "check --arm needs the ARM 4.8.3 toolchain or Docker + the builder image. "
            "Install Docker Desktop (https://docs.docker.com/get-docker/) and build the image "
            "once (see the hardware-build step), or pass --toolchain to a local toolchain."
        )
    # Same read-only-mount resources.cc stamp as the hardware build (see there).
    resources_cc = package["repo_root"] / "plaits" / "resources.cc"
    if resources_cc.is_file():
        try:
            resources_cc.touch()
        except OSError:
            pass
    command = sdk_docker_command(
        docker, args.docker_image, package,
        ["check", "/contributor", "--arm", "--native", "--no-compile",
         "--toolchain", args.toolchain],
    )
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        details = (result.stdout + result.stderr)[-8000:]
        raise PackageError(
            f"ARM compilation failed. If the {args.docker_image} image is missing, build it "
            f"once first (see the hardware-build step):\n"
            f"  git submodule update --init stmlib stm_audio_bootloader\n"
            f"  docker build --platform linux/amd64 -t {args.docker_image} -f Dockerfile.plaits-builder .\n"
            f"{details}"
        )
    # The size is measured inside the container (that's where the toolchain is);
    # surface it here, or a Docker-path check --arm would swallow it.
    for line in result.stdout.splitlines():
        if "model size" in line:
            print(line)


def _arm_compile_native(package: dict[str, Any], args: argparse.Namespace, toolchain: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="plaits-lab-armcheck-") as temp_dir:
        build_root = Path(temp_dir) / "build"
        config = Path(temp_dir) / "engine_config.h"
        config.write_text(
            render_stock_bench_config(flush_to_zero=getattr(args, "ftz", False))
            if getattr(args, "stock_bench", False)
            else render_local_hardware_config(
                package,
                cpu_probe=getattr(args, "cpu_probe", False),
                memhunt=getattr(args, "memhunt", False),
                cpu_probe_aux=getattr(args, "cpu_probe_aux", False)),
            encoding="utf-8")
        cppflags = f"-fno-exceptions -fno-rtti -I{package['source_root']} -include {config}"
        # Build only the package's OWN object(s): the makefile's $(BUILD_DIR)%.o
        # rule resolves each source through VPATH=$(PACKAGES) (which includes
        # PLAITS_EXTRA_PACKAGES). No link, no other firmware objects.
        objects = [str(build_root / "plaits" / f"{source.stem}.o") for source in package["source_files"]]
        command = [
            "make", "-f", "plaits/makefile", f"BUILD_ROOT={build_root}/",
            f"TOOLCHAIN_PATH={toolchain}/", f"PLAITS_EXTRA_PACKAGES={package['source_root']}",
            f"CPPFLAGS={cppflags}", "-j2", *objects,
        ]
        result = subprocess.run(command, cwd=package["repo_root"], text=True, capture_output=True, check=False)
        if result.returncode:
            raise PackageError(f"ARM compilation failed\n{(result.stdout + result.stderr)[-8000:]}")
        flash = arm_flash_footprint(toolchain / "bin/arm-none-eabi-size", objects)
        if flash is not None:
            # The engine's own code size — how heavy the model is. For scale, the
            # stock engines span roughly 2 KB (Virtual Analog) to 23 KB (Speech);
            # the local build carries your engine alone, so nearly all 224 KB is
            # available minus the base firmware.
            print(f"  model size: {flash:,} bytes of flash")


def hardware_build_command(args: argparse.Namespace) -> int:
    package = load_package(args.package)
    if package["manifest"]["packageType"] != "community":
        raise PackageError("fork the built-in package before making an unreviewed hardware build")
    toolchain = Path(args.toolchain).resolve()
    compiler = toolchain / "bin/arm-none-eabi-g++"
    if not compiler.is_file():
        if not args.native:
            docker = shutil.which("docker")
            if not docker:
                raise PackageError(
                    "the hardware build needs either the ARM 4.8.3 toolchain or Docker, "
                    "and neither was found. Install Docker Desktop "
                    "(https://docs.docker.com/get-docker/) — or the ARM toolchain — then re-run."
                )
            output = Path(args.output).resolve()
            output.parent.mkdir(parents=True, exist_ok=True)
            # plaits/resources.cc is checked in, but its make rule regenerates it
            # whenever a resources/*.py prerequisite is newer (stmlib/makefile.inc),
            # writing bank_*.raw into the working tree. The builder image stamps it
            # newest so make never fires that rule — but we mount the host checkout
            # read-only OVER that stamp, and a fresh git checkout has arbitrary
            # mtimes, so make tries to regenerate it and dies on the read-only
            # mount. Re-stamp the host copy (mtime only, content untouched) so the
            # mounted view is up to date, exactly as the image does.
            resources_cc = package["repo_root"] / "plaits" / "resources.cc"
            if resources_cc.is_file():
                try:
                    resources_cc.touch()
                except OSError:
                    pass
            command = [
                docker, "run", "--rm", "--platform", "linux/amd64",
                "--entrypoint", "python3",
                "-v", f"{package['repo_root']}:/workspace:ro",
                "-v", f"{package['directory']}:/contributor:ro",
                "-v", f"{output.parent}:/output",
                "-w", "/workspace", args.docker_image,
                "alt_firmwares/plaits_lab_sdk/plaits_lab.py", "build", "/contributor",
                "--hardware", "--output", f"/output/{output.name}",
                *(["--cpu-probe"] if getattr(args, "cpu_probe", False) else []),
                *(["--cpu-probe-aux"] if getattr(args, "cpu_probe_aux", False) else []),
                *(["--stock-bench"] if getattr(args, "stock_bench", False) else []),
                *(["--ftz"] if getattr(args, "ftz", False) else []),
                *(["--memhunt"] if getattr(args, "memhunt", False) else []),
                "--toolchain", args.toolchain, "--native",
            ]
            result = subprocess.run(command, text=True, capture_output=True, check=False)
            if result.returncode:
                details = (result.stdout + result.stderr)[-8000:]
                raise PackageError(
                    f"containerized ARM build failed. If the {args.docker_image} image is "
                    f"missing, build it once first (the hardware build also needs the "
                    f"stm_audio_bootloader submodule the base setup skips):\n"
                    f"  git submodule update --init stmlib stm_audio_bootloader\n"
                    f"  docker build --platform linux/amd64 -t {args.docker_image} "
                    f"-f Dockerfile.plaits-builder .\n"
                    f"then re-run this command.\n{details}"
                )
            # The container wrote the WAV to /output/<name>, which is the mount of
            # the host's output dir — rewrite that container path back to the real
            # host path so the message points at a file the user can actually find.
            print(result.stdout.strip().replace(f"/output/{output.name}", str(output)))
            return 0
        raise PackageError(
            f"ARM toolchain not found at {compiler}; run inside the Plaits devcontainer or pass --toolchain"
        )
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="plaits-lab-hardware-") as temp_dir:
        build_root = Path(temp_dir) / "build"
        config = Path(temp_dir) / "engine_config.h"
        config.write_text(
            render_stock_bench_config() if getattr(args, "stock_bench", False)
            else render_local_hardware_config(
                package,
                cpu_probe=getattr(args, "cpu_probe", False),
                memhunt=getattr(args, "memhunt", False),
                cpu_probe_aux=getattr(args, "cpu_probe_aux", False)),
            encoding="utf-8")
        cppflags = f"-fno-exceptions -fno-rtti -I{package['source_root']} -include {config}"
        command = [
            "make", "-f", "plaits/makefile", f"BUILD_ROOT={build_root}/",
            f"TOOLCHAIN_PATH={toolchain}/", f"PLAITS_EXTRA_PACKAGES={package['source_root']}",
            f"CPPFLAGS={cppflags}", "-j2", "wav",
        ]
        result = subprocess.run(command, cwd=package["repo_root"], text=True, capture_output=True, check=False)
        if result.returncode:
            raise PackageError(f"ARM firmware build failed\n{(result.stdout + result.stderr)[-8000:]}")
        wav = build_root / "plaits/plaits.wav"
        elf = build_root / "plaits/plaits.elf"
        if not wav.is_file():
            raise PackageError("ARM build did not produce an audio firmware updater")
        shutil.copyfile(wav, output)
        size_tool = toolchain / "bin/arm-none-eabi-size"
        flash = arm_flash_footprint(size_tool, [str(elf)]) if elf.is_file() else None
        # The engine's own object(s) are in the build tree — measure them for the
        # model's code size, same as `check --arm` reports.
        model_objects = [str(build_root / "plaits" / f"{source.stem}.o") for source in package["source_files"]]
        model_flash = arm_flash_footprint(size_tool, model_objects)
    print(f"built UNREVIEWED local firmware {output}")
    if model_flash is not None:
        print(f"model size: {model_flash:,} bytes of flash (your engine's own code)")
    if flash is not None:
        free = PLAITS_FLASH_BYTES - flash
        pct = 100.0 * flash / PLAITS_FLASH_BYTES
        print(f"flash: {flash:,} / {PLAITS_FLASH_BYTES:,} bytes ({pct:.0f}% used, {free:,} free) "
              f"— your engine alone, so this is its room to grow")
    print("Install only on hardware you control; this package has not passed publication review.")
    return 0


class DevSession:
    def __init__(self, package_arg: str, compiler: str | None) -> None:
        self.package_arg = package_arg
        self.compiler = compiler
        self.temp_dir = tempfile.TemporaryDirectory(prefix="plaits-lab-dev-")
        self.renderer = host_binary(Path(self.temp_dir.name), "render-model")
        self.wasm = Path(self.temp_dir.name) / "audition.wasm"
        self.wasm_available = False
        self.fingerprint = ""
        self.reference_renderers: dict[str, Path] = {}

    def close(self) -> None:
        self.temp_dir.cleanup()

    def package(self) -> dict[str, Any]:
        package = load_package(self.package_arg, autodeclare=True)
        report_autodeclared(package)
        return package

    def source_fingerprint(self, package: dict[str, Any]) -> str:
        import hashlib

        digest = hashlib.sha256()
        for path in [package["header"], *package["source_files"]]:
            digest.update(path.read_bytes())
        return digest.hexdigest()

    def ensure_renderer(self) -> tuple[dict[str, Any], bool]:
        package = self.package()
        fingerprint = self.source_fingerprint(package)
        recompiled = fingerprint != self.fingerprint or not self.renderer.is_file()
        if recompiled:
            compile_renderer(package, self.renderer, self.compiler)
            self.fingerprint = fingerprint
            if wasm_compiler_path() is not None:
                try:
                    compile_wasm(package, self.wasm)
                    self.wasm_available = True
                except PackageError as error:
                    self.wasm_available = False
                    print(f"live audition unavailable (wasm build failed): {error}")
        return package, recompiled

    def render(self, request: dict[str, Any]) -> tuple[bytes, bool]:
        package, recompiled = self.ensure_renderer()
        return self.render_package(package, self.renderer, request, "interactive.wav"), recompiled

    def render_reference(self, engine_id: str, request: dict[str, Any]) -> bytes:
        package = builtin_package(engine_id)
        renderer = self.reference_renderers.get(engine_id)
        if renderer is None:
            renderer = host_binary(Path(self.temp_dir.name), f"reference-{engine_id}")
            compile_renderer(package, renderer, self.compiler)
            self.reference_renderers[engine_id] = renderer
        return self.render_package(package, renderer, request, f"reference-{engine_id}.wav")

    def render_package(
        self, package: dict[str, Any], renderer: Path, request: dict[str, Any], filename: str,
    ) -> bytes:
        scenario = {
            "durationSeconds": request.get("durationSeconds", 2),
            "note": request.get("note", 48),
            "triggerHz": request.get("triggerHz", 0),
            "controls": request.get("controls", {}),
        }
        scenario["id"] = "interactive"
        scenario["name"] = "Interactive preview"
        validate_scenario(scenario, 0)
        output = Path(self.temp_dir.name) / filename
        run_scenario(package, renderer, scenario, output)
        return output.read_bytes()


def dev_command(args: argparse.Namespace) -> int:
    session = DevSession(args.package, args.compiler)
    package, _ = session.ensure_renderer()
    # The dev server serves its own audition UI (dev_editor.html) at "/", so page
    # and API are always same-origin — no CORS, and no cross-origin caller to
    # allow. A same-origin POST still sends an Origin header, so the guard below
    # accepts exactly this server's own origins (and rejects anything else, which
    # is what keeps another page in the browser from driving it).
    allowed_origins = {
        f"http://{args.host}:{args.port}",
        f"http://127.0.0.1:{args.port}",
        f"http://localhost:{args.port}",
    }

    class Handler(BaseHTTPRequestHandler):
        server_version = "PlaitsLabSDK/0"

        def common_headers(self) -> None:
            self.send_header("Cache-Control", "no-store")

        def origin_allowed(self) -> bool:
            origin = self.headers.get("Origin")
            if origin is None or origin in allowed_origins:
                return True
            self.send_json({"error": "origin not allowed"}, HTTPStatus.FORBIDDEN)
            return False

        def send_json(self, value: Any, status: HTTPStatus = HTTPStatus.OK) -> None:
            body = (json.dumps(value) + "\n").encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.common_headers()
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:  # noqa: N802
            if not self.origin_allowed():
                return
            request_path = urlparse(self.path).path
            if request_path in ("/", "/index.html"):
                html = Path(__file__).with_name("dev_editor.html").read_bytes()
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(html)))
                self.common_headers()
                self.end_headers()
                self.wfile.write(html)
                return
            if request_path == "/audition_worklet.js":
                js = Path(__file__).with_name("audition_worklet.js").read_bytes()
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/javascript; charset=utf-8")
                self.send_header("Content-Length", str(len(js)))
                self.common_headers()
                self.end_headers()
                self.wfile.write(js)
                return
            if request_path == "/v1/audition.wasm":
                if session.wasm_available and session.wasm.is_file():
                    data = session.wasm.read_bytes()
                    self.send_response(HTTPStatus.OK)
                    self.send_header("Content-Type", "application/wasm")
                    self.send_header("Content-Length", str(len(data)))
                    self.common_headers()
                    self.end_headers()
                    self.wfile.write(data)
                else:
                    self.send_json({"error": "live audition unavailable"}, HTTPStatus.NOT_FOUND)
                return
            if request_path == "/v1/catalog":
                catalog, _ = load_builtin_catalog()
                self.send_json({"engines": [
                    {"id": engine_id, "name": engine["name"]}
                    for engine_id, engine in catalog.items()
                ]})
                return
            if request_path == "/v1/package":
                try:
                    current, recompiled = session.ensure_renderer()
                    self.send_json({
                        "manifest": current["manifest"],
                        "scenarios": current["scenarios"],
                        "digest": package_content_digest(current["directory"]),
                        "sourceRevision": session.fingerprint,
                        "recompiled": recompiled,
                        "live": session.wasm_available,
                        "checks": ["package", "license", "source-policy", "host-compile"],
                    })
                except PackageError as error:
                    self.send_json({"error": str(error)}, HTTPStatus.UNPROCESSABLE_ENTITY)
                return
            self.send_json({"error": "not found"}, HTTPStatus.NOT_FOUND)

        def do_POST(self) -> None:  # noqa: N802
            if not self.origin_allowed():
                return
            path = urlparse(self.path).path
            reference_match = re.fullmatch(r"/v1/reference/([a-z0-9-]+)/render", path)
            if path != "/v1/render" and not reference_match:
                self.send_json({"error": "not found"}, HTTPStatus.NOT_FOUND)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0 or length > 16 * 1024:
                    raise PackageError("interactive render request is too large")
                request = json.loads(self.rfile.read(length))
                if not isinstance(request, dict):
                    raise PackageError("interactive render request must be an object")
                if reference_match:
                    wav = session.render_reference(reference_match.group(1), request)
                    recompiled = False
                else:
                    wav, recompiled = session.render(request)
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "audio/wav")
                self.send_header("Content-Length", str(len(wav)))
                self.send_header("X-Plaits-Recompiled", "true" if recompiled else "false")
                self.common_headers()
                self.end_headers()
                self.wfile.write(wav)
            except (PackageError, json.JSONDecodeError) as error:
                self.send_json({"error": str(error)}, HTTPStatus.UNPROCESSABLE_ENTITY)

        def log_message(self, format: str, *values: object) -> None:
            if args.verbose:
                print(f"dev: {format % values}")

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server_url = f"http://{args.host}:{args.port}"
    print(f"serving {package['manifest']['id']} from {server_url}")
    print(f"open {server_url}/  — audition it in your browser (nothing else to set up)")
    print("source changes are revalidated and recompiled on the next preview")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping Plaits Lab dev session")
    finally:
        server.server_close()
        session.close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="plaits-lab", description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    catalog_parser = subparsers.add_parser("catalog", help="list forkable built-in packages")
    catalog_parser.set_defaults(handler=catalog_command)

    modules_parser = subparsers.add_parser("modules", help="list shared library modules a package may declare")
    modules_parser.set_defaults(handler=modules_command)

    init_parser = subparsers.add_parser("init", help="create a blank package or fork a built-in model")
    init_parser.add_argument("output")
    init_parser.add_argument("--from", dest="from_engine", default="blank")
    init_parser.add_argument("--author", default="Contributor")
    init_parser.add_argument("--license", default=DEFAULT_LICENSE, choices=sorted(ALLOWED_LICENSES),
                             help=f"license for the package (default {DEFAULT_LICENSE}); "
                                  "a fork must keep its upstream license")
    init_parser.add_argument("--package-id")
    init_parser.add_argument("--slug")
    init_parser.add_argument("--name")
    init_parser.add_argument("--color", help="the model's colour in the palette editor, #RRGGBB")
    init_parser.set_defaults(handler=init_command)

    check_parser = subparsers.add_parser("check", help="validate and compile an engine package")
    check_parser.add_argument("package")
    check_parser.add_argument("--compiler")
    check_parser.add_argument("--no-compile", action="store_true")
    check_parser.add_argument("--full", action="store_true", help="run sanitizers and every audio scenario")
    check_parser.add_argument("--arm", action="store_true",
                              help="also compile against the pinned ARM (hardware) toolchain")
    check_parser.add_argument("--toolchain", default="/usr/local/arm-4.8.3")
    check_parser.add_argument("--docker-image", default="plaits-lab-builder:local")
    check_parser.add_argument("--native", action="store_true", help=argparse.SUPPRESS)
    check_parser.set_defaults(handler=check_command)

    render_parser = subparsers.add_parser("render", help="render a declared preview scenario")
    render_parser.add_argument("package")
    render_parser.add_argument("--scenario", default="hero")
    render_parser.add_argument("--output", required=True)
    render_parser.add_argument("--compiler")
    render_parser.set_defaults(handler=render_command)

    submit_parser = subparsers.add_parser(
        "submit", help="check, bundle, and submit the package for review")
    submit_parser.add_argument("package")
    submit_parser.add_argument("--output", help="where to write the bundle "
                               "(default: <catalogId>.plaits-package.zip)")
    submit_parser.add_argument("--compiler")
    submit_parser.add_argument("--docker-image", default="plaits-lab-builder:local")
    submit_parser.add_argument("--bundle-only", action="store_true",
                               help="build the bundle without submitting (to inspect it, or for CI)")
    submit_parser.add_argument("--author", help="the rights holder to record on the affirmation")
    submit_parser.add_argument("--contact", help="optional contact for questions about the model")
    submit_parser.add_argument("--yes", action="store_true",
                               help="skip the typed confirmation (requires --author)")
    submit_parser.add_argument("--api", help=argparse.SUPPRESS)
    submit_parser.add_argument("--native", action="store_true", help=argparse.SUPPRESS)
    submit_parser.set_defaults(handler=submit_command)

    login_parser = subparsers.add_parser(
        "login", help="submit as an existing contributor identity (e.g. on a second machine)")
    login_parser.add_argument("--token")
    login_parser.set_defaults(handler=login_command)

    whoami_parser = subparsers.add_parser(
        "whoami", help="show which contributor identity this machine submits as")
    whoami_parser.add_argument("--show", action="store_true", help="print the token itself")
    whoami_parser.set_defaults(handler=whoami_command)

    build_parser_command = subparsers.add_parser("build", help="build an unreviewed local hardware firmware")
    build_parser_command.add_argument("package")
    build_parser_command.add_argument("--hardware", action="store_true", required=True)
    build_parser_command.add_argument("--ftz", action="store_true",
        help="probe builds: enable FPU flush-to-zero (denormal test)")
    build_parser_command.add_argument("--stock-bench", action="store_true",
        help="build a multi-engine bench firmware (stock engines + AUX cycle readout)")
    build_parser_command.add_argument("--memhunt", action="store_true",
        help="probe builds: readout carries only the watched address (writer hunt); implies --cpu-probe-aux")
    build_parser_command.add_argument("--cpu-probe", action="store_true",
        help="measure Voice::Render on-chip with the DWT cycle counter and meter it on the LEDs")
    build_parser_command.add_argument("--cpu-probe-aux", action="store_true",
        help="probe builds: also read out on AUX as a tone -- precise, but it takes over the AUX output")
    build_parser_command.add_argument("--output", required=True)
    build_parser_command.add_argument("--toolchain", default="/usr/local/arm-4.8.3")
    build_parser_command.add_argument("--docker-image", default="plaits-lab-builder:local")
    build_parser_command.add_argument("--native", action="store_true", help=argparse.SUPPRESS)
    build_parser_command.set_defaults(handler=hardware_build_command)

    dev_parser = subparsers.add_parser("dev", help="serve a hot-reloading local model to the contributor UI")
    dev_parser.add_argument("package")
    dev_parser.add_argument("--host", default="127.0.0.1")
    dev_parser.add_argument("--port", type=int, default=4179)
    dev_parser.add_argument("--compiler")
    dev_parser.add_argument("--verbose", action="store_true")
    dev_parser.set_defaults(handler=dev_command)
    return parser


def use_utf8_output() -> None:
    """Make stdout/stderr UTF-8 regardless of the platform's locale.

    This CLI prints ✓ and em dashes. On Windows, Python only uses the console's
    UTF-8 path while stdout is a terminal — the moment it is REDIRECTED (a pipe,
    `> build.log`, a CI capture) it falls back to the locale encoding, cp1252,
    where those characters do not exist, and every command dies with
    UnicodeEncodeError before doing any work. Reconfiguring costs nothing on
    macOS/Linux, which are already UTF-8. Done here rather than at import so
    merely importing the module never mutates global stream state (the tests do).
    """
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            try:
                reconfigure(encoding="utf-8", errors="replace")
            except (ValueError, OSError):
                pass  # already detached or not reconfigurable; not worth failing over


def main(argv: list[str] | None = None) -> int:
    use_utf8_output()
    try:
        args = build_parser().parse_args(argv)
        return args.handler(args)
    except PackageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
