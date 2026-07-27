#!/usr/bin/env python3
"""Local validation and preview renderer for Plaits Lab engine packages."""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import wave
import zipfile
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse
from pathlib import Path
from typing import Any


SDK_VERSION = "plaits-engine-cpp-v1"
SDK_DIR = Path(__file__).resolve().parent
REPO_ROOT = SDK_DIR.parents[1]
CATALOG_PATH = SDK_DIR.parent / "plaits_lab_catalog/catalog.json"
PUBLIC_CATALOG_PATH = SDK_DIR.parent / "plaits_lab_catalog/public_catalog.json"
SHARED_MODULES_PATH = SDK_DIR.parent / "plaits_lab_catalog/shared_modules.json"
ALLOWED_LICENSES = {"MIT", "BSD-2-Clause", "BSD-3-Clause", "ISC"}
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
        raise PackageError(f"unknown built-in model {identifier!r}; run `plaits-lab catalog`")
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
                f"unknown shared module {module_id!r}; run `plaits-lab modules`")
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
    optional = {"upstream", "forkedFrom", "sharedModules"}
    require(required <= set(manifest), f"manifest is missing {sorted(required - set(manifest))}")
    require(set(manifest) <= required | optional,
            f"manifest has unsupported fields {sorted(set(manifest) - required - optional)}")
    require(manifest["schemaVersion"] == 1, "schemaVersion must be 1")
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
        "-std=c++11", "-DTEST", "-O2",
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

    package_binary = temp_dir / "cpu-bench-package"
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
    reference_binary = temp_dir / "cpu-bench-reference"
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
            "  Heavier than any stock engine. It may still fit, but nothing on the host\n"
            "  can prove that — verify on hardware, and check for per-sample work that\n"
            "  could be hoisted to per-block."
        )
    else:
        print(f"✓ CPU cost: {detail}")


