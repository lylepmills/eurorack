from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

from generate_engine_config import DEFAULT_CHORD_TABLES, DEFAULT_CONFIGURATION
from render_manual import CONTROL_IDS, manual_document, render_pdf


FIXTURES = Path(__file__).parent
HAS_REPORTLAB = importlib.util.find_spec("reportlab") is not None


class RenderManualTest(unittest.TestCase):
    def load(self, name: str) -> dict:
        return json.loads((FIXTURES / name).read_text(encoding="utf-8"))

    def fourth_bank_recipe(self) -> dict:
        return {
            "schemaVersion": 6,
            "target": "mutable-instruments-plaits",
            "firmware": "rubato-plaits",
            "output": "audio-wav",
            "slots": self.load("default_recipe.json")["slots"] + [
                "loopback", "lockstep", "tapfield", "phase-weave",
                "sideband-bank", "attractor", "undertow", "reed-pipe",
            ],
            "preferences": dict(DEFAULT_CONFIGURATION["preferences"]),
            "initialOptions": dict(DEFAULT_CONFIGURATION["initialOptions"]),
            "resources": {
                "chordTables": [dict(table) for table in DEFAULT_CHORD_TABLES],
                "userDataBanks": [],
            },
        }

    def test_bank_positions_use_public_green_red_amber_order(self) -> None:
        document = manual_document(self.load("audition_recipe.json"))
        self.assertEqual(document["slots"][0]["position"], {
            "bank": "green", "bankName": "GREEN", "color": "#4F9868", "number": 1,
        })
        self.assertEqual(document["slots"][8]["position"]["bank"], "red")
        self.assertEqual(document["slots"][16]["position"]["bank"], "amber")
        self.assertEqual(document["slots"][23]["position"]["number"], 8)

    def test_model_reference_deduplicates_engines_and_keeps_every_location(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["slots"][1] = recipe["slots"][0]
        document = manual_document(recipe)
        first = document["models"][0]
        self.assertEqual(first["id"], recipe["slots"][0])
        self.assertEqual([(item["bank"], item["number"]) for item in first["locations"]], [
            ("green", 1), ("green", 2),
        ])
        self.assertEqual(len(document["models"]), len(set(recipe["slots"])))

    def test_every_selected_model_has_complete_manual_prose(self) -> None:
        document = manual_document(self.load("audition_recipe.json"))
        self.assertEqual(len(document["models"]), 24)
        for model in document["models"]:
            self.assertEqual(tuple(model["manual"]["controls"]), CONTROL_IDS)
            self.assertTrue(model["manual"]["trigger"])
            self.assertTrue(model["documentationDigest"].startswith("sha256:"))

    def test_fourth_bank_positions_land_in_the_orange_bank(self) -> None:
        document = manual_document(self.fourth_bank_recipe())
        self.assertEqual(len(document["slots"]), 32)
        self.assertEqual(document["slots"][24]["position"], {
            "bank": "orange", "bankName": "ORANGE", "color": "#D96F35", "number": 1,
        })
        self.assertEqual(document["slots"][31]["position"]["number"], 8)
        self.assertEqual(document["slots"][23]["position"]["bank"], "amber")

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_fourth_bank_pdf_renders(self) -> None:
        document = manual_document(self.fourth_bank_recipe())
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "fourth-bank.pdf"
            render_pdf(document, output)
            self.assertTrue(output.read_bytes().startswith(b"%PDF-"))

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_pdf_render_is_deterministic(self) -> None:
        document = manual_document(self.load("audition_recipe.json"))
        with tempfile.TemporaryDirectory() as temp_dir:
            first = Path(temp_dir) / "first.pdf"
            second = Path(temp_dir) / "second.pdf"
            render_pdf(document, first)
            render_pdf(document, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertTrue(first.read_bytes().startswith(b"%PDF-"))


if __name__ == "__main__":
    unittest.main()


class ContainerManualEndpointTest(unittest.TestCase):
    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_render_manual_bytes_is_deterministic_and_pdf(self) -> None:
        import container_server

        recipe = json.loads((FIXTURES / "audition_recipe.json").read_text(encoding="utf-8"))
        key = "ab" * 32
        first = container_server.render_manual_bytes(key, recipe)
        second = container_server.render_manual_bytes(key, recipe)
        self.assertEqual(first, second)
        self.assertTrue(first.startswith(b"%PDF-"))

    def test_render_manual_bytes_rejects_invalid_recipes(self) -> None:
        import container_server

        with self.assertRaises(ValueError):
            container_server.render_manual_bytes("ab" * 32, {"slots": []})
