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

    @staticmethod
    def group_eight_frame():
        values = []
        for engine in range(8):
            values.append(5000.0 + 50.0 * engine)
            values.extend(2500.0 + engine * 10.0 + field
                          for field in range(10))
        return [6000.0, 6260.0, 6208.0] + values + [6500.0]

    @staticmethod
    def group_nine_frame():
        values = []
        for engine in range(8):
            values.append(5000.0 + 50.0 * engine)
            values.extend(2500.0 + engine * 10.0 + field
                          for field in range(10))
        return [6000.0, 6280.0, 6208.0] + values + [6500.0]

    @staticmethod
    def group_ten_frame():
        values = []
        for engine in range(8):
            values.append(5000.0 + 50.0 * engine)
            values.extend(2500.0 + engine * 10.0 + field
                          for field in range(10))
        return [6000.0, 6300.0, 6208.0] + values + [6500.0]

    @staticmethod
    def group_eleven_frame():
        values = []
        for engine in range(8):
            values.append(5000.0 + 50.0 * engine)
            values.extend(2500.0 + engine * 10.0 + field
                          for field in range(10))
        return [6000.0, 6320.0, 6208.0] + values + [6500.0]

    @staticmethod
    def group_twelve_frame():
        values = []
        for engine in range(8):
            values.append(5000.0 + 50.0 * engine)
            values.extend(2500.0 + engine * 10.0 + field
                          for field in range(10))
        return [6000.0, 6340.0, 6208.0] + values + [6500.0]

    @staticmethod
    def group_thirteen_frame():
        values = []
        for engine in range(9):
            values.append(5000.0 + 50.0 * engine)
            values.extend(2500.0 + engine * 10.0 + field
                          for field in range(10))
        return [6000.0, 6360.0, 6209.0] + values + [6500.0]

    @staticmethod
    def group_fourteen_frame():
        values = []
        for engine in range(9):
            values.append(5000.0 + 50.0 * engine)
            values.extend(2500.0 + engine * 10.0 + field
                          for field in range(10))
        return [6000.0, 6380.0, 6209.0] + values + [6500.0]

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

    def test_decodes_fast_exponential_group(self):
        group, results = decoder.decode_frequencies(self.group_eight_frame())
        self.assertEqual(group, 8)
        self.assertEqual(len(results), 8)
        self.assertEqual(results[0]["name"], "CSaw")
        self.assertEqual(results[-1]["name"], "Bytebeat")

    def test_decodes_second_fast_exponential_group(self):
        group, results = decoder.decode_frequencies(self.group_nine_frame())
        self.assertEqual(group, 9)
        self.assertEqual(len(results), 8)
        self.assertEqual(results[0]["name"], "Glisson")
        self.assertEqual(results[-1]["name"], "Freshets Formant")

    def test_decodes_third_fast_exponential_group(self):
        group, results = decoder.decode_frequencies(self.group_ten_frame())
        self.assertEqual(group, 10)
        self.assertEqual(len(results), 8)
        self.assertEqual(results[0]["name"], "Undertow")
        self.assertEqual(results[-1]["name"], "Blown")

    def test_decodes_fourth_fast_exponential_group(self):
        group, results = decoder.decode_frequencies(self.group_eleven_frame())
        self.assertEqual(group, 11)
        self.assertEqual(len(results), 8)
        self.assertEqual(results[0]["name"], "Modal Resonator")
        self.assertEqual(results[-1]["name"], "String Machine")

    def test_decodes_fifth_fast_exponential_group(self):
        group, results = decoder.decode_frequencies(self.group_twelve_frame())
        self.assertEqual(group, 12)
        self.assertEqual(len(results), 8)
        self.assertEqual(results[0]["name"], "Struck Bell")
        self.assertEqual(results[-1]["name"], "Bowed")

    def test_decodes_sixth_fast_exponential_group(self):
        group, results = decoder.decode_frequencies(
            self.group_thirteen_frame())
        self.assertEqual(group, 13)
        self.assertEqual(len(results), 9)
        self.assertEqual(results[0]["name"], "Granular Formant")
        self.assertEqual(results[-1]["name"], "Chiptune")

    def test_decodes_final_fast_exponential_group(self):
        group, results = decoder.decode_frequencies(
            self.group_fourteen_frame())
        self.assertEqual(group, 14)
        self.assertEqual(len(results), 9)
        self.assertEqual(results[0]["name"], "Saw Comb")
        self.assertEqual(results[-1]["name"], "Clap")

    def test_rejects_incomplete_frame(self):
        with self.assertRaisesRegex(ValueError, "no complete"):
            decoder.decode_frequencies([6000.0, 6180.0, 6205.0])

    def test_corrects_recorder_clock_error_and_uses_repeated_frames(self):
        frame = self.group_four_frame()
        # Model a recorder whose clock makes every reported frequency read
        # about 0.3 percent low, plus a small fixed estimator bias.
        recorded = [(frequency - 1.75) / 1.003 for frequency in frame]
        noisy = recorded.copy()
        noisy[3 + 2 * 11 + 1] += 3.0
        group, results = decoder.decode_frequencies(noisy + recorded + recorded)
        self.assertEqual(group, 4)
        self.assertEqual(results[2]["name"], "VOSIM")
        self.assertEqual(results[2]["slow_peak"], 20)
        self.assertEqual(results[2]["excess_lag"], 29)

    def test_verdict_ignores_frequency_decoder_counter_floor(self):
        result = {
            "slow_peak": 600,
            "fast_peak": 700,
            "slow_over90": 0,
            "fast_over90": 0,
            "slow_deadline": 0,
            "fast_deadline": 0,
            "overruns": 1,
            "resyncs": decoder.COUNTER_NOISE_FLOOR,
            "underflows": 0,
            "excess_lag": 2,
        }
        self.assertEqual(decoder.verdict(result), "PASS")
        result["resyncs"] += 1
        self.assertEqual(decoder.verdict(result), "FAIL FM transport")


if __name__ == "__main__":
    unittest.main()
