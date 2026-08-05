from __future__ import annotations

import base64
import re
import struct
import tempfile
import unittest
from pathlib import Path

from container_server import LPC_FRAME, _pack_plans
from speech_banks import render_speech_config, validate_speech_banks


def bank(words=("hello", "world")):
    frames = [
        struct.pack("<BBhh8b", 40, 80, 1000, -1000, 1, 2, 3, 4, 5, 6, 7, 8),
        struct.pack("<BBhh8b", 0, 80, 1000, -1000, 1, 2, 3, 4, 5, 6, 7, 8),
        struct.pack("<BBhh8b", 50, 75, 900, -900, -1, -2, -3, -4, -5, -6, -7, -8),
    ]
    return {
        "words": list(words),
        "wordBoundaries": [0, 2, 3],
        "frameData": base64.b64encode(b"".join(frames)).decode("ascii"),
    }


class SpeechBanksTest(unittest.TestCase):
    def test_custom_speech_layout_macro_covers_every_firmware_object(self) -> None:
        makefile = (Path(__file__).resolve().parents[2] / "plaits/makefile").read_text(
            encoding="utf-8")
        match = re.search(r"^SPEECH_LAYOUT_OBJS\s*=\s*(.+)$", makefile, re.MULTILINE)
        self.assertIsNotNone(match)
        declared = set(match.group(1).split())
        self.assertEqual(declared, {
            "voice.o",
            "plaits.o",
            "ui.o",
            "lpc_speech_engine.o",
            "lpc_speech_synth_controller.o",
            "lpc_speech_synth_phonemes.o",
        })

    def test_preview_plans_gain_one_guard_frame_per_word(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "word.plan"
            path.write_text(
                "40 80 1000 -1000 1 2 3 4 5 6 7 8\n"
                "0 80 1000 -1000 1 2 3 4 5 6 7 8\n",
                encoding="utf-8",
            )
            boundaries, encoded = _pack_plans([path])
        frames = [
            LPC_FRAME.unpack_from(base64.b64decode(encoded), offset)
            for offset in range(0, 3 * LPC_FRAME.size, LPC_FRAME.size)
        ]
        self.assertEqual(boundaries, [0, 3])
        self.assertEqual(frames[-1], frames[-2])

    def test_validates_and_renders_selected_stock_plus_raw_banks(self) -> None:
        value = validate_speech_banks({"stockBankIds": [0, 3], "customBanks": [bank()]})
        rendered = render_speech_config(value)
        self.assertIn("CUSTOM_SPEECH_NUM_STOCK_BANKS 2", rendered)
        self.assertIn("{ bank_0, sizeof(bank_0) }", rendered)
        self.assertIn("{ bank_3, sizeof(bank_3) }", rendered)
        self.assertIn("custom_speech_bank_0_boundaries[] = { 0, 2, 3 }", rendered)
        self.assertIn("{ 40, 80, 1000, -1000, 1, 2, 3, 4, 5, 6, 7, 8 }", rendered)

    def test_rejects_misaligned_or_inconsistent_data(self) -> None:
        malformed = bank()
        malformed["frameData"] = base64.b64encode(b"not a frame").decode("ascii")
        with self.assertRaisesRegex(ValueError, "misaligned"):
            validate_speech_banks({"stockBankIds": [], "customBanks": [malformed]})

        wrong_boundary = bank()
        wrong_boundary["wordBoundaries"][-1] = 2
        with self.assertRaisesRegex(ValueError, "boundaries"):
            validate_speech_banks({"stockBankIds": [], "customBanks": [wrong_boundary]})

    def test_stock_only_selection_is_supported(self) -> None:
        value = validate_speech_banks({"stockBankIds": [4], "customBanks": []})
        rendered = render_speech_config(value)
        self.assertIn("CUSTOM_SPEECH_NUM_WORD_BANKS 0", rendered)
        self.assertIn("custom_speech_word_banks = NULL", rendered)


if __name__ == "__main__":
    unittest.main()
