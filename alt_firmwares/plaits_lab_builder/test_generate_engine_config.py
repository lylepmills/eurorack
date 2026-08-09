from __future__ import annotations

import json
import re
import unittest
from itertools import product
from pathlib import Path

from generate_engine_config import (
    ATTENUVERTER_MODE_MIN_SCHEMA_VERSION,
    CATALOG,
    DEFAULT_CHORD_TABLES,
    DEFAULT_CONFIGURATION,
    DEFAULT_SCALE_BANK,
    MAX_RECIPE_SCHEMA_VERSION,
    MIN_RECIPE_SCHEMA_VERSION,
    render_config,
    validate_recipe,
)


FIXTURES = Path(__file__).parent


class GenerateEngineConfigTest(unittest.TestCase):
    def load(self, name: str) -> dict:
        return json.loads((FIXTURES / name).read_text(encoding="utf-8"))

    def test_catalog_matches_the_approved_product_catalog(self) -> None:
        self.assertEqual(len(CATALOG), 87)

    def test_lpc_words_emits_its_prosody_behavior_mask(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["slots"][0] = "lpc-speech"
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_HAS_LPC_WORDS_ENGINE 1", config)
        # Public green slot 0 follows the leading eight amber engines in the
        # firmware registry, so LPC Words is internal engine index 8.
        self.assertIn("static const uint32_t kLPCWordsEngineMask = 0x00000100;", config)

    def test_palette_without_lpc_words_disables_its_behavior_mask(self) -> None:
        config = render_config(validate_recipe(self.load("default_recipe.json")))
        self.assertIn("#define PLAITS_HAS_LPC_WORDS_ENGINE 0", config)
        self.assertIn("static const uint32_t kLPCWordsEngineMask = 0x00000000;", config)

    def test_speech_banks_allow_lpc_words_without_original_speech(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["slots"][7] = "lpc-speech"
        recipe.update({
            "schemaVersion": 17,
            "preferences": dict(DEFAULT_CONFIGURATION["preferences"]),
            "initialOptions": dict(DEFAULT_CONFIGURATION["initialOptions"]),
            "resources": {
                "chordTables": DEFAULT_CHORD_TABLES,
                "speechBanks": {"stockBankIds": [1, 4], "customBanks": []},
            },
        })
        build = validate_recipe(recipe)
        self.assertEqual(build.speech_banks["stockBankIds"], [1, 4])

        recipe["slots"][7] = "virtual-analog"
        with self.assertRaisesRegex(ValueError, "Speech or LPC Words"):
            validate_recipe(recipe)

    def test_randomizer_profiles_are_compact_and_cover_every_selected_slot(self) -> None:
        build = validate_recipe(self.load("default_recipe.json"))
        config = render_config(build)
        profile_line = next(
            line for line in config.splitlines()
            if line.startswith("#define PLAITS_RANDOMIZER_PROFILES ")
        )
        pair_line = next(
            line for line in config.splitlines()
            if line.startswith("#define PLAITS_ENGINE_RANDOMIZER_PROFILE_INDICES ")
        )
        self.assertIn("1.2e-05f", profile_line)
        self.assertLess(len(re.findall(r"\{ [^{}]+ \}", profile_line)), 24)
        self.assertEqual(len(re.findall(r"\{ \d+, \d+ \}", pair_line)), 24)

    def test_stock_resonators_receive_exciter_envelope_routing(self) -> None:
        config = render_config(validate_recipe(self.load("default_recipe.json")))
        # The generated registry rotates public amber/green/red into its legacy
        # internal order. Inharmonic String and Modal Resonator consequently
        # land at internal indices 19 and 20, not their public slot numbers.
        self.assertIn(
            "#define PLAITS_RESONATOR_ENVELOPE_ENGINE_MASK 0x00180000u",
            config,
        )

    def test_worker_and_container_schema_ranges_stay_in_sync(self) -> None:
        # The public Worker validates first, then sends its normalized recipe to
        # this Python container. A ceiling mismatch lets the API accept a recipe
        # that the actual firmware build rejects, which was the v13/v14 stereo
        # incident. Pin both endpoints here so changing either one alone fails CI.
        source = (FIXTURES / "src" / "contract.ts").read_text(encoding="utf-8")
        minimum = re.search(r"export const minRecipeSchemaVersion = (\d+);", source)
        maximum = re.search(r"export const maxRecipeSchemaVersion = (\d+);", source)
        self.assertIsNotNone(minimum)
        self.assertIsNotNone(maximum)
        self.assertEqual(int(minimum.group(1)), MIN_RECIPE_SCHEMA_VERSION)
        self.assertEqual(int(maximum.group(1)), MAX_RECIPE_SCHEMA_VERSION)

    def test_output_format_accepts_wav_and_intel_hex_only(self) -> None:
        recipe = self.load("default_recipe.json")
        validate_recipe(recipe)
        recipe["output"] = "intel-hex"
        validate_recipe(recipe)
        recipe["output"] = "raw-binary"
        with self.assertRaisesRegex(ValueError, "unsupported output format"):
            validate_recipe(recipe)

    def test_legacy_recipes_migrate_to_the_current_default_option_profile(self) -> None:
        # Also the value PLAITS_BUILD_OPTIONS_PROFILE_ID carries in
        # plaits/build_config.h. Its nonzero upper byte makes it disjoint from
        # every legacy 16-bit profile, forcing one correct defaults apply.
        recipe = validate_recipe(self.load("default_recipe.json"))
        self.assertEqual(recipe.options_profile_id, 0x081276)
        self.assertEqual(recipe.attenuverter_mode, 0)

    def test_option_values_match_the_firmware_numbering(self) -> None:
        # These numbers are the firmware's (plaits/dsp/voice.h): they pick the
        # LED color each setting shows and the DSP compares against them
        # directly. Recipes bind by name, so a drift here is silent — hence the
        # pin. Changing any of it means bumping OPTIONS_LAYOUT_VERSION.
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 4
        recipe["preferences"] = {"navigationMode": "linear"}
        recipe["initialOptions"] = {
            "lockedFrequencyKnob": "macro-4",
            "modelInput": "macro-4",
            "levelInput": "decay",
            "auxOutput": "stereo",
            "suboscillatorOctave": -2,
            "chordTable": "joe-mcmullen",
            "holdOnTrigger": True,
        }
        build = validate_recipe(recipe)
        self.assertEqual(build.locked_frequency_pot_option, 1)
        self.assertEqual(build.model_cv_option, 1)
        self.assertEqual(build.level_cv_option, 1)
        self.assertEqual(build.aux_output_option, 1)
        self.assertEqual(build.aux_subosc_option, 2)
        self.assertEqual(build.chord_set_option, 2)
        self.assertEqual(build.hold_on_trigger_option, 1)
        self.assertEqual(build.attenuverter_mode, 0)

    def test_one_knob_envelopes_are_schema_19_values_four_and_five(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 19
        recipe["preferences"] = {"navigationMode": "linear"}
        recipe["resources"] = {"chordTables": DEFAULT_CHORD_TABLES}

        for name, numeric in (("triggered-envelope", 4), ("gated-envelope", 5)):
            recipe["initialOptions"] = dict(
                DEFAULT_CONFIGURATION["initialOptions"],
                lockedFrequencyKnob=name,
                attenuverterMode="stock",
            )
            build = validate_recipe(recipe)
            self.assertEqual(build.locked_frequency_pot_option, numeric)
            config = render_config(build)
            self.assertIn(
                f"#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION {numeric}",
                config,
            )
            self.assertIn("#define PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE 1", config)

        recipe["schemaVersion"] = 18
        with self.assertRaisesRegex(ValueError, "schemaVersion 19"):
            validate_recipe(recipe)

    def test_sync_in_is_model_input_value_four(self) -> None:
        # Since v22 the capability comes from the preference, so the starting
        # value has to be accompanied by it; on its own it is rejected rather
        # than compiled (see test_starting_in_sync_in_requires_the_preference).
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 22
        recipe["preferences"] = {
            "navigationMode": "linear", "calibration": False,
            "colorBlindMode": False, "replaceableFmBanks": False,
            "syncInput": True,
        }
        recipe["resources"] = {"chordTables": DEFAULT_CHORD_TABLES}
        recipe["initialOptions"] = dict(
            DEFAULT_CONFIGURATION["initialOptions"],
            modelInput="sync-in",
            attenuverterMode="stock",
        )
        build = validate_recipe(recipe)
        self.assertEqual(build.model_cv_option, 4)
        config = render_config(build)
        self.assertIn("#define PLAITS_BUILD_MODEL_CV_OPTION 4", config)
        self.assertIn("#define PLAITS_BUILD_ENABLE_SYNC_INPUT 1", config)

        recipe["schemaVersion"] = 20
        with self.assertRaisesRegex(ValueError, "schemaVersion"):
            validate_recipe(recipe)

    def test_attenuverter_starting_mode_is_schema_18_and_changes_the_profile(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 18
        recipe["preferences"] = {"navigationMode": "linear"}
        recipe["resources"] = {"chordTables": DEFAULT_CHORD_TABLES}
        markers = set()
        for name, numeric in (("stock", 0), ("drift", 1), ("step", 2)):
            recipe["initialOptions"] = dict(
                DEFAULT_CONFIGURATION["initialOptions"], attenuverterMode=name
            )
            build = validate_recipe(recipe)
            self.assertEqual(build.attenuverter_mode, numeric)
            self.assertIn(
                f"#define PLAITS_BUILD_ATTENUVERTER_MODE {numeric}",
                render_config(build),
            )
            markers.add(build.options_profile_id)
        self.assertEqual(len(markers), 3)

        recipe["schemaVersion"] = 17
        with self.assertRaisesRegex(ValueError, "schemaVersion 18"):
            validate_recipe(recipe)

        # The public Worker normalizes missing legacy options to the musically
        # identical stock value before calling the private container. This
        # exact cross-layer shape must remain buildable for every pre-v18
        # recipe, while genuinely new Drift/Step choices stay gated above.
        recipe["initialOptions"]["attenuverterMode"] = "stock"
        self.assertEqual(validate_recipe(recipe).attenuverter_mode, 0)

    def test_auto_level_routing_is_schema_16_and_firmware_value_two(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 16
        recipe["preferences"] = {"navigationMode": "linear"}
        recipe["initialOptions"] = dict(
            DEFAULT_CONFIGURATION["initialOptions"], levelInput="auto"
        )
        recipe["resources"] = {"chordTables": DEFAULT_CHORD_TABLES}

        build = validate_recipe(recipe)

        self.assertEqual(build.level_cv_option, 2)
        self.assertIn("#define PLAITS_BUILD_LEVEL_CV_OPTION 2", render_config(build))

        recipe["schemaVersion"] = 15
        with self.assertRaisesRegex(ValueError, "schemaVersion 16"):
            validate_recipe(recipe)

    def calibration_recipe(self, schema_version: int, calibration) -> dict:
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = schema_version
        recipe["preferences"] = {"navigationMode": "linear", "calibration": calibration}
        recipe["initialOptions"] = dict(DEFAULT_CONFIGURATION["initialOptions"])
        # v5+ recipes carry their chord tables; v14 (like v5-v11) carries no
        # custom FM banks unless the palette actually has one.
        recipe["resources"] = {"chordTables": DEFAULT_CHORD_TABLES}
        if schema_version >= 16:
            recipe["resources"]["scaleBank"] = DEFAULT_SCALE_BANK
        return recipe

    def test_calibration_defaults_off_and_emits_the_define(self) -> None:
        # A recipe minted before v14 has no calibration key at all and must mean
        # off — the firmware has shipped without the procedure since 2023.
        build = validate_recipe(self.load("default_recipe.json"))
        self.assertEqual(build.enable_calibration, 0)
        self.assertIn("#define PLAITS_BUILD_ENABLE_CALIBRATION 0", render_config(build))

    def test_calibration_on_emits_the_define(self) -> None:
        build = validate_recipe(self.calibration_recipe(14, True))
        self.assertEqual(build.enable_calibration, 1)
        self.assertIn("#define PLAITS_BUILD_ENABLE_CALIBRATION 1", render_config(build))

    def test_calibration_does_not_move_the_options_profile_id(self) -> None:
        # The profile id exists to reset a module's SAVED options when a stored
        # value changes meaning. Calibration is compiled in or out, not stored,
        # so folding it in would reset every user's options the first time they
        # turned it on.
        off = validate_recipe(self.calibration_recipe(14, False))
        on = validate_recipe(self.calibration_recipe(14, True))
        self.assertEqual(off.options_profile_id, on.options_profile_id)

    def test_calibration_requires_schema_14(self) -> None:
        with self.assertRaisesRegex(ValueError, "schemaVersion 14"):
            validate_recipe(self.calibration_recipe(11, True))
        # Explicitly off asks for nothing new, so it is fine under any version.
        self.assertEqual(validate_recipe(self.calibration_recipe(11, False)).enable_calibration, 0)

    def test_calibration_must_be_a_boolean(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsupported firmware option"):
            validate_recipe(self.calibration_recipe(14, "yes"))

    def roved_recipe(self, schema_version: int = 15) -> dict:
        recipe = self.calibration_recipe(schema_version, False)
        recipe["target"] = "plum-audio-roved"
        return recipe

    def test_roved_target_emits_the_panel_define(self) -> None:
        roved = validate_recipe(self.roved_recipe())
        plaits = validate_recipe(self.calibration_recipe(15, False))
        self.assertEqual(roved.roved_panel, 1)
        self.assertEqual(plaits.roved_panel, 0)
        self.assertIn("#define PLAITS_ROVED_PANEL 1", render_config(roved))
        self.assertIn("#define PLAITS_ROVED_PANEL 0", render_config(plaits))

    def test_roved_target_requires_schema_15(self) -> None:
        with self.assertRaisesRegex(ValueError, "schemaVersion 15"):
            validate_recipe(self.roved_recipe(14))

    def test_roved_panel_does_not_move_the_options_profile_id(self) -> None:
        # The panel changes physical gestures, not the meaning of any saved
        # option value, so moving the profile would needlessly reset settings.
        roved = validate_recipe(self.roved_recipe())
        plaits = validate_recipe(self.calibration_recipe(15, False))
        self.assertEqual(roved.options_profile_id, plaits.options_profile_id)

    def color_blind_recipe(self, schema_version: int, color_blind_mode, calibration=False) -> dict:
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = schema_version
        recipe["preferences"] = {
            "navigationMode": "linear",
            "calibration": calibration,
            "colorBlindMode": color_blind_mode,
        }
        recipe["initialOptions"] = dict(DEFAULT_CONFIGURATION["initialOptions"])
        recipe["resources"] = {"chordTables": DEFAULT_CHORD_TABLES}
        return recipe

    def test_color_blind_mode_defaults_off_and_emits_the_define(self) -> None:
        build = validate_recipe(self.load("default_recipe.json"))
        self.assertEqual(build.color_blind_mode, 0)
        self.assertIn("#define PLAITS_BUILD_COLOR_BLIND_MODE 0", render_config(build))

    def test_legacy_recipe_emits_the_original_eight_scale_bank(self) -> None:
        build = validate_recipe(self.load("default_recipe.json"))
        config = render_config(build)
        self.assertEqual(build.scale_bank, DEFAULT_SCALE_BANK)
        self.assertIn("#define PLAITS_SCALE_BANK_COUNT 8", config)
        self.assertIn(
            "{ { 0, 256, 512, 896, 1152, 0, 0 }, 5 }",
            config,
        )

    def test_v16_scale_bank_is_validated_and_rendered_in_recipe_order(self) -> None:
        recipe = self.calibration_recipe(16, False)
        recipe["resources"]["scaleBank"] = [
            {
                "id": "just-five",
                "name": "Just five",
                "description": "A local microtonal five-note scale.",
                "pitches": [0, 261, 637, 899, 1393],
                "tuning": "Microtonal",
                "source": "Local",
            },
            {
                "id": "whole-tone",
                "name": "Whole tone",
                "description": "Six evenly spaced whole tones.",
                "pitches": [0, 256, 512, 768, 1024, 1280],
                "tuning": "12-TET",
                "source": "Shipped",
            },
        ]
        build = validate_recipe(recipe)
        config = render_config(build)
        self.assertEqual([scale["id"] for scale in build.scale_bank], ["just-five", "whole-tone"])
        self.assertIn("#define PLAITS_SCALE_BANK_COUNT 2", config)
        self.assertIn("{ { 0, 261, 637, 899, 1393, 0, 0 }, 5 }", config)
        self.assertLess(config.index("0, 261, 637"), config.index("0, 256, 512, 768"))

    def test_v16_scale_bank_defaults_when_absent_and_rejects_invalid_data(self) -> None:
        recipe = self.calibration_recipe(16, False)
        del recipe["resources"]["scaleBank"]
        self.assertEqual(
            validate_recipe(recipe).scale_bank,
            DEFAULT_SCALE_BANK,
        )

        recipe = self.calibration_recipe(16, False)
        recipe["resources"]["scaleBank"] = [DEFAULT_SCALE_BANK[0], DEFAULT_SCALE_BANK[0]]
        with self.assertRaisesRegex(ValueError, "duplicate scale ID"):
            validate_recipe(recipe)

        recipe = self.calibration_recipe(16, False)
        mismatched = dict(DEFAULT_SCALE_BANK[0])
        mismatched["pitches"] = [0, 261]
        recipe["resources"]["scaleBank"] = [mismatched, DEFAULT_SCALE_BANK[1]]
        with self.assertRaisesRegex(ValueError, "tuning label"):
            validate_recipe(recipe)

    def test_color_blind_mode_on_emits_the_define_and_composes_with_calibration(self) -> None:
        recipe = self.color_blind_recipe(15, True, calibration=True)
        recipe["target"] = "plum-audio-roved"
        build = validate_recipe(recipe)
        self.assertEqual(build.color_blind_mode, 1)
        self.assertEqual(build.enable_calibration, 1)
        self.assertEqual(build.roved_panel, 1)
        config = render_config(build)
        self.assertIn("#define PLAITS_BUILD_COLOR_BLIND_MODE 1", config)
        self.assertIn("#define PLAITS_BUILD_ENABLE_CALIBRATION 1", config)
        self.assertIn("#define PLAITS_ROVED_PANEL 1", config)

    def test_color_blind_mode_composes_with_per_engine_stereo(self) -> None:
        recipe = self.color_blind_recipe(15, True)
        recipe["stereoEngines"] = ["chiptune", "modal-resonator", "chiptune"]
        build = validate_recipe(recipe)
        self.assertEqual(build.stereo_engines, ("chiptune", "modal-resonator"))
        self.assertEqual(build.color_blind_mode, 1)

    def test_color_blind_mode_does_not_move_the_options_profile_id(self) -> None:
        off = validate_recipe(self.color_blind_recipe(15, False))
        on = validate_recipe(self.color_blind_recipe(15, True))
        self.assertEqual(off.options_profile_id, on.options_profile_id)

    def test_color_blind_mode_requires_schema_15_and_a_boolean(self) -> None:
        with self.assertRaisesRegex(ValueError, "schemaVersion 15"):
            validate_recipe(self.color_blind_recipe(14, True))
        with self.assertRaisesRegex(ValueError, "unsupported firmware option"):
            validate_recipe(self.color_blind_recipe(15, "yes"))

    def test_subosc_shape_and_octave_fold_into_one_firmware_value(self) -> None:
        # The recipe keeps the shape in auxOutput and the octave beside it; the
        # firmware wants one aux-output setting and one suboscillator setting
        # carrying both. Square takes 0-2, sine 3-5, each over 0/-1/-2 octaves.
        expected = {
            ("square-subosc", 0): (2, 0),
            ("square-subosc", -1): (2, 1),
            ("square-subosc", -2): (2, 2),
            ("sine-subosc", 0): (2, 3),
            ("sine-subosc", -1): (2, 4),
            ("sine-subosc", -2): (2, 5),
            # Not a suboscillator: the shape and octave are carried anyway, so a
            # recipe keeps the settings the player steps back to.
            ("alternate-model", -1): (0, 1),
            ("stereo", 0): (1, 0),
        }
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 4
        recipe["preferences"] = {"navigationMode": "linear"}
        for (aux, octave), (aux_value, subosc_value) in expected.items():
            recipe["initialOptions"] = dict(
                DEFAULT_CONFIGURATION["initialOptions"],
                auxOutput=aux,
                suboscillatorOctave=octave,
            )
            build = validate_recipe(recipe)
            self.assertEqual(
                (build.aux_output_option, build.aux_subosc_option),
                (aux_value, subosc_value),
                msg=f"{aux} at {octave} octaves",
            )

    def test_every_chord_table_slot_maps_to_a_distinct_profile(self) -> None:
        # Nine tables fit on the hardware, so the chord digit's radix must be
        # nine. A smaller one lets tables 6-8 alias into the next digit, and the
        # colliding recipe silently skips applying its starting options.
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 5
        recipe["preferences"] = {"navigationMode": "linear"}
        tables = list(DEFAULT_CHORD_TABLES)
        while len(tables) < 9:
            draft = json.loads(json.dumps(DEFAULT_CHORD_TABLES[0]))
            draft.update({
                "id": f"draft-{len(tables)}",
                "packageId": f"local/draft-{len(tables)}",
                "digest": None,
                "origin": "Local",
            })
            tables.append(draft)
        recipe["resources"] = {"chordTables": tables}
        markers = set()
        for table in tables:
            recipe["initialOptions"] = dict(
                DEFAULT_CONFIGURATION["initialOptions"], chordTable=table["id"]
            )
            markers.add(validate_recipe(recipe).options_profile_id)
        self.assertEqual(len(markers), 9)

    def test_every_option_profile_has_a_unique_legacy_safe_marker(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["schemaVersion"] = 19
        recipe["preferences"] = {"navigationMode": "linear"}
        recipe["resources"] = {"chordTables": DEFAULT_CHORD_TABLES}
        option_values = (
            ("lockedFrequencyKnob", [
                "octaves", "decay", "aux-crossfade", "macro-4",
                "triggered-envelope", "gated-envelope",
            ]),
            ("modelInput", ["model", "lpg-colour", "aux-crossfade", "macro-4"]),
            ("levelInput", ["level", "decay", "auto"]),
            ("auxOutput", ["alternate-model", "square-subosc", "sine-subosc", "stereo"]),
            ("suboscillatorOctave", [0, -1, -2]),
            ("chordTable", ["original", "jon-butler", "joe-mcmullen"]),
            ("holdOnTrigger", [False, True]),
            ("attenuverterMode", ["stock", "drift", "step"]),
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
        # Product of the cardinalities: 6 * 4 * 3 * 4 * 3 * 3 * 2 * 3.
        self.assertEqual(len(markers), 15552)

    def test_registry_translates_green_red_amber_to_amber_green_red(self) -> None:
        recipe = self.load("mixed_recipe.json")
        config = render_config(validate_recipe(recipe))
        registrations = config.split("#define PLAITS_REGISTER_ENGINES", 1)[1]
        self.assertLess(registrations.index("&six_op_engine_"), registrations.index("&virtual_analog_engine_"))
        self.assertLess(registrations.index("&virtual_analog_engine_"), registrations.index("&bass_drum_engine_"))

    def test_all_virtual_analog_variants_can_coexist(self) -> None:
        recipe = self.load("default_recipe.json")
        recipe["slots"][:3] = [
            "virtual-analog-dual",
            "virtual-analog-crossfade",
            "virtual-analog",
        ]
        config = render_config(validate_recipe(recipe))
        for declaration in (
            "VirtualAnalogDualEngine virtual_analog_dual_engine_;",
            "VirtualAnalogCrossfadeEngine virtual_analog_crossfade_engine_;",
            "VirtualAnalogEngine virtual_analog_engine_;",
        ):
            self.assertIn(declaration, config)
        self.assertEqual(config.count("RegisterInstance(&virtual_analog_dual_engine_"), 1)
        self.assertEqual(config.count("RegisterInstance(&virtual_analog_crossfade_engine_"), 1)
        self.assertEqual(config.count("RegisterInstance(&virtual_analog_engine_"), 1)

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
        # Bank 0 is overridden; banks 1 and 2 are live stock slots, so the resolved
        # table names syx_bank_1/2 (not NULL). Only syx_bank_0 goes unreferenced and
        # is dropped by --gc-sections.
        #
        # A v6 recipe cannot opt in to replaceable banks (that needs v20), so this
        # is also the guard that a legacy recipe still emits the layout it always
        # did — no page alignment, no per-bank regions, no re-emitted factory data.
        self.assertIn(
            "static const uint8_t* const kResolvedUserDataBank[3] = "
            "{ kUserDataBankOverride_0, syx_bank_1, syx_bank_2 };",
            config)

    def test_override_for_a_bank_no_engine_uses_is_pruned(self) -> None:
        # No six-op engine placed -> the override is accepted but not baked.
        recipe = self.v6_recipe(["virtual-analog"] * 24, [{"index": 0, "bank": self.bank_document()}])
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0", config)
        self.assertNotIn("kUserDataBankOverride_0", config)

    def replaceable_recipe(self, slots: list, user_data_banks: list[dict]) -> dict:
        """A v20 recipe that OPTS IN to replaceable FM banks.

        Opting in is what page-aligns the banks and gives each one its own flash
        region. Every other recipe in this file leaves it off, which is the
        default and keeps the historical layout — so the un-flagged tests below
        are also the regression guard that opting out changes nothing.
        """
        recipe = self.v12_recipe(slots, user_data_banks)
        recipe["schemaVersion"] = 20
        recipe["preferences"] = {
            "navigationMode": "linear",
            "calibration": False,
            "colorBlindMode": False,
            "replaceableFmBanks": True,
        }
        recipe["initialOptions"] = dict(
            DEFAULT_CONFIGURATION["initialOptions"], attenuverterMode="stock")
        return recipe

    def v12_recipe(self, slots: list, user_data_banks: list[dict]) -> dict:
        return {
            "schemaVersion": 12,
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

    def test_v12_per_slot_bank_gets_a_fresh_index_above_the_factory_three(self) -> None:
        slots = ["dx7-bank-a", "dx7-bank-b", "dx7-bank-c"] + ["virtual-analog"] * 21
        recipe = self.v12_recipe(
            slots, [{"slot": 0, "bank": self.bank_document(first_byte=7)}])
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 1", config)
        # The customized slot gets index 3 (above factory 0/1/2), with its own array.
        self.assertIn("static const uint8_t kUserDataBankOverride_3[4096] = { 7, 0,", config)
        # Bank 0's only slot was customized away, so factory bank 0 is now unreachable
        # -> NULL (syx_bank_0 dropped). Banks 1 and 2 stay live stock slots.
        self.assertIn(
            "static const uint8_t* const kResolvedUserDataBank[4] = "
            "{ NULL, syx_bank_1, syx_bank_2, kUserDataBankOverride_3 };",
            config)

    def test_swappable_build_re_emits_live_stock_banks_page_aligned(self) -> None:
        # The same palette with banks swappable (the default). Every live bank —
        # including the two STOCK ones — becomes a page-aligned region the module
        # can erase and reprogram, so the build re-emits them as its own arrays
        # and never names syx_bank_N. Net flash is unchanged: --gc-sections drops
        # the factory blobs the copies replaced.
        slots = ["dx7-bank-a", "dx7-bank-b", "dx7-bank-c"] + ["virtual-analog"] * 21
        recipe = self.replaceable_recipe(slots, [{"slot": 0, "bank": self.bank_document(first_byte=7)}])
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_HAS_SWAPPABLE_USER_DATA_BANKS 1", config)
        self.assertNotIn("syx_bank_", config)
        self.assertIn(
            "static const uint8_t* const kResolvedUserDataBank[4] = "
            "{ NULL, kUserDataBankOverride_1, kUserDataBankOverride_2, "
            "kUserDataBankOverride_3 };",
            config)
        # One region per bank the firmware carries, keyed on BANK so two slots
        # sharing a factory bank both see a transfer through either. The absent
        # bank 0 gets none, so Save refuses that slot rather than erasing flash
        # it does not own.
        self.assertIn(
            "static const UserDataRegion kUserDataRegions[3] = "
            "{ { kUserDataBankOverride_1, 1 }, { kUserDataBankOverride_2, 2 }, "
            "{ kUserDataBankOverride_3, 3 } };",
            config)
        self.assertIn("static const int kNumUserDataRegions = 3;", config)

    def test_swappable_banks_are_page_aligned_and_singly_defined(self) -> None:
        slots = ["dx7-bank-a"] + ["virtual-analog"] * 23
        recipe = self.replaceable_recipe(slots, [{"slot": 0, "bank": self.bank_document(first_byte=7)}])
        config = render_config(validate_recipe(recipe))
        # 2 KB alignment + a whole-region length is the correctness gate, not
        # tidiness: UserData::Save erases 2 KB pages, so a bank sharing a page
        # with code would erase firmware on the first transfer.
        self.assertIn(
            'extern const uint8_t kUserDataBankOverride_3[4096] __attribute__(('
            'section(".user_data_banks.3"), aligned(2048)));',
            config)
        # ONE definition program-wide, and EXTERNAL. A namespace-scope const has
        # internal linkage by default in C++, which left voice.o's copy
        # file-local and plaits.o's declaration unresolved; and two per-unit
        # copies at different addresses would have Save (plaits.o) write one
        # while the engine (voice.o) read the other, so a transfer would appear
        # to do nothing at all.
        self.assertIn(
            "extern const uint8_t kUserDataBankOverride_3[4096] = { 7, 0,", config)
        self.assertIn("#ifdef PLAITS_ENGINE_CONFIG_OWNS_USER_DATA_BANKS", config)
        self.assertIn("extern const uint8_t kUserDataBankOverride_3[4096];", config)

    def test_swappable_bank_clears_the_transfer_tag_bytes(self) -> None:
        # A baked bank must never read back as a TRANSFERRED one: the last two
        # bytes are where UserData::ptr looks for 'U' + bank. They are the tail
        # of voice 32's name field, which Plaits never displays, so the generator
        # clears them (along with the two count bytes below them).
        slots = ["dx7-bank-a"] + ["virtual-analog"] * 23
        document = self.bank_document(first_byte=7)
        document["voices"][31]["packed"] = [ord("X")] * 128
        recipe = self.replaceable_recipe(slots, [{"slot": 0, "bank": document}])
        config = render_config(validate_recipe(recipe))
        self.assertIn("88, 88, 0, 0, 0, 0 };", config)

    def test_sync_in_preference_compiles_it_without_the_starting_value(self) -> None:
        # The point of v22: Sync In is reachable at RUNTIME without having been
        # pre-selected. Starting Options are initial values the user can change on
        # the module, so deriving compiled capability from one meant changing your
        # mind later left the mode missing from the menu entirely (ui.cc sizes it
        # as 4 + PLAITS_BUILD_ENABLE_SYNC_INPUT).
        slots = ["virtual-analog"] * 24
        recipe = self.replaceable_recipe(slots, [])
        recipe["schemaVersion"] = 22
        recipe["preferences"]["syncInput"] = True
        recipe["preferences"]["replaceableFmBanks"] = False
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_BUILD_ENABLE_SYNC_INPUT 1", config)
        # ...and the starting value is untouched by it.
        self.assertIn("#define PLAITS_BUILD_MODEL_CV_OPTION 0", config)

    def test_starting_in_sync_in_requires_the_preference(self) -> None:
        # The starting value no longer implies the capability, so the pair has to
        # agree. Emitting it would compile a 4-entry MODEL menu (ui.cc sizes it
        # 4 + PLAITS_BUILD_ENABLE_SYNC_INPUT) while starting the module at index
        # 4 — off the end of its own menu.
        slots = ["virtual-analog"] * 24
        recipe = self.v12_recipe(slots, [])
        recipe["schemaVersion"] = 21
        recipe["initialOptions"] = dict(
            DEFAULT_CONFIGURATION["initialOptions"],
            attenuverterMode="stock", modelInput="sync-in")
        with self.assertRaisesRegex(ValueError, "requires the syncInput preference"):
            validate_recipe(recipe)

    def test_sync_in_starting_value_is_accepted_alongside_the_preference(self) -> None:
        slots = ["virtual-analog"] * 24
        recipe = self.replaceable_recipe(slots, [])
        recipe["schemaVersion"] = 22
        recipe["preferences"]["syncInput"] = True
        recipe["initialOptions"] = dict(
            recipe["initialOptions"], modelInput="sync-in")
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_BUILD_ENABLE_SYNC_INPUT 1", config)
        self.assertIn("#define PLAITS_BUILD_MODEL_CV_OPTION 4", config)

    def test_sync_in_is_off_by_default(self) -> None:
        slots = ["virtual-analog"] * 24
        config = render_config(validate_recipe(self.v12_recipe(slots, [])))
        self.assertIn("#define PLAITS_BUILD_ENABLE_SYNC_INPUT 0", config)

    def test_sync_in_preference_requires_v22(self) -> None:
        slots = ["virtual-analog"] * 24
        recipe = self.replaceable_recipe(slots, [])
        recipe["schemaVersion"] = 21
        recipe["preferences"]["syncInput"] = True
        with self.assertRaisesRegex(ValueError, "schemaVersion 22"):
            validate_recipe(recipe)

    def test_experimental_fm_preferences_are_independent_v23_flags(self) -> None:
        slots = ["waveshaping", "two-op-fm", "vowel-fof"] + ["virtual-analog"] * 21
        for linear_tzfm, fast_fm in product((False, True), repeat=2):
            with self.subTest(linear_tzfm=linear_tzfm, fast_fm=fast_fm):
                recipe = self.replaceable_recipe(slots, [])
                recipe["schemaVersion"] = 23
                recipe["preferences"].update({
                    "replaceableFmBanks": False,
                    "syncInput": False,
                    "linearTzfm": linear_tzfm,
                    "fastFm": fast_fm,
                })
                config = render_config(validate_recipe(recipe))
                self.assertIn(
                    f"#define PLAITS_BUILD_LINEAR_TZFM {int(linear_tzfm)}",
                    config,
                )
                self.assertIn(
                    f"#define PLAITS_BUILD_FAST_FM {int(fast_fm)}",
                    config,
                )

    def test_experimental_fm_preferences_require_v23_booleans(self) -> None:
        slots = ["waveshaping"] * 24
        recipe = self.replaceable_recipe(slots, [])
        recipe["schemaVersion"] = 22
        recipe["preferences"].update({
            "replaceableFmBanks": False,
            "syncInput": False,
            "linearTzfm": True,
            "fastFm": False,
        })
        with self.assertRaisesRegex(ValueError, "schemaVersion 23"):
            validate_recipe(recipe)

        recipe["schemaVersion"] = 23
        recipe["preferences"]["linearTzfm"] = "yes"
        with self.assertRaisesRegex(ValueError, "unsupported firmware option"):
            validate_recipe(recipe)

    def test_replaceable_fm_banks_require_v20(self) -> None:
        slots = ["dx7-bank-a"] + ["virtual-analog"] * 23
        recipe = self.replaceable_recipe(slots, [])
        recipe["schemaVersion"] = 19
        with self.assertRaisesRegex(ValueError, "schemaVersion 20"):
            validate_recipe(recipe)

    def test_v12_two_slots_of_the_same_engine_get_distinct_banks(self) -> None:
        # The whole point of v12: two dx7-bank-a placements, each its OWN bank.
        slots = ["dx7-bank-a", "dx7-bank-a", "dx7-bank-c"] + ["virtual-analog"] * 21
        recipe = self.v12_recipe(slots, [
            {"slot": 0, "bank": self.bank_document(first_byte=11)},
            {"slot": 1, "bank": self.bank_document(first_byte=22)},
        ])
        config = render_config(validate_recipe(recipe))
        self.assertIn("static const uint8_t kUserDataBankOverride_3[4096] = { 11, 0,", config)
        self.assertIn("static const uint8_t kUserDataBankOverride_4[4096] = { 22, 0,", config)
        # Both dx7-bank-a slots (factory 0) were customized and dx7-bank-b (factory 1)
        # isn't placed, so only factory bank 2 (dx7-bank-c) is live -> syx_bank_2;
        # banks 0 and 1 are NULL (syx_bank_0/1 dropped).
        self.assertIn(
            "static const uint8_t* const kResolvedUserDataBank[5] = "
            "{ NULL, NULL, syx_bank_2, kUserDataBankOverride_3, kUserDataBankOverride_4 };",
            config)

    def test_v12_uncustomized_fm_slot_keeps_its_factory_index(self) -> None:
        slots = ["dx7-bank-b"] + ["virtual-analog"] * 23
        recipe = self.v12_recipe(slots, [])
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0", config)
        self.assertIn("static const int8_t kEngineUserDataBank[24] = {", config)
        # Only dx7-bank-b (factory 1) is placed, so banks 0 and 2 are unreachable:
        # the resolved table names only syx_bank_1, dropping syx_bank_0/2 (~8 KB)
        # even with no customization at all.
        self.assertIn("#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 1", config)
        self.assertIn(
            "static const uint8_t* const kResolvedUserDataBank[3] = { NULL, syx_bank_1, NULL };",
            config)

    def test_v12_customizing_all_three_banks_drops_every_factory_blob(self) -> None:
        # All three FM slots customized -> no factory bank is reachable, so the
        # resolved table names no syx_bank_N at all (all ~12 KB of factory data is
        # dropped; only the three custom arrays remain).
        slots = ["dx7-bank-a", "dx7-bank-b", "dx7-bank-c"] + ["virtual-analog"] * 21
        recipe = self.v12_recipe(slots, [
            {"slot": 0, "bank": self.bank_document(first_byte=1)},
            {"slot": 1, "bank": self.bank_document(first_byte=2)},
            {"slot": 2, "bank": self.bank_document(first_byte=3)},
        ])
        config = render_config(validate_recipe(recipe))
        self.assertNotIn("syx_bank_0", config)
        self.assertNotIn("syx_bank_1", config)
        self.assertNotIn("syx_bank_2", config)
        self.assertIn(
            "static const uint8_t* const kResolvedUserDataBank[6] = "
            "{ NULL, NULL, NULL, kUserDataBankOverride_3, "
            "kUserDataBankOverride_4, kUserDataBankOverride_5 };",
            config)

    def test_v12_bank_on_a_non_existent_slot_is_rejected(self) -> None:
        recipe = self.v12_recipe(["dx7-bank-a"] + ["virtual-analog"] * 23,
                                 [{"slot": 99, "bank": self.bank_document()}])
        with self.assertRaisesRegex(ValueError, "distinct palette slot"):
            validate_recipe(recipe)

    def short_bank_document(self, count: int, first_byte: int = 0) -> dict:
        doc = self.bank_document(first_byte=first_byte)
        doc["voices"] = doc["voices"][:count]
        return doc

    def v13_recipe(self, slots: list, user_data_banks: list[dict]) -> dict:
        recipe = self.v12_recipe(slots, user_data_banks)
        recipe["schemaVersion"] = 13
        return recipe

    def test_v13_short_bank_emits_its_real_size(self) -> None:
        slots = ["dx7-bank-a"] + ["virtual-analog"] * 23
        recipe = self.v13_recipe(
            slots, [{"slot": 0, "bank": self.short_bank_document(4, first_byte=9)}])
        config = render_config(validate_recipe(recipe))
        # 4 patches * 128 bytes = a 512-byte array, and a matching size-table entry
        # so voice.cc sizes the Harmonics quantizer to 4 patches.
        self.assertIn("static const uint8_t kUserDataBankOverride_3[512] = { 9, 0,", config)
        self.assertIn(
            "static const size_t kResolvedUserDataBankSize[4] = { 4096, 4096, 4096, 512 };",
            config)

    def test_swappable_short_bank_pads_the_array_but_not_its_length(self) -> None:
        # A transfer always writes a whole region, so a swappable short bank is
        # padded up to one — this is the ONLY case where making banks swappable
        # costs flash, and the Advanced lock preference is what reclaims it.
        #
        # The SIZE table must keep the real content length regardless. Padding it
        # too would make a 4-patch bank sweep 32 Harmonics steps with 28 silent
        # ones, undoing the short banks shipped in schema v13.
        slots = ["dx7-bank-a"] + ["virtual-analog"] * 23
        recipe = self.replaceable_recipe(
            slots, [{"slot": 0, "bank": self.short_bank_document(4, first_byte=9)}])
        config = render_config(validate_recipe(recipe))
        self.assertIn("const uint8_t kUserDataBankOverride_3[4096]", config)
        self.assertIn(
            "static const size_t kResolvedUserDataBankSize[4] = { 4096, 4096, 4096, 512 };",
            config)

    def test_v13_full_bank_keeps_full_size(self) -> None:
        slots = ["dx7-bank-a"] + ["virtual-analog"] * 23
        recipe = self.v13_recipe(
            slots, [{"slot": 0, "bank": self.bank_document(first_byte=5)}])
        config = render_config(validate_recipe(recipe))
        self.assertIn("static const uint8_t kUserDataBankOverride_3[4096] = { 5, 0,", config)
        self.assertIn(
            "static const size_t kResolvedUserDataBankSize[4] = { 4096, 4096, 4096, 4096 };",
            config)

    def test_short_bank_rejected_below_v13(self) -> None:
        slots = ["dx7-bank-a"] + ["virtual-analog"] * 23
        recipe = self.v12_recipe(
            slots, [{"slot": 0, "bank": self.short_bank_document(4)}])
        with self.assertRaisesRegex(ValueError, "schemaVersion 13"):
            validate_recipe(recipe)

    def test_empty_bank_is_rejected(self) -> None:
        slots = ["dx7-bank-a"] + ["virtual-analog"] * 23
        recipe = self.v13_recipe(
            slots, [{"slot": 0, "bank": self.short_bank_document(0)}])
        with self.assertRaisesRegex(ValueError, "between 1 and 32"):
            validate_recipe(recipe)

    def test_malformed_custom_bank_is_rejected(self) -> None:
        slots = self.load("mixed_recipe.json")["slots"]
        short = self.bank_document()
        short["voices"][0]["packed"] = [0] * 127  # wrong packed length
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
        # Four-bank builds rotate orange to the front, keeping green and red at
        # their legacy internal indices while the cyclic order from green reads
        # green/red/amber/orange.
        self.assertLess(
            registrations.index("&loopback_engine_"),           # orange, internal first
            registrations.index("&virtual_analog_engine_"))     # green
        self.assertLess(
            registrations.index("&virtual_analog_engine_"),
            registrations.index("&swarm_engine_"))              # red
        self.assertLess(
            registrations.index("&swarm_engine_"),
            registrations.index("&virtual_analog_vcf_engine_")) # amber

    def test_24_slot_recipes_emit_the_default_engine_count(self) -> None:
        config = render_config(validate_recipe(self.load("default_recipe.json")))
        self.assertIn("#define PLAITS_ENGINE_COUNT 24", config)
        self.assertIn("kEngineUserDataBank[24]", config)

    def test_v6_custom_banks_are_optional(self) -> None:
        slots = self.load("default_recipe.json")["slots"]
        config = render_config(validate_recipe(self.v6_recipe(slots, [])))
        self.assertIn("#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0", config)

    # --- v7: short banks (empty slots) --------------------------------------

    def refs(self, engine_ids: list) -> list:
        """Build v7 slot references (or None for empties) as the editor emits them."""
        public = self.load("../plaits_lab_catalog/public_catalog.json")
        by_id = {engine["id"]: engine for engine in public["engines"]}
        return [
            None if engine_id is None else {
                "engine": engine_id,
                "package": by_id[engine_id]["packageId"],
                "version": by_id[engine_id]["version"],
                "digest": by_id[engine_id]["digest"],
            }
            for engine_id in engine_ids
        ]

    def v7_recipe(self, engine_ids: list) -> dict:
        resources: dict = {"chordTables": [dict(table) for table in DEFAULT_CHORD_TABLES]}
        if len(engine_ids) == 32:
            resources["userDataBanks"] = []
        return {
            "schemaVersion": 7,
            "target": "mutable-instruments-plaits",
            "firmware": "rubato-plaits",
            "output": "audio-wav",
            "slots": self.refs(engine_ids),
            "preferences": dict(DEFAULT_CONFIGURATION["preferences"]),
            "initialOptions": dict(DEFAULT_CONFIGURATION["initialOptions"]),
            "resources": resources,
        }

    def test_full_palettes_emit_full_bank_sizes(self) -> None:
        self.assertIn(
            "#define PLAITS_BANK_SIZES { 8, 8, 8 }",
            render_config(validate_recipe(self.load("default_recipe.json"))))
        self.assertIn(
            "#define PLAITS_BANK_SIZES { 8, 8, 8, 8 }",
            render_config(validate_recipe(self.v6_recipe(self.fourth_bank_slots(), []))))

    def test_v7_short_bank_emits_its_real_size(self) -> None:
        # Public green(8), red(3 + 5 empty), amber(8); internal amber,green,red.
        engine_ids = ["virtual-analog"] * 8 + ["virtual-analog"] * 3 + [None] * 5 + ["virtual-analog"] * 8
        config = render_config(validate_recipe(self.v7_recipe(engine_ids)))
        self.assertIn("#define PLAITS_ENGINE_COUNT 19", config)
        self.assertIn("#define PLAITS_BANK_SIZES { 8, 8, 3 }", config)
        self.assertIn("kEngineUserDataBank[19]", config)

    def test_v7_trailing_empty_bank_is_dropped(self) -> None:
        # Red empty -> internal amber,green,red = 8,8,0 -> drop the trailing 0.
        engine_ids = ["virtual-analog"] * 8 + [None] * 8 + ["virtual-analog"] * 8
        config = render_config(validate_recipe(self.v7_recipe(engine_ids)))
        self.assertIn("#define PLAITS_ENGINE_COUNT 16", config)
        self.assertIn("#define PLAITS_BANK_SIZES { 8, 8 }", config)

    def test_v7_interior_empty_bank_kept_as_zero(self) -> None:
        # Green empty -> internal amber,green,red = 8,0,8; the 0 stays so the red
        # bank keeps its LED color (bank index -> color must not shift).
        engine_ids = [None] * 8 + ["virtual-analog"] * 8 + ["virtual-analog"] * 8
        config = render_config(validate_recipe(self.v7_recipe(engine_ids)))
        self.assertIn("#define PLAITS_ENGINE_COUNT 16", config)
        self.assertIn("#define PLAITS_BANK_SIZES { 8, 0, 8 }", config)

    def test_v7_32_slot_short_orange_bank(self) -> None:
        engine_ids = ["virtual-analog"] * 24 + ["loopback", "lockstep"] + [None] * 6
        config = render_config(validate_recipe(self.v7_recipe(engine_ids)))
        self.assertIn("#define PLAITS_ENGINE_COUNT 26", config)
        self.assertIn("#define PLAITS_BANK_SIZES { 2, 8, 8, 8 }", config)

    def test_v7_four_bank_layout_keeps_trailing_empty_amber_bank(self) -> None:
        engine_ids = ["virtual-analog"] * 16 + [None] * 8 \
            + ["loopback", "lockstep"] + [None] * 6
        config = render_config(validate_recipe(self.v7_recipe(engine_ids)))
        self.assertIn("#define PLAITS_ENGINE_COUNT 18", config)
        self.assertIn("#define PLAITS_BANK_SIZES { 2, 8, 8, 0 }", config)
        self.assertIn(
            "Registry order: orange, green, red, amber.",
            config)

    def test_v7_hole_in_a_bank_is_rejected(self) -> None:
        engine_ids = ["virtual-analog", None, "virtual-analog"] + [None] * 5 \
            + ["virtual-analog"] * 8 + ["virtual-analog"] * 8
        with self.assertRaisesRegex(ValueError, "contiguous"):
            validate_recipe(self.v7_recipe(engine_ids))

    def test_v7_all_empty_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "at least one engine"):
            validate_recipe(self.v7_recipe([None] * 24))

    def test_empty_slot_requires_v7(self) -> None:
        recipe = self.v7_recipe(["virtual-analog"] * 8 + ["virtual-analog"] * 4 + [None] * 4 + ["virtual-analog"] * 8)
        recipe["schemaVersion"] = 6
        with self.assertRaisesRegex(ValueError, "schemaVersion 7"):
            validate_recipe(recipe)

    def v11_recipe(self, engine_ids: list) -> dict:
        recipe = self.v7_recipe(engine_ids)
        recipe["schemaVersion"] = 11
        return recipe

    def test_full_palette_emits_identity_engine_rows(self) -> None:
        # A fully-packed palette maps each engine to its own compact row, so
        # PLAITS_ENGINE_ROWS is the identity — byte-identical to pre-sparse builds.
        config = render_config(validate_recipe(self.load("default_recipe.json")))
        self.assertIn(
            "#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 }",
            config)

    def test_v11_sparse_bank_keeps_engine_rows(self) -> None:
        # Green bank holds engines at rows 0, 2, 4 (gaps at 1, 3 kept in place);
        # red and amber full. Internal order amber, green, red.
        engine_ids = (
            ["virtual-analog", None, "virtual-analog", None, "virtual-analog", None, None, None]
            + ["virtual-analog"] * 8
            + ["virtual-analog"] * 8
        )
        config = render_config(validate_recipe(self.v11_recipe(engine_ids)))
        self.assertIn("#define PLAITS_ENGINE_COUNT 19", config)
        self.assertIn("#define PLAITS_BANK_SIZES { 8, 3, 8 }", config)
        # amber 0..7, then green's kept rows 0,2,4, then red 0..7.
        self.assertIn(
            "#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 4, 0, 1, 2, 3, 4, 5, 6, 7 }",
            config)

    def test_sparse_bank_rejected_below_v11(self) -> None:
        # The same sparse layout is rejected on a v7 or v10 recipe — only v11 can
        # place engines on non-contiguous rows.
        engine_ids = (
            ["virtual-analog", None, "virtual-analog"] + [None] * 5
            + ["virtual-analog"] * 8 + ["virtual-analog"] * 8
        )
        for version in (7, 10):
            recipe = self.v11_recipe(engine_ids)
            recipe["schemaVersion"] = version
            with self.assertRaisesRegex(ValueError, "contiguous"):
                validate_recipe(recipe)

    def test_v7_normalized_bare_string_slots(self) -> None:
        # The Worker contract normalizes filled slots to bare engine IDs, so the
        # generator's production input is strings interleaved with None.
        engine_ids = ["virtual-analog"] * 8 + ["virtual-analog"] * 3 + [None] * 5 + ["virtual-analog"] * 8
        recipe = self.v7_recipe(engine_ids)
        recipe["slots"] = engine_ids  # bare strings + None, as normalizeRecipe emits
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_ENGINE_COUNT 19", config)
        self.assertIn("#define PLAITS_BANK_SIZES { 8, 8, 3 }", config)

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
        self.assertIn("#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION 1", config)
        self.assertIn("#define PLAITS_BUILD_MODEL_CV_OPTION 2", config)
        self.assertIn("#define PLAITS_BUILD_LEVEL_CV_OPTION 1", config)
        # A sine suboscillator two octaves down: aux output 2 (suboscillator),
        # and the sine half of the subosc light at its -2 octave value.
        self.assertIn("#define PLAITS_BUILD_AUX_OUTPUT_OPTION 2", config)
        self.assertIn("#define PLAITS_BUILD_AUX_SUBOSC_OPTION 5", config)
        self.assertIn("#define PLAITS_BUILD_CHORD_SET_OPTION 2", config)
        self.assertIn("#define PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION 1", config)
        self.assertIn("#define PLAITS_BUILD_ATTENUVERTER_MODE 0", config)
        self.assertRegex(config, r"#define PLAITS_BUILD_OPTIONS_PROFILE_ID 0x[0-9a-f]{6}u")

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
        self.assertIn("#define PLAITS_BUILD_MODEL_CV_OPTION 1", config)
        self.assertIn("#define PLAITS_BUILD_LEVEL_CV_OPTION 0", config)

        # The fourth macro moved off the LEVEL input, so requesting it there
        # is no longer a supported firmware option.
        recipe["initialOptions"]["modelInput"] = "model"
        recipe["initialOptions"]["levelInput"] = "macro-4"
        with self.assertRaisesRegex(ValueError, "unsupported firmware option"):
            validate_recipe(recipe)

    def test_stereo_aux_output_emits_wave_option_one_under_v9(self) -> None:
        public = self.load("../plaits_lab_catalog/public_catalog.json")
        by_id = {engine["id"]: engine for engine in public["engines"]}
        recipe = self.load("default_recipe.json")
        recipe.update({
            "schemaVersion": 9,
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
                "auxOutput": "stereo",
                "suboscillatorOctave": 0,
                "chordTable": DEFAULT_CHORD_TABLES[0]["id"],
                "holdOnTrigger": False,
            },
            "resources": {"chordTables": DEFAULT_CHORD_TABLES},
        })
        config = render_config(validate_recipe(recipe))
        # Stereo (OUT/AUX as a true L/R pair) sits at aux value 1, one step off
        # the regular aux model, and only a v9 builder accepts it.
        self.assertIn("#define PLAITS_BUILD_AUX_OUTPUT_OPTION 1", config)

    def test_v10_parses_the_per_engine_stereo_list(self) -> None:
        public = self.load("../plaits_lab_catalog/public_catalog.json")
        by_id = {engine["id"]: engine for engine in public["engines"]}
        recipe = self.load("default_recipe.json")
        recipe.update({
            "schemaVersion": 10,
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
                "auxOutput": "stereo",
                "suboscillatorOctave": 0,
                "chordTable": DEFAULT_CHORD_TABLES[0]["id"],
                "holdOnTrigger": False,
            },
            "resources": {"chordTables": DEFAULT_CHORD_TABLES},
            "stereoEngines": ["chiptune", "modal-resonator", "chiptune"],
        })
        built = validate_recipe(recipe)
        self.assertEqual(built.stereo_engines, ("chiptune", "modal-resonator"))  # deduped

        # A v10 recipe must carry the list; an unknown id is rejected.
        no_list = {k: v for k, v in recipe.items() if k != "stereoEngines"}
        with self.assertRaisesRegex(ValueError, "stereoEngines"):
            validate_recipe(no_list)
        recipe["stereoEngines"] = ["not-an-engine"]
        with self.assertRaisesRegex(ValueError, "approved engine ids"):
            validate_recipe(recipe)
        # stereoEngines on a schema-9 recipe is refused.
        recipe["stereoEngines"] = ["chiptune"]
        recipe["schemaVersion"] = 9
        with self.assertRaisesRegex(ValueError, "schemaVersion 10"):
            validate_recipe(recipe)

    def test_v13_short_fm_bank_allows_per_engine_stereo(self) -> None:
        # v13 adds variable-length FM banks on top of v12 and v10. The Worker
        # accepts this combination, so the container must preserve both features.
        slots = ["dx7-bank-a"] + ["virtual-analog"] * 23
        recipe = self.v13_recipe(
            slots, [{"slot": 0, "bank": self.short_bank_document(4)}])
        recipe["initialOptions"]["auxOutput"] = "stereo"
        recipe["stereoEngines"] = ["virtual-analog"]

        built = validate_recipe(recipe)

        self.assertEqual(built.stereo_engines, ("virtual-analog",))
        self.assertEqual(len(built.slot_bank_overrides[0][1]), 4 * 128)

    def test_v14_calibration_allows_per_engine_stereo(self) -> None:
        # v14 adds the optional calibration procedure on top of every prior
        # feature. A calibrated stereo build must not be rejected by the
        # container after it has passed the Worker's superset-aware contract.
        recipe = self.calibration_recipe(14, True)
        recipe["initialOptions"]["auxOutput"] = "stereo"
        recipe["stereoEngines"] = ["virtual-analog"]

        built = validate_recipe(recipe)

        self.assertEqual(built.stereo_engines, ("virtual-analog",))
        self.assertEqual(built.enable_calibration, 1)

    def test_every_later_supported_schema_inherits_per_engine_stereo(self) -> None:
        # Adding a schema should require changing only the shared ceiling, not
        # discovering another stale stereo whitelist in this container.
        for version in range(10, MAX_RECIPE_SCHEMA_VERSION + 1):
            recipe = self.calibration_recipe(version, False)
            recipe["initialOptions"]["auxOutput"] = "stereo"
            if version >= ATTENUVERTER_MODE_MIN_SCHEMA_VERSION:
                recipe["initialOptions"]["attenuverterMode"] = "stock"
            recipe["stereoEngines"] = ["virtual-analog"]
            if version in (12, 13):
                recipe["resources"]["userDataBanks"] = []
            with self.subTest(schema_version=version):
                self.assertEqual(
                    validate_recipe(recipe).stereo_engines,
                    ("virtual-analog",),
                )

    def test_v10_allows_empty_slots(self) -> None:
        # Per-engine stereo (v10) is a superset of v7-v9 and must accept empty
        # slots — a stereo palette with a model deleted (e.g. the Stereo Dreams
        # preset, which drops Speech). The Worker contract already allows this;
        # the generator formerly rejected it (empty-slot gate stuck at 7,8,9),
        # so the Worker accepted a recipe the container then failed.
        public = self.load("../plaits_lab_catalog/public_catalog.json")
        by_id = {engine["id"]: engine for engine in public["engines"]}
        recipe = self.load("default_recipe.json")
        slot_ids = list(recipe["slots"])
        slot_ids[7] = None  # drop one model -> empty slot
        recipe.update({
            "schemaVersion": 10,
            "slots": [
                None if engine_id is None else {
                    "engine": engine_id,
                    "package": by_id[engine_id]["packageId"],
                    "version": by_id[engine_id]["version"],
                    "digest": by_id[engine_id]["digest"],
                }
                for engine_id in slot_ids
            ],
            "preferences": {"navigationMode": "linear"},
            "initialOptions": {
                "lockedFrequencyKnob": "octaves",
                "modelInput": "model",
                "levelInput": "level",
                "auxOutput": "stereo",
                "suboscillatorOctave": 0,
                "chordTable": DEFAULT_CHORD_TABLES[0]["id"],
                "holdOnTrigger": False,
            },
            "resources": {"chordTables": DEFAULT_CHORD_TABLES},
            "stereoEngines": ["chiptune", "modal-resonator"],
        })
        config = render_config(validate_recipe(recipe))
        # 24 slots minus the one empty -> 23 engines. The public green bank holds
        # the hole (slot 7); in registry order (amber, green, red) it reads
        # { 8, 7, 8 }. default_recipe's slot 7 is Speech, so this is exactly the
        # Stereo Dreams shape (Speech dropped).
        self.assertIn("#define PLAITS_ENGINE_COUNT 23", config)
        self.assertIn("#define PLAITS_BANK_SIZES { 8, 7, 8 }", config)
        self.assertIn("#define PLAITS_HAS_SPEECH_ENGINE 0", config)

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

    def _local_chord_tables(self, count: int) -> list:
        base = DEFAULT_CHORD_TABLES[0]
        tables = []
        for n in range(count):
            table = json.loads(json.dumps(base))
            table["id"] = f"filler-{n}"
            table["packageId"] = f"local/filler-{n}"
            table["version"] = "draft"
            table["digest"] = None
            table["name"] = f"Filler {n}"
            table["origin"] = "Local"
            tables.append(table)
        return tables

    def test_v8_accepts_nine_chord_tables_and_emits_the_count(self) -> None:
        recipe = self.v7_recipe(["virtual-analog"] * 24)
        recipe["schemaVersion"] = 8
        recipe["initialOptions"] = {**recipe["initialOptions"], "chordTable": "filler-0"}
        recipe["resources"] = {"chordTables": self._local_chord_tables(9)}
        config = render_config(validate_recipe(recipe))
        self.assertIn("#define PLAITS_CHORD_TABLE_COUNT 9", config)

    def test_v8_rejects_ten_chord_tables(self) -> None:
        recipe = self.v7_recipe(["virtual-analog"] * 24)
        recipe["schemaVersion"] = 8
        recipe["initialOptions"] = {**recipe["initialOptions"], "chordTable": "filler-0"}
        recipe["resources"] = {"chordTables": self._local_chord_tables(10)}
        with self.assertRaisesRegex(ValueError, "between one and nine"):
            validate_recipe(recipe)


if __name__ == "__main__":
    unittest.main()
