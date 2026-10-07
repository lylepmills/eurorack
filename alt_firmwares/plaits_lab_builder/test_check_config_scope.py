"""check_config_scope.py: which units must be force-included with the recipe config.

The rule the guard enforces is that any built unit whose compilation depends on
a recipe option is in RECIPE_CONFIG_OBJS. Until 2026-10-07 it only followed
includes of the config headers, so a unit reaching plaits/dsp/engine/engine.h —
which `#if`s on PLAITS_BUILD_ENABLE_SYNC_INPUT and the FM options without
including build_config.h — counted as recipe-independent. Four units (three of
them engines) were off the list on that basis, and the guard passed.
"""

from __future__ import annotations

import os
import re
import shutil
import tempfile
import textwrap
import unittest
from pathlib import Path

import check_config_scope as ccs


REPO = Path(__file__).resolve().parents[2]


def write(root: Path, path: str, text: str) -> None:
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(textwrap.dedent(text))


class RepoTest(unittest.TestCase):
    def test_every_required_unit_is_force_included(self) -> None:
        missing, _, _ = ccs.check(str(REPO))
        self.assertEqual(missing, set())

    def test_every_unit_reaching_engine_h_is_required(self) -> None:
        scope = ccs.Scope(str(REPO))
        required = scope.required_objs()
        reaching = []
        for directory in ccs.SOURCE_DIRS:
            if not (REPO / directory).is_dir():
                continue
            for name in sorted(os.listdir(REPO / directory)):
                path = f"{directory}/{name}"
                if name.endswith(".cc") and self._includes_engine_h(path, set()):
                    reaching.append(name[:-3] + ".o")
        # Every engine and then some; an empty list would make this vacuous.
        self.assertGreater(len(reaching), 100)
        self.assertEqual(sorted(set(reaching) - required), [])

    def test_the_units_master_left_off_are_reported(self) -> None:
        # What origin/master at 08a32008 left off the list. Taking exactly these
        # back out must fail the guard and name exactly these.
        dropped = {"bubbletime_engine.o", "natural_speech_engine.o",
                   "zxphase48k_engine.o", "resonator.o"}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for directory in ("plaits", "alt_firmwares/plaits_lab_builder"):
                shutil.copytree(REPO / directory, root / directory,
                                ignore=shutil.ignore_patterns("build", "node_modules", ".*"))
            shutil.copytree(REPO / "stmlib", root / "stmlib",
                            ignore=shutil.ignore_patterns(".*"))
            makefile = root / "plaits/makefile"
            text = makefile.read_text()
            line = re.search(r"^RECIPE_CONFIG_OBJS\s*=.*$", text, re.MULTILINE).group(0)
            kept = [obj for obj in line.split("=", 1)[1].split() if obj not in dropped]
            makefile.write_text(text.replace(line, "RECIPE_CONFIG_OBJS = " + " ".join(kept)))
            missing, _, _ = ccs.check(str(root))
        self.assertEqual(missing, dropped)

    def _includes_engine_h(self, path: str, seen: set) -> bool:
        if path in seen:
            return False
        seen.add(path)
        text = ccs.read(str(REPO), path)
        if text is None:
            return False
        for inc in ccs.INCLUDE_RE.findall(text):
            if inc == "plaits/dsp/engine/engine.h" or self._includes_engine_h(inc, seen):
                return True
        return False


class SyntheticTreeTest(unittest.TestCase):
    """The rule on a tree small enough to read in one go."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        write(self.root, ccs.GENERATOR, """
            out.append(f"#define PLAITS_BUILD_ENABLE_SYNC_INPUT {sync}")
            out.append(f"#define PLAITS_BUILD_FAST_FM {fast}")
        """)
        write(self.root, "plaits/build_config.h", """
            #ifndef PLAITS_BUILD_CONFIG_H_
            #define PLAITS_BUILD_CONFIG_H_
            #ifndef PLAITS_BUILD_FAST_FM
            #define PLAITS_BUILD_FAST_FM 0
            #endif
            #define PLAITS_BUILD_PALETTE_ONLY 0
            #define PLAITS_BUILD_FREQUENCY_OFFSET_FM \\
                (PLAITS_BUILD_PALETTE_ONLY || PLAITS_BUILD_FAST_FM)
            #endif
        """)
        # engine.h's shape: reads a recipe macro, never includes build_config.h.
        write(self.root, "plaits/dsp/engine/engine.h", """
            struct EngineParameters {
              float note;
            #if PLAITS_BUILD_ENABLE_SYNC_INPUT
              unsigned hard_sync;
            #endif
              bool stereo;
            };
        """)
        write(self.root, "plaits/dsp/engine/via_engine_h.cc",
              '#include "plaits/dsp/engine/engine.h"\n')
        write(self.root, "plaits/dsp/engine/via_build_config.cc",
              '#include "plaits/build_config.h"\n')
        write(self.root, "plaits/dsp/engine/derived_macro.cc",
              "float f = PLAITS_BUILD_FREQUENCY_OFFSET_FM ? 1.0f : 0.0f;\n")
        write(self.root, "plaits/dsp/engine/palette_only.cc",
              "#if PLAITS_BUILD_PALETTE_ONLY\nint x;\n#endif\n")
        write(self.root, "plaits/dsp/engine/comment_only.cc",
              "// Unlike PLAITS_BUILD_ENABLE_SYNC_INPUT units, this one is plain.\n"
              "/* PLAITS_BUILD_FAST_FM */ int y;\n")
        write(self.root, "plaits/dsp/engine/independent.cc", "int z;\n")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def required(self) -> set:
        return ccs.Scope(str(self.root)).required_objs()

    def test_a_header_that_only_reads_a_recipe_macro_couples_its_includers(self) -> None:
        self.assertIn("via_engine_h.o", self.required())

    def test_including_a_config_header_still_couples(self) -> None:
        self.assertIn("via_build_config.o", self.required())

    def test_a_macro_derived_from_a_recipe_option_is_a_recipe_macro(self) -> None:
        self.assertIn("derived_macro.o", self.required())

    def test_an_option_no_recipe_can_set_does_not_couple(self) -> None:
        self.assertNotIn("palette_only.o", self.required())

    def test_a_mention_in_a_comment_does_not_couple(self) -> None:
        self.assertNotIn("comment_only.o", self.required())
        self.assertNotIn("independent.o", self.required())

    def test_check_reports_a_unit_missing_from_the_list(self) -> None:
        write(self.root, "plaits/makefile",
              "RECIPE_CONFIG_OBJS = via_build_config.o derived_macro.o\n")
        missing, extra, _ = ccs.check(str(self.root))
        self.assertEqual(missing, {"via_engine_h.o"})
        self.assertEqual(extra, set())


if __name__ == "__main__":
    unittest.main()
