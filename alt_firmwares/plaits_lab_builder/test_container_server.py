from __future__ import annotations

import json
import unittest
from pathlib import Path

from container_server import (
    ALL_STEREO_MACROS,
    _recipe_is_stereo,
    _stereo_disable_flags,
    classify_link_failure,
)
from generate_engine_config import DEFAULT_CONFIGURATION, validate_recipe


class ClassifyLinkFailureTest(unittest.TestCase):
    def test_flash_overflow_reports_the_exact_overage(self) -> None:
        log = (
            "arm-none-eabi/bin/ld: <build-root>/plaits/plaits.elf "
            "section `.text' will not fit in region `FLASH'\n"
            "arm-none-eabi/bin/ld: region `FLASH' overflowed by 4096 bytes\n"
            "collect2: error: ld returned 1 exit status\n"
        )
        result = classify_link_failure(log)
        self.assertIsNotNone(result)
        code, message = result
        self.assertEqual(code, "flash_budget_exceeded")
        self.assertIn("4096 bytes", message)
        self.assertIn("flash", message.lower())

    def test_flash_overflow_without_a_byte_count_still_classifies(self) -> None:
        # Some ld invocations emit only the section "will not fit" line.
        log = "ld: section `.rodata' will not fit in region `FLASH'\n"
        code, message = classify_link_failure(log)
        self.assertEqual(code, "flash_budget_exceeded")
        self.assertNotIn("None", message)
        self.assertNotIn("bytes", message)  # no overage available -> no count

    def test_ram_overflow_is_classified_as_ram_not_compiler(self) -> None:
        log = (
            "ld: region `RAM' overflowed by 512 bytes\n"
            "ld: section `.bss' will not fit in region `RAM'\n"
        )
        code, message = classify_link_failure(log)
        self.assertEqual(code, "ram_budget_exceeded")
        self.assertIn("512 bytes", message)
        self.assertIn("RAM", message)

    def test_a_genuine_compiler_error_is_not_a_region_overflow(self) -> None:
        log = (
            "plaits/dsp/voice.cc:42:3: error: 'foo' was not declared in this scope\n"
            "make: *** [voice.o] Error 1\n"
        )
        self.assertIsNone(classify_link_failure(log))


class StereoSelectionTest(unittest.TestCase):
    """The seam where the aux-output NUMBER decides whether stereo is compiled.

    Nothing else notices when the firmware renumbers that option: the recipe
    binds by name, the build still succeeds, and the only symptom is a stereo
    firmware that plays mono. So read it back through validate_recipe rather
    than restating the number.
    """

    def recipe_for(self, aux_output: str) -> object:
        recipe = json.loads(
            (Path(__file__).parent / "default_recipe.json").read_text(encoding="utf-8")
        )
        recipe["schemaVersion"] = 4
        recipe["preferences"] = {"navigationMode": "linear"}
        recipe["initialOptions"] = dict(
            DEFAULT_CONFIGURATION["initialOptions"], auxOutput=aux_output
        )
        return validate_recipe(recipe)

    def test_only_a_stereo_recipe_selects_the_stereo_render_path(self) -> None:
        self.assertTrue(_recipe_is_stereo(self.recipe_for("stereo")))
        for aux_output in ("alternate-model", "square-subosc", "sine-subosc"):
            self.assertFalse(
                _recipe_is_stereo(self.recipe_for(aux_output)), msg=aux_output
            )

    def test_a_stereo_recipe_without_a_list_keeps_every_engine_stereo(self) -> None:
        # Schema <= 9 carried no per-engine list; the whole build is stereo.
        self.assertEqual(_stereo_disable_flags(True, None), [])

    def test_a_mono_recipe_disables_every_engine_stereo_path(self) -> None:
        self.assertEqual(
            len(_stereo_disable_flags(False, None)), len(ALL_STEREO_MACROS)
        )


class StereoMacroRegistrationTest(unittest.TestCase):
    """STEREO_MACROS and the makefile's PLAITS_STEREO_MODELS must agree.

    They are two hand-maintained lists of the same fact -- which engines have a
    gated stereo render path. Drift is SILENT and asymmetric: a macro the
    makefile knows and this map does not can never be switched off (the engine
    always pays for its stereo branch), while a macro this map emits and the
    makefile does not is a make variable nothing consumes. Neither breaks a
    build, so only a test catches it. Adding four Braids ports to both lists at
    once is what motivated this.
    """

    def makefile_macros(self) -> set[str]:
        makefile = (
            Path(__file__).resolve().parents[2] / "plaits" / "makefile"
        ).read_text()
        start = makefile.index("PLAITS_STEREO_MODELS =")
        # A backslash-continued make variable: take lines until one does not
        # continue.
        lines = []
        for line in makefile[start:].splitlines():
            lines.append(line)
            if not line.rstrip().endswith("\\"):
                break
        body = " ".join(lines).replace("\\", "")
        body = body.split("=", 1)[1]
        return {pair.split(":", 1)[0] for pair in body.split()}

    def test_every_makefile_macro_is_mapped_from_a_catalog_id(self) -> None:
        self.assertEqual(self.makefile_macros(), set(ALL_STEREO_MACROS))


if __name__ == "__main__":
    unittest.main()
