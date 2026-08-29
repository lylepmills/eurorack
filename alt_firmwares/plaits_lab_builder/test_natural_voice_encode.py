"""Tests for the Natural Voice bank encoder.

The load-bearing one is test_matches_the_research_encoder: this module is a
deliberate second copy of the lab original in the rubato-audio repo (the
same split the LPC path already has), so the two must produce identical
frames or the firmware plays something the research never auditioned.
"""

from __future__ import annotations

import base64
import struct
import sys
import unittest
import wave
from pathlib import Path
from tempfile import TemporaryDirectory

try:
    import numpy as np
except ModuleNotFoundError:  # pragma: no cover
    np = None

RESEARCH = (Path.home() / "Desktop" / "claude" / "rubato-audio" /
            "research" / "natural_speech")
RESEARCH_WORKTREE = (Path.home() / "rubato-worktrees" / "natural-speech-2" /
                     "research" / "natural_speech")


def _skip_without_world():
    try:
        import pyworld  # noqa: F401
    except ModuleNotFoundError:
        raise unittest.SkipTest("pyworld is not installed in this environment")


def _tone_wav(path: Path, seconds: float = 0.6, fs: int = 16000,
              f0: float = 140.0) -> Path:
    """A voiced-sounding buzz: enough structure for the analysis to fit."""
    t = np.arange(int(seconds * fs)) / fs
    x = sum(np.sin(2 * np.pi * f0 * h * t) / h for h in range(1, 12))
    x *= np.hanning(len(x)) * 0.4 / np.abs(x).max()
    with wave.open(str(path), "wb") as sink:
        sink.setnchannels(1)
        sink.setsampwidth(2)
        sink.setframerate(fs)
        sink.writeframes((x * 32767).astype("<i2").tobytes())
    return path


class EncoderTests(unittest.TestCase):
    def setUp(self):
        if np is None:
            self.skipTest("numpy is not installed")
        _skip_without_world()

    def test_frames_are_the_declared_size(self):
        import natural_voice_encode as nve

        with TemporaryDirectory() as tmp:
            source = _tone_wav(Path(tmp) / "tone.wav")
            packed = nve.encode_word(source)
        self.assertTrue(packed)
        self.assertEqual(len(packed) % nve.FRAME_STRUCT.size, 0)

    def test_bank_output_satisfies_the_recipe_contract(self):
        """The encoder's output shape and the validator cannot drift."""
        import natural_voice_encode as nve
        from natural_voice_banks import validate_natural_voice_banks

        with TemporaryDirectory() as tmp:
            source = _tone_wav(Path(tmp) / "tone.wav")
            bank = nve.encode_bank([("one", source), ("two", source)])
        result = validate_natural_voice_banks({"customBanks": [bank]})
        self.assertEqual(result["customBanks"][0]["words"], ["one", "two"])
        self.assertEqual(len(result["customBanks"][0]["meanLar"]),
                         nve.LPC_ORDER)

    def test_rejects_a_hop_that_is_not_whole_world_frames(self):
        """Rounding desynchronises frames from the advertised rate and the
        word plays stretched."""
        import natural_voice_encode as nve

        with self.assertRaises(ValueError):
            nve.analyze(np.zeros(nve.ANALYSIS_FS // 2), hop_ms=12.5)

    def test_voicing_gate_only_reduces_and_is_monotonic(self):
        import natural_voice_encode as nve

        gate = nve.voicing_gate(np.linspace(0.0, 1.0, 40))
        self.assertTrue(np.all(gate >= 0.0) and np.all(gate <= 1.0))
        self.assertTrue(np.all(np.diff(gate) >= 0.0))
        self.assertEqual(nve.voicing_gate(np.array([0.2]))[0], 0.0)
        self.assertEqual(nve.voicing_gate(np.array([0.9]))[0], 1.0)

    def test_matches_the_research_encoder(self):
        """This module and research/natural_speech/analyze_world.py are two
        copies of one algorithm; they must agree byte for byte."""
        import natural_voice_encode as nve

        research = next((p for p in (RESEARCH_WORKTREE, RESEARCH)
                         if (p / "analyze_world.py").exists()), None)
        if research is None:
            self.skipTest("the research tree is not available here")
        sys.path.insert(0, str(research))
        try:
            import analyze_world as aw
        finally:
            sys.path.pop(0)

        sample = next(iter(sorted(
            (research / "out" / "ab2" / "kokoro").glob("*.source16k.wav"))),
            None)
        if sample is None:
            self.skipTest("no cached research source audio")

        x = aw.load_wav_16k(sample)
        dec = aw.decimate(aw.world_analyze(x))
        ks, excitation_db, power_db = aw.to_hd_frames(dec)
        lar = aw.k_to_lar(ks)
        v = aw.quant_voicing(aw.band_voicing(dec))
        st, _, voiced = aw.f0_contour_st(dec)
        active = np.where(power_db > -70.0)[0]
        lo = max(0, active[0] - 1) if len(active) else 0
        hi = (min(len(power_db), active[-1] + 2) if len(active)
              else len(power_db))
        expected = bytearray()
        for i in range(lo, hi):
            gain = (0 if power_db[i] <= -70.0
                    else int(np.clip(round((excitation_db[i] + 96.0) / 0.5),
                                     1, 255)))
            nibbles = [int(round(q * 15.0)) for q in v[i]]
            lars = np.clip(np.round(lar[i] / aw.LAR_MAX * 127.0), -127, 127)
            expected += struct.pack(
                "<5B18b", gain,
                int(np.clip(round(st[i] / 0.25), -128, 127)) & 0xFF,
                nibbles[0] | (nibbles[1] << 4),
                nibbles[2] | (nibbles[3] << 4),
                nibbles[4] | ((1 if voiced[i] else 0) << 4),
                *[int(q) for q in lars])

        self.assertEqual(nve.encode_word(sample), bytes(expected),
                         "the builder encoder has drifted from the lab "
                         "original in research/natural_speech")


if __name__ == "__main__":
    unittest.main(verbosity=2)
