from __future__ import annotations

import importlib.util
import io
import json
import shutil
import tempfile
import unittest
import zipfile
from contextlib import redirect_stdout
from pathlib import Path
from types import SimpleNamespace


SDK_DIR = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("plaits_lab", SDK_DIR / "plaits_lab.py")
assert SPEC and SPEC.loader
plaits_lab = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(plaits_lab)


class PackageTests(unittest.TestCase):
    def test_reference_packages_validate(self) -> None:
        packages = [
            SDK_DIR / "packages" / "mutable-instruments" / "virtual-analog",
            SDK_DIR / "packages" / "rubato" / "pulsar",
        ]
        for package in packages:
            with self.subTest(package=package):
                loaded = plaits_lab.load_package(str(package))
                self.assertEqual(loaded["manifest"]["schemaVersion"], 1)
                self.assertEqual(len(loaded["manifest"]["controls"]), 4)
                self.assertTrue(loaded["source_files"])

    def test_scenario_rejects_out_of_range_controls(self) -> None:
        scenario = {
            "id": "bad",
            "name": "Bad",
            "durationSeconds": 1,
            "note": 60,
            "triggerHz": 0,
            "controls": {
                "harmonics": [0, 2],
                "timbre": [0, 1],
                "morph": [0, 1],
                "macro": [0, 1],
            },
        }
        with self.assertRaises(plaits_lab.PackageError):
            plaits_lab.validate_scenario(scenario, 0)

    def test_path_escape_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            base = Path(temp_dir)
            with self.assertRaises(plaits_lab.PackageError):
                plaits_lab.resolve_within(base, "../outside.cc", "test path")

    def test_community_policy_rejects_hardware_access(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "unsafe.cc"
            source.write_text("void Render() { HAL_GPIO_WritePin(); }\n", encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError):
                plaits_lab.validate_community_source([source])

    def test_authoritative_catalog_exposes_every_forkable_model(self) -> None:
        catalog, public = plaits_lab.load_builtin_catalog()
        self.assertEqual(len(catalog), 39)
        self.assertEqual(set(catalog), set(public))
        self.assertTrue(all(item["digest"].startswith("sha256:") for item in public.values()))

    def test_every_catalog_model_can_be_forked(self) -> None:
        catalog, _ = plaits_lab.load_builtin_catalog()
        with tempfile.TemporaryDirectory() as temp_dir:
            for engine_id in catalog:
                with self.subTest(engine=engine_id):
                    slug = f"{engine_id}-fork"
                    output = Path(temp_dir) / slug
                    with redirect_stdout(io.StringIO()):
                        plaits_lab.init_command(SimpleNamespace(
                            output=str(output), from_engine=engine_id,
                            author="Test Author", package_id=f"test-author/{slug}",
                            slug=slug, name=None,
                        ))
                    loaded = plaits_lab.load_package(str(output))
                    self.assertEqual(loaded["manifest"]["forkedFrom"], engine_id)

    def test_source_policy_ignores_comments_but_not_code(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "comments.cc"
            source.write_text(
                "// a new capture window\n/* delete old notes */\nvoid Render() {}\n",
                encoding="utf-8",
            )
            plaits_lab.validate_community_source([source])
            source.write_text("void Render() { delete pointer; }\n", encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError):
                plaits_lab.validate_community_source([source])

    @unittest.skipUnless(shutil.which("c++") or shutil.which("g++"), "host C++ compiler required")
    def test_blank_package_can_be_validated_and_bundled(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package = Path(temp_dir) / "bright-wave"
            plaits_lab.init_command(SimpleNamespace(
                output=str(package), from_engine="blank", author="Test Author",
                package_id="test-author/bright-wave", slug="bright-wave", name="Bright Wave",
            ))
            loaded = plaits_lab.load_package(str(package))
            self.assertEqual(loaded["manifest"]["packageType"], "community")

            bundle = Path(temp_dir) / "bright-wave.zip"
            plaits_lab.submit_command(SimpleNamespace(
                package=str(package), output=str(bundle), compiler=None,
            ))
            with zipfile.ZipFile(bundle) as archive:
                submission = json.loads(archive.read("submission.json"))
                self.assertEqual(submission["state"], "draft")
                self.assertEqual(set(submission["audioAnalysis"]), {"hero", "triggered"})
                self.assertIn("package/src/bright-wave_engine.cc", archive.namelist())


if __name__ == "__main__":
    unittest.main()
