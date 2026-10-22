#!/usr/bin/env python3
# Copyright 2026 Matt Policastro.
# SPDX-License-Identifier: MIT
"""The VCV test bench's build glue (vcv/sources.py) stays in step with the SDK."""

from __future__ import annotations

import json
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SDK_DIR = Path(__file__).resolve().parents[1]
SOURCES = SDK_DIR / "vcv" / "sources.py"


def blank_package(directory: str) -> Path:
    """A fresh community package from `init`, outside the repository."""
    package = Path(directory) / "probe"
    subprocess.run([sys.executable, "-B", str(SDK_DIR / "plaits_lab.py"), "init", str(package),
                    "--author", "Test Bench", "--name", "Test Bench Probe"],
                   capture_output=True, text=True, check=True)
    return package


def fragment(package: str | Path) -> dict[str, str]:
    """The makefile fragment sources.py prints, as variable -> value."""
    result = subprocess.run([sys.executable, "-B", str(SOURCES), str(package)],
                            capture_output=True, text=True, check=True)
    return dict(line.split(" := ", 1) for line in result.stdout.splitlines())


def make_flags(value: str) -> list[str]:
    """The shell words make passes on, after undoing make's own escapes."""
    return shlex.split(value.replace("\\#", "#").replace("$$", "$"))


class SourcesTest(unittest.TestCase):
    def test_builtin_by_catalog_id(self) -> None:
        variables = fragment("chords")
        flags = make_flags(variables["PLAITS_LAB_FLAGS"])
        self.assertIn("-DPLAITS_LAB_ENGINE_CLASS=plaits::ChordEngine", flags)
        self.assertIn('-DPLAITS_LAB_ENGINE_HEADER="plaits/dsp/engine/chord_engine.h"', flags)
        self.assertIn("-DPLAITS_LAB_ALREADY_ENVELOPED=0", flags)
        self.assertIn("-DPLAITS_LAB_OUT_GAIN=0.8f", flags)
        sources = variables["PLAITS_LAB_SOURCES"].split()
        self.assertTrue(any(s.endswith("plaits/dsp/engine/chord_engine.cc") for s in sources))
        self.assertTrue(all(Path(s).is_file() for s in sources))
        self.assertFalse(any(s.endswith("lab_voice.cc") for s in sources))

    def test_community_package_by_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            package = blank_package(directory)
            manifest = json.loads((package / "plaits-engine.json").read_text(encoding="utf-8"))
            variables = fragment(package)
        flags = make_flags(variables["PLAITS_LAB_FLAGS"])
        self.assertIn(f"-DPLAITS_LAB_ENGINE_CLASS=plaits::{manifest['source']['className']}", flags)
        self.assertIn(f'-DPLAITS_LAB_ENGINE_NAME="{manifest["name"]}"', flags)
        labels = {c["id"]: c["label"] for c in manifest["controls"]}
        self.assertIn(f'-DPLAITS_LAB_LABEL_HARMONICS="{labels["harmonics"]}"', flags)

    def test_awkward_label_text_survives_make_and_the_compiler(self) -> None:
        compiler = shutil.which("c++")
        if compiler is None or shutil.which("make") is None:
            self.skipTest("needs make and a C++ compiler")
        awkward = "Say \"hi\", it's #1 at $5 \\ 50%"
        with tempfile.TemporaryDirectory() as directory:
            package = blank_package(directory)
            manifest_path = package / "plaits-engine.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            for control in manifest["controls"]:
                if control["id"] == "timbre":
                    control["description"] = awkward
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            build = Path(directory) / "build"
            build.mkdir()
            (build / "package.mk").write_text(
                subprocess.run([sys.executable, "-B", str(SOURCES), str(package)],
                               capture_output=True, text=True, check=True).stdout,
                encoding="utf-8")
            (build / "probe.cc").write_text(
                '#include <cstdio>\nint main() { std::fputs(PLAITS_LAB_DESCRIPTION_TIMBRE, stdout); }\n',
                encoding="utf-8")
            (build / "Makefile").write_text(
                "include package.mk\nprobe: probe.cc\n\t$(CXX) $(PLAITS_LAB_FLAGS) probe.cc -o probe\n",
                encoding="utf-8")
            subprocess.run(["make", "-s", "-C", str(build), f"CXX={compiler}", "probe"],
                           capture_output=True, text=True, check=True)
            printed = subprocess.run([str(build / "probe")], capture_output=True, text=True, check=True)
        self.assertEqual(printed.stdout, awkward)


if __name__ == "__main__":
    unittest.main()
