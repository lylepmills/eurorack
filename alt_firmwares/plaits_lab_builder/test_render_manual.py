from __future__ import annotations

import base64
import importlib.util
import json
import re
import tempfile
import unittest
import zlib
from pathlib import Path

from generate_engine_config import DEFAULT_CHORD_TABLES, DEFAULT_CONFIGURATION
from render_manual import CONTROL_IDS, _clip, display_name, manual_document, render_pdf


FIXTURES = Path(__file__).parent
HAS_REPORTLAB = importlib.util.find_spec("reportlab") is not None


def pdf_strings(path: Path) -> str:
    """Every literal string drawn into the PDF, concatenated. ReportLab writes
    its content streams ASCII85 + Flate encoded, so they're inflated first. Good
    enough to assert WHAT the guide prints (not where), with no PDF-parser
    dependency in the builder image."""
    raw = path.read_bytes()
    streams = []
    for match in re.finditer(rb"stream\r?\n(.*?)endstream", raw, re.S):
        try:
            streams.append(zlib.decompress(base64.a85decode(match.group(1), adobe=True)).decode("latin-1"))
        except Exception:
            continue
    return "".join(re.findall(r"\((?:\\.|[^\\()])*\)", "\n".join(streams)))


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

    def short_bank_recipe(self) -> dict:
        # v7 short bank: green full (8), red partly empty (3 + 5 empty), amber
        # full (8). The Worker normalizes filled slots to bare IDs interleaved
        # with None, which is what the container renderer receives.
        full = self.load("default_recipe.json")["slots"]
        slots = full[:8] + full[8:11] + [None] * 5 + full[16:24]
        return {
            "schemaVersion": 7,
            "target": "mutable-instruments-plaits",
            "firmware": "rubato-plaits",
            "output": "audio-wav",
            "slots": slots,
            "preferences": dict(DEFAULT_CONFIGURATION["preferences"]),
            "initialOptions": dict(DEFAULT_CONFIGURATION["initialOptions"]),
            "resources": {"chordTables": [dict(table) for table in DEFAULT_CHORD_TABLES]},
        }

    def test_v7_short_bank_skips_empty_slots(self) -> None:
        # Empty slots must not crash the manual (was KeyError: None, which failed
        # the whole render and hid the field-guide download link).
        document = manual_document(self.short_bank_recipe())
        self.assertEqual(len(document["slots"]), 24)
        self.assertIsNone(document["slots"][11]["engine"])  # first empty red slot
        self.assertEqual(sum(1 for s in document["slots"] if s["engine"] is None), 5)
        # Empty slots never become model references.
        self.assertTrue(all(model["id"] for model in document["models"]))

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_v7_short_bank_pdf_renders(self) -> None:
        document = manual_document(self.short_bank_recipe())
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "short-bank.pdf"
            render_pdf(document, output)
            self.assertTrue(output.read_bytes().startswith(b"%PDF-"))

    def calibration_recipe(self, calibration: bool) -> dict:
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 14
        recipe["preferences"] = {"navigationMode": "linear", "calibration": calibration}
        recipe["initialOptions"] = dict(DEFAULT_CONFIGURATION["initialOptions"])
        recipe["resources"] = {"chordTables": DEFAULT_CHORD_TABLES}
        return recipe

    def test_calibration_page_follows_the_build(self) -> None:
        self.assertIs(manual_document(self.calibration_recipe(True))["calibration"], True)
        self.assertIs(manual_document(self.calibration_recipe(False))["calibration"], False)
        # A recipe from before v14 has no such key and no such page.
        self.assertIs(manual_document(self.load("default_recipe.json"))["calibration"], False)

    def roved_recipe(self, calibration: bool = False) -> dict:
        recipe = self.calibration_recipe(calibration)
        recipe["schemaVersion"] = 15
        recipe["target"] = "plum-audio-roved"
        return recipe

    def test_manual_document_carries_the_hardware_target(self) -> None:
        self.assertEqual(
            manual_document(self.roved_recipe())["target"],
            "plum-audio-roved",
        )
        self.assertEqual(
            manual_document(self.calibration_recipe(False))["target"],
            "mutable-instruments-plaits",
        )

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_calibration_procedure_is_printed_only_when_the_build_has_it(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            on = Path(temp_dir) / "calibration-on.pdf"
            render_pdf(manual_document(self.calibration_recipe(True)), on)
            printed = pdf_strings(on)
            self.assertIn("CALIBRATION", printed)
            self.assertIn("RIGHT model button while powering", printed)

            off = Path(temp_dir) / "calibration-off.pdf"
            render_pdf(manual_document(self.calibration_recipe(False)), off)
            # Not a word about a gesture this firmware does not answer.
            self.assertNotIn("CALIBRATION", pdf_strings(off))

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_roved_guide_prints_clickable_knob_instructions(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "roved.pdf"
            render_pdf(manual_document(self.roved_recipe(True)), path)
            printed = pdf_strings(path)
            self.assertIn("Your Ro'Ved Field Guide", printed)
            self.assertIn("TIMBRE + FREQUENCY", printed)
            self.assertIn("FREQUENCY/TIMBRE for previous/next bank", printed)
            self.assertIn("MORPH/HARMONICS for previous/next", printed)
            self.assertIn("FREQUENCY/TIMBRE for the previous/next light", printed)
            self.assertIn("MORPH/HARMONICS for the previous/next setting", printed)
            self.assertIn("keep holding it", printed)
            self.assertIn("Hold the MORPH knob down while powering", printed)
            self.assertNotIn("RIGHT model button while powering", printed)

    def color_blind_recipe(self, enabled: bool, calibration: bool = False) -> dict:
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 15
        recipe["preferences"] = {
            "navigationMode": "linear",
            "calibration": calibration,
            "colorBlindMode": enabled,
        }
        recipe["initialOptions"] = dict(DEFAULT_CONFIGURATION["initialOptions"])
        recipe["resources"] = {"chordTables": DEFAULT_CHORD_TABLES}
        return recipe

    def test_color_blind_bank_note_follows_the_build(self) -> None:
        document = manual_document(self.color_blind_recipe(True))
        self.assertIs(document["colorBlindMode"], True)
        self.assertEqual(
            [document["slots"][index * 8]["position"]["bankName"] for index in range(3)],
            ["BRIGHTEST", "BRIGHT", "DIM"],
        )
        self.assertEqual(
            [document["slots"][index * 8]["position"]["brightness"] for index in range(3)],
            ["100%", "50%", "25%"],
        )
        four_bank_recipe = self.color_blind_recipe(True)
        four_bank_recipe["slots"] += ["virtual-analog"] * 8
        fourth = manual_document(four_bank_recipe)["slots"][24]["position"]
        self.assertEqual((fourth["bankName"], fourth["brightness"]), ("DIMMEST", "12.5%"))
        self.assertIs(manual_document(self.color_blind_recipe(False))["colorBlindMode"], False)
        self.assertIs(manual_document(self.load("default_recipe.json"))["colorBlindMode"], False)

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_color_blind_brightness_map_is_printed_only_when_enabled(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            on = Path(temp_dir) / "color-blind-on.pdf"
            render_pdf(manual_document(self.color_blind_recipe(True)), on)
            printed = pdf_strings(on)
            joined = printed.replace(")(", " ")
            self.assertIn("BANK LIGHTS", printed)
            self.assertIn("BRIGHTEST is 100%", joined)
            self.assertIn("DIMMEST is 12.5%", joined)
            self.assertIn(r"Medium \(50%\)", printed)
            for color_name in ("GREEN", "RED", "YELLOW", "AMBER", "ORANGE"):
                self.assertNotIn(color_name, printed)

            off = Path(temp_dir) / "color-blind-off.pdf"
            render_pdf(manual_document(self.color_blind_recipe(False)), off)
            self.assertNotIn("BANK LIGHTS", pdf_strings(off))

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_color_blind_calibration_uses_brightness_language(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "color-blind-calibration.pdf"
            render_pdf(manual_document(self.color_blind_recipe(True, calibration=True)), output)
            printed = pdf_strings(output)
            self.assertIn("pulses at the brightest level", printed)
            self.assertIn("light becomes dim", printed)
            self.assertNotIn("pulses green", printed)
            self.assertNotIn("turns yellow", printed)
            self.assertNotIn("flashes red", printed)

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

    def bank_document(self, name: str = "Warm Keys", patches: int = 32) -> dict:
        return {
            "id": "warm-keys", "packageId": "anon/warm-keys", "version": "1.0.0",
            "digest": None, "name": name, "author": "T. Contributor", "license": "CC0-1.0",
            "origin": "Community", "description": "Eight electric pianos and their ghosts.",
            "voices": [
                {"name": f"V{i}", "algorithm": 1, "packed": [0] * 128} for i in range(patches)
            ],
        }

    def slot_bank_recipe(self, banks: list[dict], schema_version: int = 12) -> dict:
        # mixed_recipe.json places the three six-op banks at slots 16, 18, and 20.
        recipe = self.load("mixed_recipe.json")
        recipe.update(
            schemaVersion=schema_version,
            preferences=dict(DEFAULT_CONFIGURATION["preferences"]),
            initialOptions=dict(DEFAULT_CONFIGURATION["initialOptions"]),
            resources={
                "chordTables": [dict(table) for table in DEFAULT_CHORD_TABLES],
                "userDataBanks": banks,
            },
        )
        return recipe

    def test_customized_slot_carries_its_bank_credit(self) -> None:
        recipe = self.slot_bank_recipe([{"slot": 16, "bank": self.bank_document()}])
        document = manual_document(recipe)
        self.assertEqual(document["slots"][16]["customBank"], {
            "name": "Warm Keys", "author": "T. Contributor", "origin": "Community",
            "description": "Eight electric pianos and their ghosts.", "patches": 32,
        })
        # An untouched FM slot keeps the factory bank, so it carries no credit.
        self.assertIsNone(document["slots"][18]["customBank"])
        self.assertIsNone(document["slots"][0]["customBank"])
        customized = [model for model in document["models"] if model["customBank"]]
        self.assertEqual([model["id"] for model in customized], ["dx7-bank-a"])

    def test_a_short_bank_reports_its_real_patch_count(self) -> None:
        recipe = self.slot_bank_recipe(
            [{"slot": 16, "bank": self.bank_document(patches=6)}],
            schema_version=13,
        )
        document = manual_document(recipe)
        self.assertEqual(document["slots"][16]["customBank"]["patches"], 6)

    def test_two_placements_of_one_engine_split_by_their_banks(self) -> None:
        # v12's defining case: the same FM engine twice, each slot with its own
        # bank. Merging them into one reference would credit the wrong bank for
        # one of the two locations.
        recipe = self.slot_bank_recipe([
            {"slot": 16, "bank": self.bank_document(name="Warm Keys")},
            {"slot": 18, "bank": self.bank_document(name="Cold Bells")},
        ])
        recipe["slots"][18] = "dx7-bank-a"
        document = manual_document(recipe)
        entries = [model for model in document["models"] if model["id"] == "dx7-bank-a"]
        self.assertEqual(
            [(model["customBank"]["name"], model["locations"]) for model in entries],
            [
                ("Warm Keys", [{"bank": "amber", "bankName": "AMBER", "color": "#D59635", "number": 1}]),
                ("Cold Bells", [{"bank": "amber", "bankName": "AMBER", "color": "#D59635", "number": 3}]),
            ],
        )

    def test_two_placements_sharing_one_bank_stay_a_single_reference(self) -> None:
        recipe = self.slot_bank_recipe([
            {"slot": 16, "bank": self.bank_document()},
            {"slot": 18, "bank": self.bank_document()},
        ])
        recipe["slots"][18] = "dx7-bank-a"
        document = manual_document(recipe)
        entries = [model for model in document["models"] if model["id"] == "dx7-bank-a"]
        self.assertEqual(len(entries), 1)
        self.assertEqual([item["number"] for item in entries[0]["locations"]], [1, 3])

    def test_a_v6_index_keyed_bank_credits_every_slot_playing_it(self) -> None:
        # v6 overrides a FACTORY bank, so the credit lands on every placement of
        # the engine that reads that bank — here both dx7-bank-a slots.
        recipe = self.slot_bank_recipe(
            [{"index": 0, "bank": self.bank_document()}], schema_version=6,
        )
        recipe["slots"][18] = "dx7-bank-a"
        document = manual_document(recipe)
        self.assertEqual(document["slots"][16]["customBank"]["name"], "Warm Keys")
        self.assertEqual(document["slots"][18]["customBank"]["name"], "Warm Keys")
        # dx7-bank-c reads factory bank 2, which this recipe left alone.
        self.assertIsNone(document["slots"][20]["customBank"])

    def test_a_customized_slot_is_named_for_its_custom_bank(self) -> None:
        # The factory A/B/C letter describes patches the slot no longer plays.
        self.assertEqual(display_name("6-Op FM Bank A", None), "6-Op FM Bank A")
        self.assertEqual(
            display_name("6-Op FM Bank A", {"name": "Warm Keys"}), "Custom 6-Op FM Bank",
        )

    def test_a_bank_name_too_wide_for_the_slot_row_is_ellipsized(self) -> None:
        # The bank map's rows are a fixed height, so an over-long subtitle has to
        # lose its tail rather than wrap into the row below.
        self.assertEqual(_clip("Warm Keys", "Helvetica", 6.4, 200), "Warm Keys")
        clipped = _clip("An Extremely Long Custom Bank Name That Runs On", "Helvetica", 6.4, 60)
        self.assertTrue(clipped.endswith("…"))
        self.assertLess(len(clipped), len("An Extremely Long Custom Bank Name That Runs On"))
        self.assertEqual(_clip("", "Helvetica", 6.4, 60), "")

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_the_bank_map_prints_the_custom_bank_label_and_name(self) -> None:
        # The regression: page 1 used to list a customized six-op slot under its
        # factory letter, so the map disagreed with the slot's own reference page.
        document = manual_document(self.slot_bank_recipe([{"slot": 16, "bank": self.bank_document()}]))
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "manual.pdf"
            render_pdf(document, path)
            printed = pdf_strings(path)
        # Bank map and model-reference title both, so the two agree.
        self.assertEqual(printed.count("Custom 6-Op FM Bank"), 2)
        # The bank's own name rides along as the map's subtitle (the second
        # occurrence is the reference page's credit row).
        self.assertEqual(printed.count("Warm Keys"), 2)
        self.assertNotIn("6-Op FM Bank A", printed)
        # A six-op slot the recipe left alone keeps its factory letter.
        self.assertIn("6-Op FM Bank B", printed)

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_an_unnamed_custom_bank_prints_the_label_alone(self) -> None:
        document = manual_document(self.slot_bank_recipe([{"slot": 16, "bank": self.bank_document(name="")}]))
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "manual.pdf"
            render_pdf(document, path)
            printed = pdf_strings(path)
        self.assertEqual(printed.count("Custom 6-Op FM Bank"), 2)
        self.assertNotIn("6-Op FM Bank A", printed)

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_custom_bank_pdf_renders_deterministically(self) -> None:
        document = manual_document(self.slot_bank_recipe([
            {"slot": 16, "bank": self.bank_document()},
            {"slot": 18, "bank": self.bank_document(name="")},
        ]))
        with tempfile.TemporaryDirectory() as temp_dir:
            first = Path(temp_dir) / "first.pdf"
            second = Path(temp_dir) / "second.pdf"
            render_pdf(document, first)
            render_pdf(document, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertTrue(first.read_bytes().startswith(b"%PDF-"))

    def nine_table_recipe(self) -> dict:
        base = DEFAULT_CHORD_TABLES[0]
        tables = [dict(t) for t in DEFAULT_CHORD_TABLES]
        for n in range(9 - len(tables)):
            table = json.loads(json.dumps(base))
            table.update(
                id=f"filler-{n}", packageId=f"local/filler-{n}", version="draft",
                digest=None, name=f"Filler {n}", origin="Local",
            )
            tables.append(table)
        recipe = self.load("default_recipe.json")
        recipe.update(
            schemaVersion=8,
            preferences=dict(DEFAULT_CONFIGURATION["preferences"]),
            initialOptions=dict(DEFAULT_CONFIGURATION["initialOptions"]),
            resources={"chordTables": tables},
        )
        return recipe

    def test_nine_chord_tables_list_in_the_document(self) -> None:
        document = manual_document(self.nine_table_recipe())
        self.assertEqual(len(document["chordTables"]), 9)

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_nine_chord_tables_pdf_renders(self) -> None:
        # The options-menu table indexes LED_STATES by chord-table position, so
        # the 7th-9th tables must reach the fast-blink tier without an IndexError.
        document = manual_document(self.nine_table_recipe())
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "nine-tables.pdf"
            render_pdf(document, output)
            self.assertTrue(output.read_bytes().startswith(b"%PDF-"))


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


class ManualEndpointHeaderTest(unittest.TestCase):
    """Drive the real HTTP handler on a loopback port. The endpoint had no
    request-level test, which is how X-Plaits-Manual-Contract went on reporting
    the image's env default ("1") long after the Worker had moved to 5."""

    def post_manual(self, body: dict) -> tuple[int, dict[str, str], bytes]:
        import json as json_module
        import threading
        import urllib.error
        import urllib.request
        from http.server import ThreadingHTTPServer

        import container_server

        server = ThreadingHTTPServer(("127.0.0.1", 0), container_server.Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            request = urllib.request.Request(
                f"http://127.0.0.1:{server.server_address[1]}/manual",
                data=json_module.dumps(body).encode("utf-8"),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            try:
                with urllib.request.urlopen(request, timeout=60) as response:
                    return response.status, dict(response.headers), response.read()
            except urllib.error.HTTPError as error:
                return error.code, dict(error.headers), error.read()
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)

    def recipe(self) -> dict:
        return json.loads((FIXTURES / "audition_recipe.json").read_text(encoding="utf-8"))

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_the_header_echoes_the_contract_the_caller_rendered_under(self) -> None:
        status, headers, body = self.post_manual(
            {"manualKey": "ab" * 32, "recipe": self.recipe(), "manualContract": "5"},
        )
        self.assertEqual(status, 200)
        self.assertEqual(headers["X-Plaits-Manual-Contract"], "5")
        self.assertTrue(body.startswith(b"%PDF-"))

    @unittest.skipUnless(HAS_REPORTLAB, "ReportLab is installed in the builder image and bundled document runtime")
    def test_a_caller_that_sends_no_contract_still_renders(self) -> None:
        # A pre-2026-07-27 Worker against this image: the PDF must still come
        # back, falling back to the env default rather than 400ing.
        import container_server

        status, headers, body = self.post_manual({"manualKey": "ab" * 32, "recipe": self.recipe()})
        self.assertEqual(status, 200)
        self.assertEqual(headers["X-Plaits-Manual-Contract"], container_server.MANUAL_CONTRACT_FALLBACK)
        self.assertTrue(body.startswith(b"%PDF-"))

    def test_a_contract_that_could_forge_a_header_is_rejected(self) -> None:
        for bad in ["5\r\nX-Injected: 1", "", "x" * 33, 5, "contract 5"]:
            with self.subTest(contract=bad):
                status, _headers, body = self.post_manual(
                    {"manualKey": "ab" * 32, "recipe": self.recipe(), "manualContract": bad},
                )
                self.assertEqual(status, 400)
                self.assertIn(b"manual contract is invalid", body)


# Keep this LAST. It used to sit mid-file, above ContainerManualEndpointTest, so
# `python3 test_render_manual.py` ran unittest.main() before the classes below
# were defined and quietly collected only the 16 tests above it — a direct run
# reported OK while skipping five, including every /manual endpoint test.
if __name__ == "__main__":
    unittest.main()
