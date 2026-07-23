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
from urllib.parse import quote, urlparse
from pathlib import Path
from typing import Any


SDK_VERSION = "plaits-engine-cpp-v1"
DEV_EDITOR_DEFAULT = "http://localhost:3000"
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
    "algorithm", "cmath", "cstddef", "cstdint", "limits", "stdint.h",
}
FORBIDDEN_SOURCE_PATTERNS = {
    "inline assembly": re.compile(r"\b(?:asm|__asm__)\b"),
    "dynamic allocation": re.compile(
        r"\b(?:malloc|calloc|realloc|free)\s*\(|\bnew\s+[A-Za-z_]|\bdelete(?:\[\])?\s+[A-Za-z_]"
    ),
    "direct hardware access": re.compile(r"\b(?:HAL_|NVIC_|FLASH_|RCC_|GPIO|SysTick)"),
}


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
            require(pattern.search(policy_source) is None, f"{path.name} uses forbidden {description}")
        for match in re.finditer(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', source, re.MULTILINE):
            delimiter, include = match.groups()
            require(".." not in Path(include).parts, f"{path.name} include escapes the package: {include}")
            if delimiter == "<":
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


def compile_renderer(
    package: dict[str, Any], output: Path, requested_compiler: str | None,
    sanitizers: bool = False,
) -> None:
    manifest = package["manifest"]
    source = manifest["source"]
    header_include = (
        package["header"].name
        if manifest["packageType"] == "community"
        else package["header"].relative_to(package["repo_root"]).as_posix()
    )
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
    # Every translation unit linked into the renderer, de-duplicated by resolved
    # path so a shared module already reachable as a fork support file (or in the
    # always-linked base set) is never handed to the compiler twice.
    translation_units = [
        Path(__file__).with_name("render_model.cc"),
        *package["source_files"],
        *support_files,
        *shared_sources,
        package["repo_root"] / "plaits/resources.cc",
        package["repo_root"] / "stmlib/dsp/units.cc",
        package["repo_root"] / "stmlib/utils/random.cc",
    ]
    seen_units: set[str] = set()
    compiled: list[str] = []
    for unit in translation_units:
        key = str(unit.resolve())
        if key not in seen_units:
            seen_units.add(key)
            compiled.append(str(unit))
    command = [
        compiler_path(requested_compiler),
        "-std=c++11",
        "-DTEST",
        "-O2",
        "-Wall",
        "-Werror",
        "-Wno-unused-variable",
        "-Wno-unused-parameter",
        "-Wno-unused-local-typedefs",
        "-Wno-deprecated-declarations",
        f'-DPLAITS_LAB_ENGINE_HEADER="{header_include}"',
        f'-DPLAITS_LAB_ENGINE_CLASS=plaits::{source["className"]}',
        "-I",
        str(package["repo_root"]),
        "-I",
        str(package["source_root"]),
        *compiled,
        "-o",
        str(output),
    ]
    if sanitizers:
        command[4:4] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        details = (result.stderr or result.stdout).strip()
        raise PackageError(f"host compilation failed\n{details}")


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

#include <cmath>

namespace plaits {{

void {class_name}::Init(stmlib::BufferAllocator* allocator) {{ Reset(); }}
void {class_name}::Reset() {{ phase_ = 0.0f; }}

void {class_name}::Render(const EngineParameters& parameters, float* out,
    float* aux, size_t size, bool* already_enveloped) {{
  const float frequency = NoteToFrequency(parameters.note);
  for (size_t i = 0; i < size; ++i) {{
    phase_ += frequency;
    phase_ -= static_cast<int>(phase_);
    out[i] = 0.5f * std::sin(phase_ * 6.28318530718f);
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
        print("✓ host compilation")
        if args.full:
            print("✓ sanitizer execution and audio health")
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
    require(peak >= 0.001 and rms >= 0.0001, f"{path.name} is silent or effectively silent")
    require(peak <= 1.0, f"{path.name} contains invalid PCM amplitude")
    require(abs(dc_offset) <= 0.2, f"{path.name} has excessive DC offset ({dc_offset:.4f})")
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


def render_local_hardware_config(package: dict[str, Any]) -> str:
    catalog, _ = load_builtin_catalog()
    slots = list(read_json(SDK_DIR.parent / "plaits_lab_builder/default_recipe.json")["slots"])
    custom_id = "__local_contributor__"
    slots[16] = custom_id
    manifest = package["manifest"]
    source = manifest["source"]
    post = manifest["postProcessing"]
    custom = {
        "source": {
            "header": package["header"].name,
            "className": source["className"],
            "member": f"community_{manifest['catalogId'].replace('-', '_')}_engine_",
        },
        "postProcessing": post,
    }
    selected = [custom if engine_id == custom_id else catalog[engine_id] for engine_id in slots[16:24] + slots[0:16]]
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
    return f"""// Generated by Plaits Lab for local, unreviewed hardware testing.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

{includes}

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
static const int8_t kEngineUserDataBank[24] = {{ {", ".join(str(value) for value in user_data)} }};
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
                raise PackageError("ARM toolchain and Docker are both unavailable")
            output = Path(args.output).resolve()
            output.parent.mkdir(parents=True, exist_ok=True)
            command = [
                docker, "run", "--rm", "--platform", "linux/amd64",
                "--entrypoint", "python3",
                "-v", f"{package['repo_root']}:/workspace:ro",
                "-v", f"{package['directory']}:/contributor:ro",
                "-v", f"{output.parent}:/output",
                "-w", "/workspace", args.docker_image,
                "alt_firmwares/plaits_lab_sdk/plaits_lab.py", "build", "/contributor",
                "--hardware", "--output", f"/output/{output.name}",
                "--toolchain", args.toolchain, "--native",
            ]
            result = subprocess.run(command, text=True, capture_output=True, check=False)
            if result.returncode:
                details = (result.stdout + result.stderr)[-8000:]
                raise PackageError(
                    f"containerized ARM build failed; build {args.docker_image} with "
                    f"`docker build --platform linux/amd64 -t {args.docker_image} "
                    f"-f Dockerfile.plaits-builder .`\n{details}"
                )
            print(result.stdout.strip())
            return 0
        raise PackageError(
            f"ARM toolchain not found at {compiler}; run inside the Plaits devcontainer or pass --toolchain"
        )
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="plaits-lab-hardware-") as temp_dir:
        build_root = Path(temp_dir) / "build"
        config = Path(temp_dir) / "engine_config.h"
        config.write_text(render_local_hardware_config(package), encoding="utf-8")
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
        if elf.is_file() and size_tool.is_file():
            size_result = subprocess.run([str(size_tool), str(elf)], text=True, capture_output=True, check=False)
            size = size_result.stdout.strip().splitlines()[-1] if size_result.returncode == 0 else "size unavailable"
        else:
            size = "size unavailable"
    print(f"built UNREVIEWED local firmware {output}")
    print(f"ARM size: {size}")
    print("Install only on hardware you control; this package has not passed publication review.")
    return 0


class DevSession:
    def __init__(self, package_arg: str, compiler: str | None) -> None:
        self.package_arg = package_arg
        self.compiler = compiler
        self.temp_dir = tempfile.TemporaryDirectory(prefix="plaits-lab-dev-")
        self.renderer = Path(self.temp_dir.name) / "render-model"
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


def contributor_url_for(editor: str, server_url: str) -> str:
    """The contributor-center URL to open, preserving the editor's PATH.

    `--editor` is the full page URL (e.g. https://rubato.audio/plaits-palette/
    contribute); a bare origin falls back to the contributor route rather than
    the wrong top-level /contribute.
    """
    parsed = urlparse(editor)
    origin = f"{parsed.scheme}://{parsed.netloc}"
    path = parsed.path.rstrip("/") or "/plaits-palette/contribute"
    return f"{origin}{path}?devServer={quote(server_url, safe='')}"


def dev_command(args: argparse.Namespace) -> int:
    session = DevSession(args.package, args.compiler)
    package, _ = session.ensure_renderer()
    editor = urlparse(args.editor)
    require(editor.scheme in {"http", "https"} and bool(editor.netloc)
            and not editor.username and not editor.password,
            "--editor must be an HTTP(S) origin")
    editor_origin = f"{editor.scheme}://{editor.netloc}"
    # The dev server now serves its own audition UI (dev_editor.html) at "/", so
    # its OWN origin(s) must be allowed too — a same-origin POST still sends an
    # Origin header. --editor stays supported for driving the full website.
    allowed_origins = {
        editor_origin,
        f"http://{args.host}:{args.port}",
        f"http://127.0.0.1:{args.port}",
        f"http://localhost:{args.port}",
    }

    class Handler(BaseHTTPRequestHandler):
        server_version = "PlaitsLabSDK/0"

        def cors(self) -> None:
            self.send_header("Access-Control-Allow-Origin", editor_origin)
            self.send_header("Vary", "Origin")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            # Opt into Private Network Access: a public HTTPS contributor page
            # (e.g. rubato.audio) reaching this loopback dev server is a
            # public->private request that Chrome blocks at the preflight unless
            # the server returns this header. Harmless for a localhost editor.
            self.send_header("Access-Control-Allow-Private-Network", "true")
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
            self.cors()
            self.end_headers()
            self.wfile.write(body)

        def do_OPTIONS(self) -> None:  # noqa: N802
            if not self.origin_allowed():
                return
            self.send_response(HTTPStatus.NO_CONTENT)
            self.cors()
            self.end_headers()

        def do_GET(self) -> None:  # noqa: N802
            if not self.origin_allowed():
                return
            request_path = urlparse(self.path).path
            if request_path in ("/", "/index.html"):
                html = Path(__file__).with_name("dev_editor.html").read_bytes()
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(html)))
                self.cors()
                self.end_headers()
                self.wfile.write(html)
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
                self.cors()
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
    if args.editor != DEV_EDITOR_DEFAULT:
        print(f"or drive the full contributor site: {contributor_url_for(args.editor, server_url)}")
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
    build_parser_command.add_argument("--output", required=True)
    build_parser_command.add_argument("--toolchain", default="/usr/local/arm-4.8.3")
    build_parser_command.add_argument("--docker-image", default="plaits-lab-builder:local")
    build_parser_command.add_argument("--native", action="store_true", help=argparse.SUPPRESS)
    build_parser_command.set_defaults(handler=hardware_build_command)

    dev_parser = subparsers.add_parser("dev", help="serve a hot-reloading local model to the contributor UI")
    dev_parser.add_argument("package")
    dev_parser.add_argument("--host", default="127.0.0.1")
    dev_parser.add_argument("--port", type=int, default=4179)
    dev_parser.add_argument("--editor", default=DEV_EDITOR_DEFAULT)
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
