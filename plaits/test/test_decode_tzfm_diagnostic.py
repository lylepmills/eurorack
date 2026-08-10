import importlib.util
import array
import pathlib
import unittest


TOOL = pathlib.Path(__file__).parents[1] / "tools" / "decode_tzfm_diagnostic.py"
SPEC = importlib.util.spec_from_file_location("decoder", TOOL)
decoder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(decoder)


class DecoderTest(unittest.TestCase):
    @staticmethod
    def group_four_frame():
        values = []
        for engine in range(5):
            values.append(5000.0 + 50.0 * engine)
            values.extend(2500.0 + engine * 10.0 + field
                          for field in range(10))
        return [6000.0, 6180.0, 6205.0] + values + [6500.0]

    def test_decodes_last_complete_frame(self):
        group = 4
        frame = self.group_four_frame()
        decoded_group, results = decoder.decode_frequencies(
            [777.0] + frame[:-1] + [888.0] + frame
        )
        self.assertEqual(decoded_group, group)
        self.assertEqual(len(results), 5)
        self.assertEqual(results[2]["name"], "VOSIM")
        self.assertEqual(results[2]["slow_peak"], 20)
        self.assertEqual(results[2]["excess_lag"], 29)

    def test_square_wave_recording_round_trip(self):
        rate = 16000
        samples = array.array("i", [0] * rate)
        for frequency in self.group_four_frame():
            tone_samples = int(rate * 0.42)
            for i in range(tone_samples):
                phase = (i * frequency / rate) % 1.0
                samples.append(12000 if phase < 0.5 else -12000)
            samples.extend([0] * int(rate * 0.12))
        runs = decoder.active_runs(samples, rate)
        frequencies = [decoder.estimate_frequency(samples, rate, run)
                       for run in runs]
        group, results = decoder.decode_frequencies(frequencies)
        self.assertEqual(group, 4)
        self.assertEqual(len(results), 5)
        self.assertEqual(results[-1]["name"], "Swarm")

    def test_rejects_incomplete_frame(self):
        with self.assertRaisesRegex(ValueError, "no complete"):
            decoder.decode_frequencies([6000.0, 6180.0, 6205.0])


if __name__ == "__main__":
    unittest.main()
