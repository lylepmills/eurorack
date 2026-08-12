from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from export_recipe_source import EXPORT_FORMAT, export_recipe_source
from generate_engine_config import DEFAULT_CONFIGURATION


FIXTURES = Path(__file__).parent


class ExportRecipeSourceTest(unittest.TestCase):
    def load(self, name: str = "default_recipe.json") -> dict:
        return json.loads((FIXTURES / name).read_text(encoding="utf-8"))

    def export(self, document: dict, output: Path) -> list[str]:
        return export_recipe_source(
            document,
            output,
            source_revision="a" * 40,
            source_dirty=False,
        )

    def test_exports_bare_recipe_with_build_instructions_and_source_map(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "source"
            files = self.export(self.load(), output)

            self.assertEqual(
                files,
                sorted({
                    "README.md", "build-metadata.json", "build.sh",
                    "engine_config.h", "recipe.json", "selected-models.md",
                    "speech_config.h",
                }),
            )
            metadata = json.loads((output / "build-metadata.json").read_text())
            self.assertEqual(metadata["format"], EXPORT_FORMAT)
            self.assertEqual(metadata["sourceRevision"], "a" * 40)
            self.assertEqual(metadata["toolchain"], "gcc-arm-none-eabi-4.8-2013q4")
            self.assertIn("virtual-analog", metadata["selectedEngineIds"])

            build = (output / "build.sh").read_text()
            self.assertIn('"ENGINE_CONFIG=$export_dir/engine_config.h"', build)
            self.assertIn('"SPEECH_BANKS_ENABLED=0"', build)
            self.assertIn("PLAITS_STEREO_VIRTUAL_ANALOG=0", build)
            self.assertIn("validate_local_build.py", build)
            self.assertNotIn("exec make", build)
            self.assertNotIn("plaits_user_banks.ld", build)

            readme = (output / "README.md").read_text()
            self.assertIn("Hack at your own risk", readme)
            self.assertIn("Rubato Audio is not responsible", readme)
            self.assertIn("Running `make` directly bypasses them", readme)

            sources = (output / "selected-models.md").read_text()
            self.assertIn("Virtual Analog", sources)
            self.assertIn("plaits/dsp/engine/virtual_analog_engine.cc", sources)

    def test_accepts_browser_configuration_envelope(self) -> None:
        document = {
            "format": "rubato-plaits-palette-configuration",
            "version": 1,
            "name": "My `Hacked` Palette",
            "recipe": self.load(),
        }
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "source"
            self.export(document, output)
            readme = (output / "README.md").read_text()
            self.assertIn("Configuration name: `My 'Hacked' Palette`", readme)
            self.assertEqual(
                json.loads((output / "recipe.json").read_text()),
                document["recipe"],
            )

    def test_replaceable_fm_banks_export_the_linker_overlay(self) -> None:
        recipe = self.load()
        recipe["slots"][0] = "dx7-bank-a"
        recipe.update({
            "schemaVersion": 22,
            "preferences": {
                **DEFAULT_CONFIGURATION["preferences"],
                "calibration": False,
                "colorBlindMode": False,
                "replaceableFmBanks": True,
                "syncInput": False,
            },
            "initialOptions": {
                **DEFAULT_CONFIGURATION["initialOptions"],
                "attenuverterMode": "stock",
            },
            "resources": {"chordTables": self._default_chord_tables()},
        })
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            stock_linker = temporary_path / "stock.ld"
            stock_linker.write_text(
                "SECTIONS\n{\n  .text :\n  {\n"
                "    . = ALIGN(16);\n"
                "     _etext = .;\n"
                "     _sidata = _etext;\n"
                "  } >FLASH\n}\n"
            )
            output = temporary_path / "source"
            with patch("export_recipe_source.STOCK_LINKER_SCRIPT", stock_linker):
                files = self.export(recipe, output)
            self.assertIn("plaits_user_banks.ld", files)
            self.assertIn(
                '"LINKER_SCRIPT=$export_dir/plaits_user_banks.ld"',
                (output / "build.sh").read_text(),
            )

    def _default_chord_tables(self) -> list[dict]:
        catalog = json.loads(
            (FIXTURES.parent / "plaits_lab_chord_tables/catalog.json").read_text()
        )
        return catalog["tables"]

    def test_refuses_to_overwrite_a_nonempty_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "source"
            output.mkdir()
            (output / "my-edit.cc").write_text("keep me")
            with self.assertRaisesRegex(ValueError, "not empty"):
                self.export(self.load(), output)
            self.assertEqual((output / "my-edit.cc").read_text(), "keep me")


if __name__ == "__main__":
    unittest.main()
