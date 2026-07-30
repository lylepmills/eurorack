from __future__ import annotations

import json
import unittest
from pathlib import Path

from container_server import (
    ALL_STEREO_MACROS,
    MAX_REQUEST_BYTES,
    _recipe_is_stereo,
    _stereo_disable_flags,
    classify_link_failure,
    request_size_is_valid,
)
from generate_engine_config import (
    DEFAULT_CHORD_TABLES,
    DEFAULT_CONFIGURATION,
    validate_recipe,
)


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


class RequestSizeBudgetTest(unittest.TestCase):
    """The container transport limit must cover every normalized recipe shape.

    FM patches are 4 KiB as binary data, but the Worker forwards their packed
    bytes as decimal JSON arrays. The original 32 KiB cap predated custom banks
    and rejected valid one- and two-bank recipes before compilation.
    """

    @staticmethod
    def bank_document(index: int) -> dict:
        return {
            "id": f"bank-{index}",
            "packageId": f"local/bank-{index}",
            "version": "1.0.0",
            "digest": None,
            "name": "N" * 80,
            "author": "A" * 80,
            "license": "Private",
            "origin": "Local",
            "description": "D" * 240,
            "voices": [
                {
                    "name": "V" * 16,
                    "algorithm": 32,
                    # Three decimal digits per byte exercises the largest
                    # canonical JSON representation a valid patch can have.
                    "packed": [127] * 128,
                }
                for _voice in range(32)
            ],
        }

    def maximum_bank_recipe_request(self) -> bytes:
        recipe = {
            "schemaVersion": 12,
            "target": "mutable-instruments-plaits",
            "firmware": "rubato-plaits",
            "output": "audio-wav",
            "slots": ["dx7-bank-a"] * 32,
            "preferences": dict(DEFAULT_CONFIGURATION["preferences"]),
            "initialOptions": dict(DEFAULT_CONFIGURATION["initialOptions"]),
            "resources": {
                "chordTables": [dict(table) for table in DEFAULT_CHORD_TABLES],
                "userDataBanks": [
                    {"slot": slot, "bank": self.bank_document(slot)}
                    for slot in range(32)
                ],
            },
        }
        # Prove this is a contract-valid recipe before measuring the exact
        # compact JSON shape the Worker forwards to the container.
        validate_recipe(recipe)
        return json.dumps(
            {"buildKey": "a" * 64, "recipe": recipe},
            separators=(",", ":"),
        ).encode("utf-8")

    def test_all_32_supported_custom_banks_fit_the_transport_budget(self) -> None:
        request = self.maximum_bank_recipe_request()
        self.assertGreater(len(request), 32 * 1024)
        self.assertLessEqual(len(request), MAX_REQUEST_BYTES)
        self.assertTrue(request_size_is_valid(len(request)))

    def test_transport_budget_still_rejects_empty_and_oversized_bodies(self) -> None:
        self.assertFalse(request_size_is_valid(0))
        self.assertTrue(request_size_is_valid(MAX_REQUEST_BYTES))
        self.assertFalse(request_size_is_valid(MAX_REQUEST_BYTES + 1))


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
