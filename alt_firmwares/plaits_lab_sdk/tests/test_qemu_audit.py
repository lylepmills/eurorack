import sys
import unittest
from pathlib import Path

QEMU_DIR = Path(__file__).resolve().parents[1] / "qemu"
sys.path.insert(0, str(QEMU_DIR))

import audit_catalog
import estimate


class AuditCatalogTest(unittest.TestCase):
    def test_extreme_sweep_covers_axes_pitch_and_every_control_pair(self):
        positions = estimate.sweep_positions("extreme", "chords")
        by_name = {position[0]: position for position in positions}
        for name in (
            "harm-low", "harm-high", "timbre-low", "timbre-high",
            "morph-low", "morph-high", "macro-low", "macro-high",
            "note-low", "note-high",
        ):
            self.assertIn(name, by_name)

        corners = [position[1:5] for position in positions
                   if position[0].startswith("corner-")]
        self.assertEqual(len(corners), 8)
        for first in range(4):
            for second in range(first + 1, 4):
                combinations = {(row[first], row[second]) for row in corners}
                self.assertEqual(len(combinations), 4)

    def test_engine_specific_transition_is_added_once(self):
        names = [position[0] for position in
                 estimate.sweep_positions("extreme", "wavetable-chord")]
        self.assertEqual(names.count("harm-six"), 1)

    def test_six_op_sweep_covers_every_program_at_high_macro(self):
        positions = estimate.sweep_positions("extreme", "dx7-bank-c")
        programs = [position for position in positions
                    if position[0].startswith("program-")]
        self.assertEqual(len(programs), 32)
        self.assertEqual(programs[0][0], "program-01-macro-high")
        self.assertEqual(programs[-1][0], "program-32-macro-high")
        self.assertTrue(all(position[4] == 0.999 for position in programs))

    def test_terrain_equation_sweep_covers_every_diagnostic_case(self):
        positions = estimate.sweep_positions("extreme", "terrain-equation-bench")
        cases = [position for position in positions
                 if position[0].startswith("case-")]
        self.assertEqual(len(cases), 19)
        self.assertEqual(cases[0][0], "case-00-original-terrain-1")
        self.assertEqual(cases[-1][0], "case-18-theta-mu-field")
        self.assertAlmostEqual(cases[9][1], 0.5)

    def test_wavetable_equation_sweep_covers_every_diagnostic_case(self):
        positions = estimate.sweep_positions(
            "extreme", "wavetable-equation-bench")
        cases = [position for position in positions
                 if position[0].startswith("case-")]
        self.assertEqual(len(cases), 17)
        self.assertEqual(cases[0][0], "case-00-sampled-bank-4kb")
        self.assertEqual(cases[-1][0], "case-16-eight-sine-stress")
        self.assertAlmostEqual(cases[8][1], 0.5)

    def test_every_current_catalog_model_advertises_stereo(self):
        ids = audit_catalog.stereo_catalog_ids()
        self.assertEqual(len(ids), 92)
        self.assertIn("chords", ids)

    def test_risk_requires_hardware_on_midpoint_or_upper_band(self):
        def result(mid, high):
            return {"estimate": {"usage": mid, "usage_high": high}}

        self.assertEqual(audit_catalog.risk(result(0.60, 0.80)), "OK")
        self.assertEqual(audit_catalog.risk(result(0.86, 0.90)), "HARDWARE")
        self.assertEqual(audit_catalog.risk(result(0.70, 1.01)), "HARDWARE")
        self.assertEqual(audit_catalog.risk(result(1.01, 1.20)), "FAIL")

    def test_estimate_command_exercises_both_trigger_states(self):
        command = audit_catalog.estimate_command("chords", "extreme", "image", 200, 400)
        self.assertIn("--stereo", command)
        self.assertEqual(command[command.index("--trigger") + 1], "both")
        self.assertEqual(command[command.index("--sweep") + 1], "extreme")
        self.assertIn("--json", command)


if __name__ == "__main__":
    unittest.main()
