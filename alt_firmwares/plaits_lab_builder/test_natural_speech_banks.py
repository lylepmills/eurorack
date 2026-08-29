"""Tests for the Natural Speech recipe bank contract."""

from __future__ import annotations

import base64
import unittest

from natural_speech_banks import (
    BYTES_PER_FRAME,
    FRAME_STRUCT,
    LPC_ORDER,
    MAX_BANKS,
    MAX_FRAMES,
    MAX_WORDS,
    bank_flash_bytes,
    render_natural_speech_config,
    validate_natural_speech_banks,
)


def frame(gain=200, f0=0, lars=None):
    lars = lars if lars is not None else [0] * LPC_ORDER
    return FRAME_STRUCT.pack(gain, f0 & 0xFF, 0xFF, 0xFF, 0x1F, *lars)


def bank(words=("hello",), frames_per_word=2, lars=None, gain=200):
    packed = b"".join(frame(gain=gain, lars=lars)
                      for _ in range(len(words) * frames_per_word))
    boundaries = [i * frames_per_word for i in range(len(words) + 1)]
    return {"words": list(words), "wordBoundaries": boundaries,
            "frameData": base64.b64encode(packed).decode("ascii")}


def recipe(*banks):
    return {"customBanks": list(banks) or [bank()]}


class ValidationTests(unittest.TestCase):
    def test_accepts_a_well_formed_bank(self):
        result = validate_natural_speech_banks(recipe(bank(("one", "two"))))
        self.assertEqual(len(result["customBanks"]), 1)
        self.assertEqual(result["customBanks"][0]["words"], ["one", "two"])

    def test_rejects_unknown_fields(self):
        with self.assertRaises(ValueError):
            validate_natural_speech_banks({"customBanks": [], "extra": 1})

    def test_rejects_misaligned_frame_data(self):
        broken = bank()
        packed = base64.b64decode(broken["frameData"])
        broken["frameData"] = base64.b64encode(packed[:-1]).decode("ascii")
        with self.assertRaises(ValueError):
            validate_natural_speech_banks(recipe(broken))

    def test_rejects_boundaries_that_do_not_cover_the_frames(self):
        broken = bank(("a", "b"))
        broken["wordBoundaries"] = [0, 2, 3]  # last must equal frame count
        with self.assertRaises(ValueError):
            validate_natural_speech_banks(recipe(broken))

    def test_rejects_non_monotonic_boundaries(self):
        broken = bank(("a", "b"))
        broken["wordBoundaries"] = [0, 4, 4]
        with self.assertRaises(ValueError):
            validate_natural_speech_banks(recipe(broken))

    def test_rejects_too_many_banks(self):
        with self.assertRaises(ValueError):
            validate_natural_speech_banks(recipe(*[bank()] * (MAX_BANKS + 1)))

    def test_rejects_an_empty_recipe(self):
        with self.assertRaises(ValueError):
            validate_natural_speech_banks({"customBanks": []})

    def test_rejects_too_many_words(self):
        words = tuple(f"w{i}" for i in range(MAX_WORDS + 1))
        with self.assertRaises(ValueError):
            validate_natural_speech_banks(recipe(bank(words)))

    def test_rejects_a_bank_over_the_frame_ceiling(self):
        packed = frame() * (MAX_FRAMES + 1)
        over = {"words": ["long"], "wordBoundaries": [0, MAX_FRAMES + 1],
                "frameData": base64.b64encode(packed).decode("ascii")}
        with self.assertRaises(ValueError):
            validate_natural_speech_banks(recipe(over))


class MeanTractTests(unittest.TestCase):
    """MACRO interpolates each frame toward its bank's mean tract, so the
    mean is derived here rather than trusted from the client."""

    def test_mean_is_derived_from_the_frames(self):
        lars = [7] * LPC_ORDER
        result = validate_natural_speech_banks(recipe(bank(lars=lars)))
        self.assertEqual(result["customBanks"][0]["meanLar"],
                         [7] * LPC_ORDER)

    def test_silent_frames_do_not_drag_the_mean(self):
        """A gain of 0 means hard silence, and its fitted tract is
        meaningless."""
        audible = frame(gain=200, lars=[10] * LPC_ORDER)
        silent = frame(gain=0, lars=[-120] * LPC_ORDER)
        packed = audible + silent
        entry = {"words": ["w"], "wordBoundaries": [0, 2],
                 "frameData": base64.b64encode(packed).decode("ascii")}
        result = validate_natural_speech_banks(recipe(entry))
        self.assertEqual(result["customBanks"][0]["meanLar"],
                         [10] * LPC_ORDER)

    def test_mean_is_not_accepted_from_the_client(self):
        entry = bank()
        entry["meanLar"] = [99] * LPC_ORDER
        with self.assertRaises(ValueError):
            validate_natural_speech_banks(recipe(entry))


class RenderTests(unittest.TestCase):
    def test_absent_banks_render_the_off_switch(self):
        text = render_natural_speech_config(None)
        self.assertIn("#define PLAITS_HAS_CUSTOM_NATURAL_SPEECH_BANKS 0", text)
        self.assertNotIn("kBankFrames", text)

    def test_rendered_config_carries_every_frame(self):
        value = validate_natural_speech_banks(
            recipe(bank(("one", "two")), bank(("three",))))
        text = render_natural_speech_config(value)
        self.assertIn("#define PLAITS_HAS_CUSTOM_NATURAL_SPEECH_BANKS 1", text)
        self.assertIn("const int kNumBanks = 2;", text)
        self.assertIn("const int kNumWords = 3;", text)
        self.assertIn("const int kNumFrames = 6;", text)
        self.assertIn("kBankFirstWord[] = { 0, 2, 3 }", text)
        self.assertIn("kWordBoundaries[] = { 0, 2, 4, 6 }", text)

    def test_mean_tracts_are_emitted_per_bank(self):
        value = validate_natural_speech_banks(
            recipe(bank(lars=[3] * LPC_ORDER), bank(lars=[-4] * LPC_ORDER)))
        text = render_natural_speech_config(value)
        expected = ", ".join(["3"] * LPC_ORDER + ["-4"] * LPC_ORDER)
        self.assertIn(f"kBankMeanLar[] = {{ {expected} }}", text)

    def test_word_names_cannot_break_out_of_a_comment(self):
        value = validate_natural_speech_banks(recipe(bank(("a */ evil",))))
        text = render_natural_speech_config(value)
        self.assertNotIn("*/ evil", text)


class FlashCostTests(unittest.TestCase):
    def test_cost_is_reported_at_the_real_frame_size(self):
        value = validate_natural_speech_banks(recipe(bank(("a", "b"))))
        cost = bank_flash_bytes(value)
        self.assertEqual(cost, 4 * BYTES_PER_FRAME + LPC_ORDER + 2 * 3 + 2 * 2)

    def test_no_banks_cost_nothing(self):
        self.assertEqual(bank_flash_bytes(None), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
