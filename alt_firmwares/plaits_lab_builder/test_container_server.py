from __future__ import annotations

import json
import base64
import struct
import tempfile
import unittest
import wave
from pathlib import Path
from unittest.mock import patch

from container_server import (
    ALL_STEREO_MACROS,
    BuildError,
    FLASH_BUDGET_BYTES,
    MAX_REQUEST_BYTES,
    RAM_BUDGET_BYTES,
    RAM_STACK_RESERVE_BYTES,
    _build_targets,
    _concatenate_pcm_wavs,
    _declared_region_count,
    _validate_saved_bank_preview_request,
    _validate_speech_request,
    _write_saved_plan,
    _recipe_is_stereo,
    _stereo_disable_flags,
    classify_link_failure,
    request_size_is_valid,
    validate_linked_firmware,
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


class LinkedFirmwareSafetyTest(unittest.TestCase):
    def elf(self, directory: str) -> Path:
        path = Path(directory) / "plaits.elf"
        path.write_bytes(b"ELF")
        return path

    @patch("container_server._linked_flash_span", return_value=1200)
    @patch("container_server.parse_size", return_value=(1000, 100, 200))
    @patch("container_server.check_elf")
    def test_safe_build_checks_replaceable_fm_layout(
        self, check, _size, _span
    ) -> None:
        config = "static const int kNumUserDataRegions = 2;\n"
        with tempfile.TemporaryDirectory() as directory:
            result = validate_linked_firmware(self.elf(directory), config)
        check.assert_called_once()
        self.assertEqual(result["flashBytes"], 1200)
        self.assertEqual(result["replaceableFmBankRegions"], 2)

    @patch("container_server._linked_flash_span", return_value=0)
    @patch("container_server.parse_size")
    def test_flash_overflow_fails_closed(self, size, _span) -> None:
        size.return_value = (FLASH_BUDGET_BYTES + 1, 0, 0)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(BuildError) as raised:
                validate_linked_firmware(self.elf(directory), "")
        self.assertEqual(raised.exception.code, "flash_budget_exceeded")

    @patch("container_server._linked_flash_span", return_value=1000)
    @patch("container_server.parse_size")
    def test_ram_overflow_includes_the_stack_reserve(self, size, _span) -> None:
        size.return_value = (
            1000,
            0,
            RAM_BUDGET_BYTES - RAM_STACK_RESERVE_BYTES + 1,
        )
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(BuildError) as raised:
                validate_linked_firmware(self.elf(directory), "")
        self.assertEqual(raised.exception.code, "ram_budget_exceeded")

    @patch("container_server.check_elf", side_effect=ValueError("shared page"))
    def test_unsafe_replaceable_fm_layout_fails_closed(self, _check) -> None:
        config = "static const int kNumUserDataRegions = 1;\n"
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(BuildError) as raised:
                validate_linked_firmware(self.elf(directory), config)
        self.assertEqual(raised.exception.code, "unsafe_flash_layout")


class UserDataRegionCountTest(unittest.TestCase):
    def test_generated_region_macro_counts_fm_and_custom_model_regions(self) -> None:
        config = "#define PLAITS_USER_DATA_REGION_COUNT 5\n"
        self.assertEqual(_declared_region_count(config), 5)
        self.assertEqual(_declared_region_count("#define PLAITS_ENGINE_COUNT 24\n"), 0)


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


class SavedSpeechPreviewTest(unittest.TestCase):
    def request(self) -> dict:
        frame = struct.Struct("<BBhh8b")
        first = (5, 20, 100, -100, 1, 2, 3, 4, 5, 6, 7, 8)
        second = (6, 21, 101, -101, -1, -2, -3, -4, -5, -6, -7, -8)
        packed = frame.pack(*first) + frame.pack(*second) + frame.pack(*second)
        return {
            "format": "rubato.plaits-lpc-bank-preview/v1",
            "bank": {
                "words": ["hello"],
                "wordBoundaries": [0, 3],
                "frameData": base64.b64encode(packed).decode("ascii"),
            },
        }

    def test_saved_bank_reuses_the_firmware_resource_shape(self) -> None:
        bank = _validate_saved_bank_preview_request(self.request())
        self.assertEqual(bank["words"], ["hello"])
        self.assertEqual(len(bank["frames"]), 3)

    def test_plan_writer_removes_only_the_duplicate_firmware_guard(self) -> None:
        frames = _validate_saved_bank_preview_request(self.request())["frames"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "word.plan"
            _write_saved_plan(path, frames)
            self.assertEqual(len(path.read_text(encoding="utf-8").splitlines()), 2)

    def test_malformed_saved_bank_is_rejected(self) -> None:
        request = self.request()
        request["bank"]["wordBoundaries"] = [0, 4]
        with self.assertRaisesRegex(Exception, "saved Speech bank is invalid"):
            _validate_saved_bank_preview_request(request)

    def test_word_previews_with_different_lengths_concatenate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = [root / "short.wav", root / "long.wav"]
            for source, frames in zip(sources, (b"\x00\x00", b"\x01\x00\x02\x00")):
                with wave.open(str(source), "wb") as output:
                    output.setnchannels(1)
                    output.setsampwidth(2)
                    output.setframerate(48000)
                    output.writeframes(frames)
            target = root / "bank.wav"
            _concatenate_pcm_wavs(sources, target)
            with wave.open(str(target), "rb") as result:
                self.assertEqual(result.getnframes(), 3)


class SpeechEncodeRequestTest(unittest.TestCase):
    @staticmethod
    def request(word_count: int) -> dict:
        return {
            "format": "rubato.plaits-lpc-word-bank/v1",
            "name": "Capacity test",
            "language": "en-US",
            "entries": [
                {"word": f"word {index + 1}"} for index in range(word_count)
            ],
            "synthesis": {
                "voice": "af_heart",
                "pitchContour": "flat-to-natural",
                "referencePitchHz": 100,
            },
        }

    def test_accepts_thirty_two_words_but_not_thirty_three(self) -> None:
        self.assertEqual(len(_validate_speech_request(self.request(32))["entries"]), 32)
        with self.assertRaisesRegex(Exception, "between 1 and 32"):
            _validate_speech_request(self.request(33))


class FirmwareOutputTargetTest(unittest.TestCase):
    def test_wav_uses_the_audio_encoder_target(self) -> None:
        self.assertEqual(_build_targets("audio-wav"), ["wav"])

    def test_hex_is_application_only_and_keeps_the_binary_digest(self) -> None:
        targets = _build_targets("intel-hex")
        self.assertEqual(targets, ["bin", "hex"])
        self.assertNotIn("wav", targets)
        self.assertFalse(any("combo" in target or "bootloader" in target for target in targets))


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
