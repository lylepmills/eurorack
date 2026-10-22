#!/usr/bin/env python3
# Copyright 2026 Matt Policastro.
# SPDX-License-Identifier: MIT
"""Print a makefile fragment that builds one Plaits Lab package into the module.

The SDK already knows a package's translation units, engine class and
post-processing (the same facts `check`, `render` and `dev` use), so the
module asks it instead of keeping its own list.

    python3 sources.py <package directory | built-in catalog id>
"""

import importlib.util
import argparse
import hashlib
import json
import shlex
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("plaits_lab", HERE.parent / "plaits_lab.py")
plaits_lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plaits_lab)


def write_changed(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


def make_flags(flags: list[str]) -> str:
    return " ".join(shlex.quote(flag) for flag in flags).replace("$", "$$").replace("#", "\\#")


def wrappers(directory: Path, sources: list[Path]) -> list[str]:
    """Give make space-free source names; compiler depfiles track the originals.

    Copying source would hide later edits, and symlinks need extra privileges
    on Windows. Including the original also preserves its relative includes.
    """
    result = []
    for index, source in enumerate(sources):
        path = directory / (str(index) + source.suffix)
        write_changed(path, '#include "' + source.resolve().as_posix() + '"\n')
        result.append(path.as_posix())
    return result


def main(argument: str, prepare: Path | None = None) -> None:
    path = Path(argument)
    package = (plaits_lab.load_package(str(path.resolve())) if path.exists()
               else plaits_lab.builtin_package(argument))
    manifest = package["manifest"]
    post = manifest.get("postProcessing", {})
    entry = HERE / "src" / "lab_voice.cc"
    units = [unit for unit in plaits_lab.engine_translation_units(package, entry)
             if Path(unit).resolve() != entry]
    flags = [
        f"-I{Path(package['repo_root']).resolve()}",
        f"-I{Path(package['source_root']).resolve()}",
        f'-DPLAITS_LAB_ENGINE_HEADER="{plaits_lab.engine_header_define(package)}"',
        f"-DPLAITS_LAB_ENGINE_CLASS=plaits::{manifest['source']['className']}",
        "-DPLAITS_LAB_ENGINE_NAME=" + json.dumps(manifest["name"], ensure_ascii=False),
        "-DPLAITS_LAB_ENGINE_ID=" + json.dumps(manifest["id"]),
        f"-DPLAITS_LAB_USER_DATA_BANK={package.get('user_data_bank', -1)}",
        f"-DPLAITS_LAB_ALREADY_ENVELOPED={int(bool(post.get('alreadyEnveloped', False)))}",
        f"-DPLAITS_LAB_OUT_GAIN={float(post.get('outGain', 1.0))!r}f",
        f"-DPLAITS_LAB_AUX_GAIN={float(post.get('auxGain', 1.0))!r}f",
    ]
    # Tooltips keep the package's names; the panel uses stable control labels.
    for control in manifest.get("controls", []):
        key = control["id"].upper()
        for suffix, field in (("LABEL", "label"), ("DESCRIPTION", "description")):
            if control.get(field):
                flags.append(f"-DPLAITS_LAB_{suffix}_{key}="
                             + json.dumps(control[field], ensure_ascii=False))
    if prepare is not None:
        # Package-specific object paths also cover the Rack wrapper. Switching
        # packages cannot reuse another engine's compiled class or metadata.
        key = hashlib.sha256(json.dumps([flags, units, str(HERE)]).encode()).hexdigest()[:16]
        generated = wrappers(prepare / key, [Path(unit) for unit in units] + [
            entry, HERE / "src/plugin.cpp", HERE / "tests/test_package.cc"])
        units = generated[:-3]
        print("PLAITS_LAB_VOICE_SOURCE := " + generated[-3])
        print("PLAITS_LAB_PLUGIN_SOURCE := " + generated[-2])
        print("PLAITS_LAB_SMOKE_SOURCE := " + generated[-1])
        print("PLAITS_LAB_BUILD_KEY := " + key)
        test_flags = [
            f"-I{Path(package['repo_root']).resolve()}", f"-I{HERE / 'src'}", f"-I{HERE / 'tests'}",
            '-DPLAITS_LAB_ENGINE_HEADER="probe_engine.h"',
            "-DPLAITS_LAB_ENGINE_CLASS=plaits::ProbeEngine",
            '-DPLAITS_LAB_ENGINE_NAME="Control-path probe"',
            "-DPLAITS_LAB_USER_DATA_BANK=-1", "-DPLAITS_LAB_ALREADY_ENVELOPED=0",
            "-DPLAITS_LAB_OUT_GAIN=1.0f", "-DPLAITS_LAB_AUX_GAIN=1.0f",
        ]
        test_sources = wrappers(prepare / "voice-test", [
            entry, HERE / "tests/test_lab_voice.cc",
            Path(package["repo_root"]) / "stmlib/dsp/units.cc"])
        print("PLAITS_LAB_TEST_SOURCES := " + " ".join(test_sources))
        print("PLAITS_LAB_TEST_FLAGS := " + make_flags(test_flags))
        flags.append(f"-I{HERE / 'src'}")
    print("PLAITS_LAB_SOURCES := " + " ".join(str(unit) for unit in units))
    print("PLAITS_LAB_FLAGS := " + make_flags(flags))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package")
    parser.add_argument("--prepare", type=Path, help="Generate local source wrappers for make")
    args = parser.parse_args()
    main(args.package, args.prepare)