def compile_renderer(
    package: dict[str, Any], output: Path, requested_compiler: str | None,
    sanitizers: bool = False,
) -> None:
    manifest = package["manifest"]
    compiled = engine_translation_units(package, Path(__file__).with_name("render_model.cc"))
    command = [
        compiler_path(requested_compiler),
        "-std=c++11", "-DTEST", "-O2", "-Wall", "-Werror",
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
        command[4:4] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        details = (result.stderr or result.stdout).strip()
        raise PackageError(f"host compilation failed\n{details}")


def wasm_compiler_path() -> str | None:
    return shutil.which("emcc")


# Emscripten exports the audition harness surface the AudioWorklet drives.
WASM_EXPORTS = ('["_init","_render","_set_params","_trigger","_set_env_mode",'
               '"_set_stereo","_stereo_capable","_main_out","_aux_out"]')


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
        "-std=c++11", "-DTEST", "-O2",
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


def mit_license(author: str) -> str:
    return f"""MIT License

Copyright (c) 2026 {author}

Permission is hereby granted, free of charge, to any person obtaining a copy
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


def blank_source(slug: str, class_name: str) -> tuple[str, str]:
    guard = f"PLAITS_LAB_{slug.replace('-', '_').upper()}_ENGINE_H_"
    header = f"""// Copyright 2026 Contributor.
// SPDX-License-Identifier: MIT

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
    implementation = f"""// Copyright 2026 Contributor.
// SPDX-License-Identifier: MIT

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


def init_command(args: argparse.Namespace) -> int:
    output = Path(args.output).resolve()
    require(not output.exists(), f"output already exists: {output}")
    slug = args.slug or output.name.lower().replace("_", "-")
    require(CATALOG_ID_PATTERN.fullmatch(slug) is not None, "package slug must be lowercase words joined by hyphens")
    package_id = args.package_id or f"community/{slug}"
    require(ID_PATTERN.fullmatch(package_id) is not None, "package ID must use namespace/slug form")
    class_name = slug_to_class(slug)
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
        header, implementation = blank_source(slug, class_name)
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
        "license": "MIT",
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
    (output / "LICENSE").write_text(mit_license(args.author), encoding="utf-8")
    (output / "README.md").write_text(
        f"# {name}\n\nA Plaits Lab community engine package.\n\n"
        f"Run `plaits-lab check .` and `plaits-lab render . --output preview.wav` from this directory.\n",
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
    print(f"✓ package {package['manifest']['id']}@{package['manifest']['version']}")
    print("✓ metadata, license, source policy, and scenarios")
    cpu_cost: dict[str, Any] | None = None
    if not args.no_compile:
        with tempfile.TemporaryDirectory(prefix="plaits-lab-check-") as temp_dir:
            renderer = Path(temp_dir) / "render-model"
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
) -> float:
    controls = scenario["controls"]
    post = package["manifest"]["postProcessing"]
    command = [
        str(renderer), str(output), str(scenario["durationSeconds"]), str(scenario["note"]),
        str(controls["harmonics"][0]), str(controls["harmonics"][1]),
        str(controls["timbre"][0]), str(controls["timbre"][1]),
        str(controls["morph"][0]), str(controls["morph"][1]),
        str(controls["macro"][0]), str(controls["macro"][1]),
        str(scenario["triggerHz"]), str(post["outGain"]), str(post["auxGain"]),
    ]
    started = time.monotonic()
    result = subprocess.run(command, text=True, capture_output=True, check=False)
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
        renderer = Path(temp_dir) / "render-model"
        compile_renderer(package, renderer, args.compiler)
        elapsed = run_scenario(package, renderer, scenario, output)
        metrics = analyze_wav(output, scenario["durationSeconds"], elapsed)
        print(f"rendered {output} (peak {metrics['peak']:.4f}, RMS {metrics['rms']:.4f})")
    return 0


def package_content_digest(package_dir: Path) -> str:
    import hashlib

    digest = hashlib.sha256()
    files = sorted(
        path for path in package_dir.rglob("*")
        if path.is_file() and ".plaits-lab" not in path.parts
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


def submit_command(args: argparse.Namespace) -> int:
    package = load_package(args.package)
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="plaits-lab-submit-") as temp_dir:
        preview_dir = Path(temp_dir) / "previews"
        preview_dir.mkdir()
        renderer = Path(temp_dir) / "render-model"
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
            for path in sorted(item for item in package["directory"].rglob("*") if item.is_file()):
                add_zip_file(archive, f"package/{path.relative_to(package['directory']).as_posix()}", path.read_bytes())
            for path in sorted(preview_dir.glob("*.wav")):
                add_zip_file(archive, f"previews/{path.name}", path.read_bytes())
            add_zip_file(archive, "submission.json", (json.dumps(submission, indent=2) + "\n").encode("utf-8"))
    print(f"created draft submission {output}")
    print(f"package digest {submission['digest']}")
    return 0


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
    selected so a sweep through them can be identified."""
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
    package: dict[str, Any], cpu_probe: bool = False,
) -> str:
    manifest = package["manifest"]
    source = manifest["source"]
    post = manifest["postProcessing"]
    # A probe build measures Voice::Render with the Cortex-M4 cycle counter and
    # reports the result on AUX; see plaits/cpu_probe.h. Emitted through the
    # generated config rather than as a make flag so it travels into the
    # containerised build with everything else.
    cpu_probe_define = "#define PLAITS_CPU_PROBE 1\n" if cpu_probe else ""
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
    command = [
        docker, "run", "--rm", "--platform", "linux/amd64", "--entrypoint", "python3",
        "-v", f"{package['repo_root']}:/workspace:ro",
        "-v", f"{package['directory']}:/contributor:ro",
        "-w", "/workspace", args.docker_image,
        "alt_firmwares/plaits_lab_sdk/plaits_lab.py", "check", "/contributor",
        "--arm", "--native", "--no-compile", "--toolchain", args.toolchain,
    ]
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
            else render_local_hardware_config(package, cpu_probe=getattr(args, "cpu_probe", False)),
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
                *(["--stock-bench"] if getattr(args, "stock_bench", False) else []),
                *(["--ftz"] if getattr(args, "ftz", False) else []),
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
            else render_local_hardware_config(package, cpu_probe=getattr(args, "cpu_probe", False)),
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
        self.renderer = Path(self.temp_dir.name) / "render-model"
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
            renderer = Path(self.temp_dir.name) / f"reference-{engine_id}"
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
    init_parser.add_argument("--package-id")
    init_parser.add_argument("--slug")
    init_parser.add_argument("--name")
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

    submit_parser = subparsers.add_parser("submit", help="validate and bundle an immutable draft submission")
    submit_parser.add_argument("package")
    submit_parser.add_argument("--output", required=True)
    submit_parser.add_argument("--compiler")
    submit_parser.set_defaults(handler=submit_command)

    build_parser_command = subparsers.add_parser("build", help="build an unreviewed local hardware firmware")
    build_parser_command.add_argument("package")
    build_parser_command.add_argument("--hardware", action="store_true", required=True)
    build_parser_command.add_argument("--ftz", action="store_true",
        help="probe builds: enable FPU flush-to-zero (denormal test)")
    build_parser_command.add_argument("--stock-bench", action="store_true",
        help="build a multi-engine bench firmware (stock engines + AUX cycle readout)")
    build_parser_command.add_argument("--cpu-probe", action="store_true",
        help="measure Voice::Render on-chip with the DWT cycle counter and report on AUX")
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


def main(argv: list[str] | None = None) -> int:
    try:
        args = build_parser().parse_args(argv)
        return args.handler(args)
    except PackageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
