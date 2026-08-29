import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GATE = ROOT / (
    "alt_firmwares/plaits_lab_sdk/diagnostics/"
    "wavetable-production-bank-test/build_autonomous.py"
)
DECODER = ROOT / (
    "alt_firmwares/plaits_lab_sdk/diagnostics/"
    "wavetable-production-bank-test/decode_capture.py"
)


def load_gate():
    spec = importlib.util.spec_from_file_location("wavetable_production_gate", GATE)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_decoder():
    spec = importlib.util.spec_from_file_location("wavetable_production_decoder", DECODER)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class WavetableProductionGateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gate = load_gate()
        cls.decoder = load_decoder()
        cls.recipes = cls.gate.recipes()

    def test_mirrored_image_exercises_every_runtime_representation_at_limit(self):
        recipe = self.recipes["mirrored-mixed-8"]
        bank = recipe["resources"]["wavetableBank"]
        self.assertEqual(set(recipe["slots"]), {"wavetable"})
        self.assertTrue(bank["mirrored"])
        self.assertEqual(len(bank["entries"]), 8)
        self.assertEqual(
            {entry["kind"] for entry in bank["entries"]},
            {"factory", "custom"},
        )
        custom = [entry["model"] for entry in bank["entries"] if entry["kind"] == "custom"]
        self.assertIn("native", {model.get("representation", "sampled") for model in custom})
        self.assertIn("sampled", {model.get("representation", "sampled") for model in custom})

    def test_one_way_image_hits_sixteen_without_retaining_a_factory_entry(self):
        bank = self.recipes["one-way-custom-16"]["resources"]["wavetableBank"]
        self.assertFalse(bank["mirrored"])
        self.assertEqual(len(bank["entries"]), 16)
        self.assertTrue(all(entry["kind"] == "custom" for entry in bank["entries"]))
        self.assertEqual(
            sum(entry["model"].get("representation") == "native" for entry in bank["entries"]),
            8,
        )

    def test_shipping_engine_keeps_the_autosweep_disabled_by_default(self):
        source = (ROOT / "plaits/dsp/engine/wavetable_engine.cc").read_text(encoding="utf-8")
        self.assertIn("#define PLAITS_WAVETABLE_PRODUCTION_AUTOSWEEP 0", source)
        self.assertIn("kWavetableAutosweepCycleSamples", source)
        self.assertEqual(self.gate.AUTOSWEEP_DEFINE, "#define PLAITS_WAVETABLE_PRODUCTION_AUTOSWEEP 1")

    def test_decoder_uses_the_midpoint_of_the_unique_twelve_second_gap(self):
        rate = 1000
        cycle = [0] * int(self.decoder.CYCLE * rate)
        for profile in range(self.decoder.PROFILES):
            start = int((self.decoder.LEADER + profile * self.decoder.SLOT + self.decoder.GAP) * rate)
            cycle[start:start + int(self.decoder.WINDOW * rate)] = [6000] * int(self.decoder.WINDOW * rate)
        stream = cycle * 3
        offset = 17 * rate
        capture = stream[offset:offset + int(100 * rate)]
        decoded = self.decoder.find_cycle(capture, rate)
        self.assertLessEqual(abs(decoded - 33 * rate), rate // 10)


if __name__ == "__main__":
    unittest.main()
