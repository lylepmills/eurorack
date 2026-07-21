from __future__ import annotations

import json
import unittest
from itertools import product
from pathlib import Path

from generate_engine_config import (
    CATALOG,
    DEFAULT_CHORD_TABLES,
    DEFAULT_CONFIGURATION,
    render_config,
    validate_recipe,
)


FIXTURES = Path(__file__).parent


class GenerateEngineConfigTest(unittest.TestCase):
    def load(self, name: str) -> dict:
        return json.loads((FIXTURES / name).read_text(encoding="utf-8"))

    def test_catalog_matches_the_approved_product_catalog(self) -> None:
        self.assertEqual(len(CATALOG), 39)

    def test_legacy_recipes_receive_the_stable_default_option_profile(self) -> None:
        recipe = validate_recipe(self.load("default_recipe.json"))
        self.assertEqual(recipe.options_profile_id, 0x0002)

    def test_every_option_profile_has_a_unique_legacy_safe_marker(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 4
        recipe["preferences"] = {"navigationMode": "linear"}
        option_values = (
            ("lockedFrequencyKnob", ["octaves", "decay", "aux-crossfade", "macro-4"]),
            ("modelInput", ["model", "lpg-colour", "aux-crossfade", "macro-4"]),
            ("levelInput", ["level", "decay"]),
            ("auxOutput", ["alternate-model", "square-subosc", "sine-subosc"]),
            ("suboscillatorOctave", [0, -1, -2]),
            ("chordTable", ["original", "jon-butler", "joe-mcmullen"]),
            ("holdOnTrigger", [False, True]),
        )
        markers = set()
        for selected in product(*(values for _, values in option_values)):
            recipe["initialOptions"] = {
                name: value for (name, _), value in zip(option_values, selected)
            }
            marker = validate_recipe(recipe).options_profile_id
            self.assertGreater(marker & 0xff, 1)
            markers.add(marker)
        # Every option combination must map to a distinct profile marker.
        # Product of the option cardinalities: 4 * 4 * 2 * 3 * 3 * 3 * 2.
        self.assertEqual(len(markers), 1728)

    def test_registry_translates_green_red_amber_to_amber_green_red(self) -> None:
        recipe = self.load("mixed_recipe.json")
        config = render_config(validate_recipe(recipe))
        registrations = config.split("#define PLAITS_REGISTER_ENGINES", 1)[1]
        self.assertLess(registrations.index("&six_op_engine_"), registrations.index("&virtual_analog_engine_"))
        self.assertLess(registrations.index("&virtual_analog_engine_"), registrations.index("&bass_drum_engine_"))

    def test_dx_banks_and_mixed_lab_engines_are_encoded_per_slot(self) -> None:
        config = render_config(validate_recipe(self.load("mixed_recipe.json")))
        self.assertIn("{ 0, -1, 1, -1, 2,", config)
        self.assertIn("GlissonEngine glisson_engine_;", config)
        self.assertIn("PulsarEngine pulsar_engine_;", config)
        self.assertEqual(config.count("SixOpEngine six_op_engine_;"), 1)
        self.assertEqual(config.count("RegisterInstance(&six_op_engine_"), 3)

    def bank_document(self, first_byte: int = 0) -> dict:
        voices = [{"name": f"V{i}", "algorithm": 1, "packed": [0] * 128} for i in range(32)]
        voices[0]["packed"][0] = first_byte
        return {
            "id": "warm-keys", "packageId": "anon/warm-keys", "version": "1.0.0",
            "digest": None, "name": "Warm Keys", "author": "T", "license": "CC0-1.0",
            "origin": "Community", "description": "Custom keys.", "voices": voices,
        }

    def v6_recipe(self, slots: list[str], user_data_banks: list[dict]) -> dict:
        return {
            "schemaVersion": 6,
            "target": "mutable-instruments-plaits",
            "firmware": "rubato-plaits",
            "output": "audio-wav",
            "slots": slots,
            "preferences": dict(DEFAULT_CONFIGURATION["preferences"]),
            "initialOptions": dict(DEFAULT_CONFIGURATION["initialOptions"]),
            "resources": {
                "chordTables": [dict(table) for table in DEFAULT_CHORD_TABLES],
                "userDataBanks": user_data_banks,
            },
        }

    def test_custom_bank_is_baked_as_an_override(self) -> None:
        # mixed_recipe.json places six-op banks 0, 1, and 2.
        slots = self.load("mixed_recipe.json")["slots"]
        recipe = self.v6_recipe(slots, [{"index": 0, "bank": self.bank_document(first_byte=42)}])
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 1", config)
        self.assertIn("static const uint8_t kUserDataBankOverride_0[4096] = { 42, 0,", config)
        self.assertIn("static const uint8_t* const kUserDataBankOverride[3] = { kUserDataBankOverride_0, NULL, NULL };", config)

    def test_override_for_a_bank_no_engine_uses_is_pruned(self) -> None:
        # No six-op engine placed -> the override is accepted but not baked.
        recipe = self.v6_recipe(["virtual-analog"] * 24, [{"index": 0, "bank": self.bank_document()}])
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0", config)
        self.assertNotIn("kUserDataBankOverride_0", config)

    def test_malformed_custom_bank_is_rejected(self) -> None:
        slots = self.load("mixed_recipe.json")["slots"]
        short = self.bank_document()
        short["voices"] = short["voices"][:31]
        with self.assertRaises(ValueError):
            validate_recipe(self.v6_recipe(slots, [{"index": 0, "bank": short}]))
        out_of_range = self.bank_document(first_byte=200)  # >127
        with self.assertRaises(ValueError):
            validate_recipe(self.v6_recipe(slots, [{"index": 0, "bank": out_of_range}]))
        with self.assertRaises(ValueError):  # duplicate index
            validate_recipe(self.v6_recipe(slots, [
                {"index": 0, "bank": self.bank_document()},
                {"index": 0, "bank": self.bank_document()},
            ]))

    def test_audition_recipe_registers_every_new_lab_engine(self) -> None:
        config = render_config(validate_recipe(self.load("audition_recipe.json")))
        class_names = [
            "LoopbackEngine",
            "LockstepEngine",
            "TapfieldEngine",
            "PhaseWeaveEngine",
            "SidebandEngine",
            "AttractorEngine",
            "UndertowEngine",
            "ReedPipeEngine",
            "PhaseFlockEngine",
            "RulefieldEngine",
            "SpectralSpiralEngine",
        ]
        for class_name in class_names:
            self.assertEqual(config.count(f"{class_name} "), 1)

    def test_unknown_engine_is_rejected(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["slots"][0] = "source-code-from-browser"
        with self.assertRaisesRegex(ValueError, "unapproved"):
            validate_recipe(recipe)

    def test_recipe_slot_count_must_be_24_or_32(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["slots"].pop()
        with self.assertRaisesRegex(ValueError, "24 slots, or 32"):
            validate_recipe(recipe)

    def test_32_slot_recipes_require_schema_v6(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["slots"] = recipe["slots"] + ["loopback"] * 8
        with self.assertRaisesRegex(ValueError, "schemaVersion 6"):
            validate_recipe(recipe)

    def fourth_bank_slots(self) -> list[str]:
        return self.load("default_recipe.json")["slots"] + [
            "loopback", "lockstep", "tapfield", "phase-weave",
            "sideband-bank", "attractor", "undertow", "reed-pipe",
        ]

    def test_fourth_bank_recipe_renders_a_32_engine_registry(self) -> None:
        config = render_config(validate_recipe(self.v6_recipe(self.fourth_bank_slots(), [])))
        self.assertIn("#define PLAITS_ENGINE_COUNT 32", config)
        self.assertIn("kEngineUserDataBank[32]", config)
        registrations = config.split("#define PLAITS_REGISTER_ENGINES", 1)[1]
        self.assertEqual(registrations.count("RegisterInstance("), 32)
        # The fourth (orange) bank stays last in the internal
        # amber/green/red/orange registry order.
        self.assertLess(
            registrations.index("&virtual_analog_vcf_engine_"),  # amber bank, internal first
            registrations.index("&loopback_engine_"))
        self.assertLess(
            registrations.index("&loopback_engine_"),
            registrations.index("&reed_pipe_engine_"))

    def test_24_slot_recipes_emit_the_default_engine_count(self) -> None:
        config = render_config(validate_recipe(self.load("default_recipe.json")))
        self.assertIn("#define PLAITS_ENGINE_COUNT 24", config)
        self.assertIn("kEngineUserDataBank[24]", config)

    def test_v6_custom_banks_are_optional(self) -> None:
        slots = self.load("default_recipe.json")["slots"]
        config = render_config(validate_recipe(self.v6_recipe(slots, [])))
        self.assertIn("#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0", config)

    def test_versioned_package_references_are_normalized(self) -> None:
        public = self.load("../plaits_lab_catalog/public_catalog.json")
        by_id = {engine["id"]: engine for engine in public["engines"]}
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 3
        recipe["slots"] = [
            {
                "engine": engine_id,
                "package": by_id[engine_id]["packageId"],
                "version": by_id[engine_id]["version"],
                "digest": by_id[engine_id]["digest"],
            }
            for engine_id in recipe["slots"]
        ]
        self.assertEqual(validate_recipe(recipe).public_slots, self.load("default_recipe.json")["slots"])

    def test_firmware_preferences_are_rendered_as_closed_numeric_constants(self) -> None:
        public = self.load("../plaits_lab_catalog/public_catalog.json")
        by_id = {engine["id"]: engine for engine in public["engines"]}
        recipe = self.load("default_recipe.json")
        recipe.update({
            "schemaVersion": 4,
            "slots": [
                {
                    "engine": engine_id,
                    "package": by_id[engine_id]["packageId"],
                    "version": by_id[engine_id]["version"],
                    "digest": by_id[engine_id]["digest"],
                }
                for engine_id in recipe["slots"]
            ],
            "preferences": {"navigationMode": "banked"},
            "initialOptions": {
                "lockedFrequencyKnob": "macro-4",
                "modelInput": "aux-crossfade",
                "levelInput": "decay",
                "auxOutput": "sine-subosc",
                "suboscillatorOctave": -2,
                "chordTable": "joe-mcmullen",
                "holdOnTrigger": True,
            },
        })
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_BUILD_NAVIGATION_MODE 1", config)
        self.assertIn("#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION 3", config)
        self.assertIn("#define PLAITS_BUILD_MODEL_CV_OPTION 2", config)
        self.assertIn("#define PLAITS_BUILD_LEVEL_CV_OPTION 1", config)
        self.assertIn("#define PLAITS_BUILD_AUX_SUBOSC_WAVE_OPTION 2", config)
        self.assertIn("#define PLAITS_BUILD_AUX_SUBOSC_OCTAVE_OPTION 2", config)
        self.assertIn("#define PLAITS_BUILD_CHORD_SET_OPTION 2", config)
        self.assertIn("#define PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION 1", config)
        self.assertRegex(config, r"#define PLAITS_BUILD_OPTIONS_PROFILE_ID 0x[0-9a-f]{4}u")

        recipe["initialOptions"]["compilerFlag"] = "-DUNTRUSTED"
        with self.assertRaisesRegex(ValueError, "unsupported firmware option"):
            validate_recipe(recipe)

    def test_fourth_macro_lives_on_model_input_not_level(self) -> None:
        public = self.load("../plaits_lab_catalog/public_catalog.json")
        by_id = {engine["id"]: engine for engine in public["engines"]}
        recipe = self.load("default_recipe.json")
        recipe.update({
            "schemaVersion": 4,
            "slots": [
                {
                    "engine": engine_id,
                    "package": by_id[engine_id]["packageId"],
                    "version": by_id[engine_id]["version"],
                    "digest": by_id[engine_id]["digest"],
                }
                for engine_id in recipe["slots"]
            ],
            "preferences": {"navigationMode": "banked"},
            "initialOptions": {
                "lockedFrequencyKnob": "octaves",
                "modelInput": "macro-4",
                "levelInput": "level",
                "auxOutput": "alternate-model",
                "suboscillatorOctave": 0,
                "chordTable": "original",
                "holdOnTrigger": False,
            },
        })
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_BUILD_MODEL_CV_OPTION 3", config)
        self.assertIn("#define PLAITS_BUILD_LEVEL_CV_OPTION 0", config)

        # The fourth macro moved off the LEVEL input, so requesting it there
        # is no longer a supported firmware option.
        recipe["initialOptions"]["modelInput"] = "model"
        recipe["initialOptions"]["levelInput"] = "macro-4"
        with self.assertRaisesRegex(ValueError, "unsupported firmware option"):
            validate_recipe(recipe)

    def test_local_chord_tables_are_rendered_as_bounded_numeric_data(self) -> None:
        public = self.load("../plaits_lab_catalog/public_catalog.json")
        chord_catalog = self.load("../plaits_lab_chord_tables/catalog.json")
        by_id = {engine["id"]: engine for engine in public["engines"]}
        local_table = chord_catalog["tables"][0]
        local_table.update({
            "id": "original-draft-1",
            "packageId": "local/original-draft-1",
            "version": "draft",
            "digest": None,
            "name": "Original edit",
            "author": "You",
            "origin": "Local",
            "description": "A local table edit.",
        })
        local_table["chords"][0]["voices"][1] = 9
        recipe = self.load("default_recipe.json")
        recipe.update({
            "schemaVersion": 5,
            "slots": [
                {
                    "engine": engine_id,
                    "package": by_id[engine_id]["packageId"],
                    "version": by_id[engine_id]["version"],
                    "digest": by_id[engine_id]["digest"],
                }
                for engine_id in recipe["slots"]
            ],
            "preferences": {"navigationMode": "linear"},
            "initialOptions": {
                "lockedFrequencyKnob": "octaves",
                "modelInput": "model",
                "levelInput": "level",
                "auxOutput": "alternate-model",
                "suboscillatorOctave": 0,
                "chordTable": "original-draft-1",
                "holdOnTrigger": False,
            },
            "resources": {"chordTables": [local_table]},
        })
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_CHORD_TABLE_COUNT 1", config)
        self.assertIn("{ 0, 9, 1199, 1200 }", config)
        self.assertIn("#define PLAITS_BUILD_CHORD_SET_OPTION 0", config)

        recipe["resources"]["chordTables"][0]["chords"][0]["voices"][1] = 9000
        with self.assertRaisesRegex(ValueError, "bounded cent offsets"):
            validate_recipe(recipe)


if __name__ == "__main__":
    unittest.main()
