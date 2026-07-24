#!/usr/bin/env python3
"""Generate the compile-time Plaits engine registry for an approved recipe.

The input recipe is always expressed in the public green/red/amber order.  The
generated registry is deliberately emitted in Plaits' internal amber/green/red
order.  Only identifiers in this file's catalog can influence C++ output.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


CATALOG_PATH = Path(__file__).resolve().parents[1] / "plaits_lab_catalog/catalog.json"
PUBLIC_CATALOG_PATH = Path(__file__).resolve().parents[1] / "plaits_lab_catalog/public_catalog.json"
CHORD_CATALOG_PATH = Path(__file__).resolve().parents[1] / "plaits_lab_chord_tables/catalog.json"


@dataclass(frozen=True)
class Engine:
    header: str
    class_name: str
    member: str
    already_enveloped: bool
    out_gain: float
    aux_gain: float
    user_data_bank: int = -1
    behavior: str = "standard"


# The module has three built-in six-operator FM banks; a recipe may override any
# of them with a custom 32-patch bank (128 packed bytes/patch = 4096 bytes/bank).
MAX_USER_DATA_BANKS = 3
PATCHES_PER_BANK = 32
PACKED_PATCH_SIZE = 128
PACKED_BANK_SIZE = PATCHES_PER_BANK * PACKED_PATCH_SIZE  # 4096


@dataclass(frozen=True)
class BuildRecipe:
    public_slots: list[str]
    chord_tables: list[dict[str, Any]]
    navigation_mode: int
    locked_frequency_pot_option: int
    model_cv_option: int
    level_cv_option: int
    aux_subosc_wave_option: int
    aux_subosc_octave_option: int
    chord_set_option: int
    hold_on_trigger_option: int
    options_profile_id: int
    # index (0..2) -> 4096 packed bytes for a custom bank overriding a built-in one.
    user_data_bank_overrides: tuple[tuple[int, bytes], ...] = ()
    # Catalog ids of engines built with the stereo (OUT/AUX L/R) render path.
    # None means "not specified" — a pre-per-engine (schema <= 9) recipe, which
    # the builder treats as all stereo-capable engines when the aux option is
    # stereo. A tuple (schema 10) lists exactly the enabled engines.
    stereo_engines: tuple[str, ...] | None = None


def load_catalog() -> dict[str, Engine]:
    value = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if value.get("schemaVersion") != 1:
        raise ValueError("unsupported engine catalog schema")
    result: dict[str, Engine] = {}
    for item in value["engines"]:
        source = item["source"]
        post = item["postProcessing"]
        result[item["id"]] = Engine(
            header=source["header"],
            class_name=source["className"],
            member=source["member"],
            already_enveloped=post["alreadyEnveloped"],
            out_gain=post["outGain"],
            aux_gain=post["auxGain"],
            user_data_bank=source.get("userDataBank", -1),
            behavior=source.get("behavior", "standard"),
        )
    return result


CATALOG = load_catalog()
PUBLIC_ENGINES = {
    item["id"]: item
    for item in json.loads(PUBLIC_CATALOG_PATH.read_text(encoding="utf-8"))["engines"]
}
APPROVED_CHORD_TABLES = {
    item["id"]: item
    for item in json.loads(CHORD_CATALOG_PATH.read_text(encoding="utf-8"))["tables"]
}


DEFAULT_CONFIGURATION = {
    "preferences": {"navigationMode": "linear"},
    "initialOptions": {
        "lockedFrequencyKnob": "octaves",
        "modelInput": "model",
        "levelInput": "level",
        "auxOutput": "alternate-model",
        "suboscillatorOctave": 0,
        "chordTable": "original",
        "holdOnTrigger": False,
    },
}
DEFAULT_CHORD_TABLES = list(APPROVED_CHORD_TABLES.values())


def validate_chord_tables(value: Any) -> list[dict[str, Any]]:
    if not isinstance(value, list) or not 1 <= len(value) <= 9:
        raise ValueError("recipe must contain between one and nine chord tables")
    result: list[dict[str, Any]] = []
    table_ids: set[str] = set()
    for table in value:
        if not isinstance(table, dict) or set(table) != {
            "id", "packageId", "version", "digest", "name", "author",
            "license", "origin", "description", "chords",
        }:
            raise ValueError("recipe contains invalid chord-table metadata")
        table_id = table.get("id")
        if not isinstance(table_id, str) or not table_id or len(table_id) > 80 or table_id in table_ids:
            raise ValueError("recipe contains an invalid or duplicate chord-table ID")
        if not all(character.islower() or character.isdigit() or character == "-" for character in table_id):
            raise ValueError("recipe contains an invalid chord-table ID")
        if any(not isinstance(table.get(key), str) or not table[key] for key in (
            "packageId", "version", "name", "author", "license", "origin", "description",
        )):
            raise ValueError("recipe contains invalid chord-table metadata")
        chords = table.get("chords")
        if not isinstance(chords, list) or not 1 <= len(chords) <= 24:
            raise ValueError("a chord table must contain between one and 24 positions")
        chord_ids: set[str] = set()
        for chord in chords:
            if not isinstance(chord, dict) or set(chord) != {"id", "name", "voices", "arpLength"}:
                raise ValueError("recipe contains an invalid chord position")
            chord_id = chord.get("id")
            voices = chord.get("voices")
            if not isinstance(chord_id, str) or not chord_id or chord_id in chord_ids \
                    or not isinstance(chord.get("name"), str) or not chord["name"] \
                    or not isinstance(voices, list) or len(voices) != 4 \
                    or any(type(voice) is not int or voice < -4800 or voice > 7200 for voice in voices) \
                    or chord.get("arpLength") not in (1, 2, 3, 4):
                raise ValueError("a chord position must contain four bounded cent offsets")
            chord_ids.add(chord_id)
        digest = table.get("digest")
        if digest is not None:
            if APPROVED_CHORD_TABLES.get(table_id) != table:
                raise ValueError("recipe contains an unavailable published chord table")
        elif table.get("origin") != "Local" or not table["packageId"].startswith("local/"):
            raise ValueError("editable chord tables must be device-local drafts")
        table_ids.add(table_id)
        result.append(table)
    return result


def validate_user_data_banks(value: Any) -> list[tuple[int, bytes]]:
    """Validate a v6 recipe's custom FM banks into (index, 4096 packed bytes).

    The packed bytes are baked verbatim into the firmware, so they are checked
    exactly: 32 patches x 128 bytes, every byte a 7-bit value. Metadata is
    validated for shape only (it never reaches the ARM build).
    """
    if not isinstance(value, list) or len(value) > MAX_USER_DATA_BANKS:
        raise ValueError("recipe must contain between zero and three custom banks")
    result: list[tuple[int, bytes]] = []
    seen: set[int] = set()
    for entry in value:
        if not isinstance(entry, dict) or set(entry) != {"index", "bank"}:
            raise ValueError("recipe contains an invalid custom-bank assignment")
        index = entry["index"]
        if type(index) is not int or not 0 <= index < MAX_USER_DATA_BANKS or index in seen:
            raise ValueError("a custom bank must target a distinct built-in FM bank (0-2)")
        seen.add(index)
        bank = entry["bank"]
        if not isinstance(bank, dict):
            raise ValueError("recipe contains an invalid custom bank")
        voices = bank.get("voices")
        if not isinstance(voices, list) or len(voices) != PATCHES_PER_BANK:
            raise ValueError("a custom bank must contain exactly 32 voices")
        packed = bytearray()
        for voice in voices:
            if not isinstance(voice, dict):
                raise ValueError("a custom-bank voice is invalid")
            data = voice.get("packed")
            if not isinstance(data, list) or len(data) != PACKED_PATCH_SIZE \
                    or any(type(byte) is not int or byte < 0 or byte > 127 for byte in data):
                raise ValueError("a custom-bank voice must have 128 packed 7-bit bytes")
            packed.extend(data)
        result.append((index, bytes(packed)))
    return result


def normalize_slots(slots: list[Any], schema_version: int) -> list[str | None]:
    if schema_version in (2, 4, 5, 6) and all(isinstance(engine_id, str) for engine_id in slots):
        if any(engine_id not in CATALOG for engine_id in slots):
            raise ValueError("recipe contains an unapproved engine ID")
        return list(slots)

    # v7+ short-bank recipes carry null entries for empty slots; keep them as None
    # so render_config can size each bank. v5/v6 are always fully filled. (v10 —
    # per-engine stereo — is a superset of v7/v8/v9 and must allow empties too;
    # the Worker contract already does, so omitting 10 here rejected a recipe the
    # Worker had accepted, e.g. a stereo palette with a model deleted.)
    normalized: list[str | None] = []
    for reference in slots:
        if reference is None:
            if schema_version not in (7, 8, 9, 10, 11):
                raise ValueError("empty slots require schemaVersion 7, 8, 9, 10, or 11")
            normalized.append(None)
            continue
        if isinstance(reference, str):
            # The Worker contract normalizes every filled slot to a bare engine ID,
            # so a v7 recipe reaches the generator as engine IDs interleaved with
            # None. Validate the ID against the approved catalog, as v5 does.
            if reference not in CATALOG:
                raise ValueError("recipe contains an unapproved engine ID")
            normalized.append(reference)
            continue
        if not isinstance(reference, dict) or not isinstance(reference.get("engine"), str):
            raise ValueError("recipe contains an invalid package reference")
        approved = PUBLIC_ENGINES.get(reference["engine"])
        if not approved or any(
            reference.get(key) != approved[approved_key]
            for key, approved_key in (
                ("package", "packageId"),
                ("version", "version"),
                ("digest", "digest"),
            )
        ):
            raise ValueError("recipe contains an unavailable package version")
        normalized.append(reference["engine"])
    return normalized


def has_sparse_bank(public_slots: list[str | None]) -> bool:
    """A bank is "sparse" when an empty slot has a filled slot AFTER it in the
    same bank — a gap the user kept in place. The firmware holds each engine on
    its own LED row for these; a bank whose empties only TRAIL is a plain short
    bank, not sparse."""
    for start in range(0, len(public_slots), 8):
        seen_empty = False
        for engine_id in public_slots[start:start + 8]:
            if engine_id is None:
                seen_empty = True
            elif seen_empty:
                return True
    return False


def validate_bank_shape(public_slots: list[str | None], schema_version: int) -> None:
    """The palette must hold at least one engine. A bank's engines may be sparse
    (a gap kept in place, not compacted to the front) only on a schemaVersion 11
    recipe — the module firmware then keeps each engine on its physical LED row.
    Older recipes must keep each bank's engines contiguous at its front, the
    shape the pre-sparse navigation assumed."""
    if all(engine_id is None for engine_id in public_slots):
        raise ValueError("recipe must contain at least one engine")
    if has_sparse_bank(public_slots) and schema_version < 11:
        raise ValueError("a bank's engines must be contiguous (empty slots only "
                         "at the end) unless the recipe uses schemaVersion 11")


def validate_recipe(value: Any) -> BuildRecipe:
    if not isinstance(value, dict):
        raise ValueError("recipe must be a JSON object")
    schema_version = value.get("schemaVersion")
    if schema_version not in (2, 3, 4, 5, 6, 7, 8, 9, 10, 11):
        raise ValueError("recipe schemaVersion must be 2, 3, 4, 5, 6, 7, 8, 9, 10, or 11")
    if value.get("target") != "mutable-instruments-plaits":
        raise ValueError("unsupported firmware target")
    if value.get("firmware") != "rubato-plaits":
        raise ValueError("unsupported firmware family")
    if value.get("output") != "audio-wav":
        raise ValueError("unsupported output format")
    slots = value.get("slots")
    if not isinstance(slots, list) or len(slots) not in (24, 32):
        raise ValueError("recipe must contain 24 slots, or 32 for a four-bank build")
    if len(slots) == 32 and schema_version not in (6, 7, 8, 9, 10, 11):
        raise ValueError("32-slot recipes require schemaVersion 6, 7, 8, 9, 10, or 11")
    public_slots = normalize_slots(slots, schema_version)
    validate_bank_shape(public_slots, schema_version)
    user_data_banks: list[tuple[int, bytes]] = []
    if schema_version in (5, 6, 7, 8, 9, 10, 11):
        resources = value.get("resources")
        # v6 always carries the custom-FM-banks resource (its defining feature).
        # v7+ mirror the editor: userDataBanks only for a 32-slot (fourth-bank)
        # recipe; a 24-slot v7+ carries chord tables only, like v5.
        expect_user_data_banks = schema_version == 6 or (schema_version in (7, 8, 9, 10, 11) and len(slots) == 32)
        expected_resource_keys = {"chordTables", "userDataBanks"} if expect_user_data_banks else {"chordTables"}
        if not isinstance(resources, dict) or set(resources) != expected_resource_keys:
            raise ValueError("recipe must contain only supported firmware resources")
        chord_tables = validate_chord_tables(resources.get("chordTables"))
        if expect_user_data_banks:
            user_data_banks = validate_user_data_banks(resources.get("userDataBanks"))
    else:
        chord_tables = validate_chord_tables(DEFAULT_CHORD_TABLES)
    configuration = value if schema_version in (4, 5, 6, 7, 8, 9, 10, 11) else DEFAULT_CONFIGURATION
    preferences = configuration.get("preferences")
    options = configuration.get("initialOptions")
    if not isinstance(preferences, dict) or not isinstance(options, dict):
        raise ValueError("recipe must contain firmware preferences and starting options")
    if set(preferences) != {"navigationMode"} or set(options) != {
        "lockedFrequencyKnob", "modelInput", "levelInput", "auxOutput",
        "suboscillatorOctave", "chordTable", "holdOnTrigger",
    }:
        raise ValueError("recipe contains an unsupported firmware option")

    mappings = {
        "navigation_mode": (preferences.get("navigationMode"), {"linear": 0, "banked": 1}),
        "locked_frequency_pot_option": (options.get("lockedFrequencyKnob"), {"octaves": 0, "decay": 1, "aux-crossfade": 2, "macro-4": 3}),
        "model_cv_option": (options.get("modelInput"), {"model": 0, "lpg-colour": 1, "aux-crossfade": 2, "macro-4": 3}),
        "level_cv_option": (options.get("levelInput"), {"level": 0, "decay": 1}),
        "aux_subosc_wave_option": (options.get("auxOutput"), {"alternate-model": 0, "square-subosc": 1, "sine-subosc": 2, "stereo": 3}),
        "aux_subosc_octave_option": (options.get("suboscillatorOctave"), {0: 0, -1: 1, -2: 2}),
        "chord_set_option": (options.get("chordTable"), {
            table["id"]: index for index, table in enumerate(chord_tables)
        }),
        "hold_on_trigger_option": (options.get("holdOnTrigger"), {False: 0, True: 1}),
    }
    normalized_options: dict[str, int] = {}
    for name, (selected, allowed) in mappings.items():
        if selected not in allowed or (name == "hold_on_trigger_option" and not isinstance(selected, bool)):
            raise ValueError("recipe contains an unsupported firmware option")
        normalized_options[name] = allowed[selected]

    profile_code = normalized_options["locked_frequency_pot_option"]
    for name, radix in (
        ("model_cv_option", 4),
        ("level_cv_option", 2),
        # The aux/subosc-wave option gained a fourth value (stereo OUT/AUX), so
        # its radix is 4; a default (all-zero) recipe still folds to 0, keeping
        # its profile id — and thus the default build — unchanged.
        ("aux_subosc_wave_option", 4),
        ("aux_subosc_octave_option", 3),
        ("chord_set_option", 6),
        ("hold_on_trigger_option", 2),
    ):
        profile_code = profile_code * radix + normalized_options[name]
    # The reversible encoding stays well under the reserved range (max profile
    # code < 254*256), so it still fits the legacy navigation and padding bytes,
    # while reserving low bytes 0 and 1 so saved states from the old navigation
    # setting can never look initialized.
    profile_id = ((profile_code // 254) << 8) | (2 + profile_code % 254)

    # Per-engine stereo (schema 10): stereoEngines lists the catalog ids built
    # with the stereo render path. Absent on schema <= 9, which the container
    # treats as "all stereo-capable engines" (back-compat with the global gate).
    stereo_engines: tuple[str, ...] | None = None
    if "stereoEngines" in value:
        # v10's defining feature; a v11 (sparse) recipe may also carry it when its
        # aux output is stereo, since v11 is a superset of v10.
        if schema_version not in (10, 11):
            raise ValueError("stereoEngines requires schemaVersion 10 or 11")
        raw = value.get("stereoEngines")
        if not isinstance(raw, list) or not all(
            isinstance(engine_id, str) and engine_id in PUBLIC_ENGINES for engine_id in raw
        ):
            raise ValueError("stereoEngines must list approved engine ids")
        stereo_engines = tuple(dict.fromkeys(raw))
    elif schema_version == 10:
        raise ValueError("schemaVersion 10 recipes must carry a stereoEngines list")

    return BuildRecipe(
        public_slots=public_slots,
        chord_tables=chord_tables,
        options_profile_id=profile_id,
        user_data_bank_overrides=tuple(user_data_banks),
        stereo_engines=stereo_engines,
        **normalized_options,
    )


def cpp_float(value: float) -> str:
    return f"{value:.1f}f"


def cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def render_config(recipe: BuildRecipe) -> str:
    public_slots = recipe.public_slots
    # Public order is green, red, amber (+ optional orange); the registry is
    # emitted in the module's internal amber, green, red (+ orange) order. Empty
    # slots (None — v7 short banks) are dropped, and each internal bank's engine
    # count becomes PLAITS_BANK_SIZES so navigation wraps at the real size.
    public_banks = [public_slots[i:i + 8] for i in range(0, len(public_slots), 8)]
    internal_order = [2, 0, 1] + ([3] if len(public_banks) > 3 else [])
    # Each internal bank keeps its filled engines paired with their PHYSICAL row
    # (0..7 within the public bank). Empty slots are dropped from the engine list
    # (navigation stays compact) but their positions survive as gaps in the rows
    # of the engines that follow — that row map becomes PLAITS_ENGINE_ROWS so the
    # module lights each engine at its kept LED position.
    internal_banks = [
        [(engine_id, row) for row, engine_id in enumerate(public_banks[bank])
         if engine_id is not None]
        for bank in internal_order
    ]
    bank_sizes = [len(bank) for bank in internal_banks]
    # Drop trailing empty banks (no engines, and no later bank whose LED color
    # would shift); interior/leading empties stay 0 to keep bank->color aligned.
    while len(bank_sizes) > 1 and bank_sizes[-1] == 0:
        bank_sizes.pop()
    internal_slots = [engine_id for bank in internal_banks for engine_id, _ in bank]
    engine_rows = [row for bank in internal_banks for _, row in bank]
    selected = [CATALOG[engine_id] for engine_id in internal_slots]

    unique: list[Engine] = []
    seen_members: set[str] = set()
    for selected_engine in selected:
        if selected_engine.member not in seen_members:
            seen_members.add(selected_engine.member)
            unique.append(selected_engine)

    includes = "\n".join(f'#include "{item.header}"' for item in unique)
    continuation = " " + "\\" + "\n  "
    members = continuation.join(f"{item.class_name} {item.member};" for item in unique)
    registrations = continuation.join(
        "(registry).RegisterInstance(&{member}, {enveloped}, {out_gain}, {aux_gain});".format(
            member=item.member,
            enveloped=cpp_bool(item.already_enveloped),
            out_gain=cpp_float(item.out_gain),
            aux_gain=cpp_float(item.aux_gain),
        )
        for item in selected
    )
    user_data_banks = ", ".join(str(item.user_data_bank) for item in selected)
    speech_mask = sum(1 << index for index, item in enumerate(selected) if item.behavior == "speech")
    chiptune_mask = sum(1 << index for index, item in enumerate(selected) if item.behavior == "chiptune")
    chord_offsets: list[int] = []
    chord_sizes: list[int] = []
    chord_cents: list[str] = []
    chord_arp_lengths: list[str] = []
    chord_offset = 0
    for table in recipe.chord_tables:
        chord_offsets.append(chord_offset)
        chord_sizes.append(len(table["chords"]))
        for chord in table["chords"]:
            chord_cents.append("{ " + ", ".join(str(value) for value in chord["voices"]) + " }")
            chord_arp_lengths.append(str(chord["arpLength"]))
            chord_offset += 1

    # Custom-bank overrides: bake only banks a placed engine actually uses, so an
    # orphaned assignment (its engine removed) never bloats the firmware. Each
    # override replaces the built-in fm_patches_table[index] default; a runtime
    # TIMBRE-loaded user bank still takes precedence in voice.cc.
    used_banks = {item.user_data_bank for item in selected if item.user_data_bank >= 0}
    active_overrides = [
        (index, data) for index, data in recipe.user_data_bank_overrides if index in used_banks
    ]
    override_index = {index for index, _ in active_overrides}
    override_arrays = "\n".join(
        "static const uint8_t kUserDataBankOverride_{index}[{size}] = {{ {body} }};".format(
            index=index, size=PACKED_BANK_SIZE, body=", ".join(str(byte) for byte in data)
        )
        for index, data in active_overrides
    )
    override_table = "static const uint8_t* const kUserDataBankOverride[{count}] = {{ {pointers} }};".format(
        count=MAX_USER_DATA_BANKS,
        pointers=", ".join(
            f"kUserDataBankOverride_{i}" if i in override_index else "NULL"
            for i in range(MAX_USER_DATA_BANKS)
        ),
    ) if active_overrides else ""
    user_data_bank_override_block = (
        f"\n#if PLAITS_HAS_USER_DATA_BANK_OVERRIDE\n{override_arrays}\n{override_table}\n#endif\n"
        if active_overrides else ""
    )

    return f"""// Generated by alt_firmwares/plaits_lab_builder/generate_engine_config.py.
// Public recipe order: green, red, amber. Registry order: amber, green, red.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

{includes}

#define PLAITS_ENGINE_COUNT {len(selected)}
#define PLAITS_BANK_SIZES {{ {", ".join(str(size) for size in bank_sizes)} }}
#define PLAITS_ENGINE_ROWS {{ {", ".join(str(row) for row in engine_rows)} }}
#define PLAITS_HAS_SPEECH_ENGINE {1 if any(item.behavior == 'speech' for item in selected) else 0}
#define PLAITS_HAS_CHIPTUNE_ENGINE {1 if any(item.behavior == 'chiptune' for item in selected) else 0}
#define PLAITS_HAS_USER_DATA_BANK {1 if any(item.user_data_bank >= 0 for item in selected) else 0}
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE {1 if active_overrides else 0}

#define PLAITS_CHORD_TABLE_COUNT {len(recipe.chord_tables)}
#define PLAITS_CHORD_COUNT {chord_offset}
#define PLAITS_CHORD_TABLE_OFFSETS {{ {", ".join(str(value) for value in chord_offsets)} }}
#define PLAITS_CHORD_TABLE_SIZES {{ {", ".join(str(value) for value in chord_sizes)} }}
#define PLAITS_CHORD_CENTS {{ {", ".join(chord_cents)} }}
#define PLAITS_CHORD_ARP_LENGTHS {{ {", ".join(chord_arp_lengths)} }}

#define PLAITS_BUILD_NAVIGATION_MODE {recipe.navigation_mode}
#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION {recipe.locked_frequency_pot_option}
#define PLAITS_BUILD_MODEL_CV_OPTION {recipe.model_cv_option}
#define PLAITS_BUILD_LEVEL_CV_OPTION {recipe.level_cv_option}
#define PLAITS_BUILD_AUX_SUBOSC_WAVE_OPTION {recipe.aux_subosc_wave_option}
#define PLAITS_BUILD_AUX_SUBOSC_OCTAVE_OPTION {recipe.aux_subosc_octave_option}
#define PLAITS_BUILD_CHORD_SET_OPTION {recipe.chord_set_option}
#define PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION {recipe.hold_on_trigger_option}
#define PLAITS_BUILD_OPTIONS_PROFILE_ID 0x{recipe.options_profile_id:04x}u

#define PLAITS_ENGINE_MEMBERS \\
  {members}

#define PLAITS_REGISTER_ENGINES(registry) do {{ \\
  {registrations} \\
}} while (0)

namespace plaits {{

#if PLAITS_HAS_USER_DATA_BANK
static const int8_t kEngineUserDataBank[{len(selected)}] = {{ {user_data_banks} }};
#endif
{user_data_bank_override_block}
#if PLAITS_HAS_SPEECH_ENGINE
static const uint32_t kSpeechEngineMask = 0x{speech_mask:08x};
#endif
#if PLAITS_HAS_CHIPTUNE_ENGINE
static const uint32_t kChiptuneEngineMask = 0x{chiptune_mask:08x};
#endif

}}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("recipe", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
    validated_recipe = validate_recipe(recipe)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render_config(validated_recipe), encoding="utf-8")


if __name__ == "__main__":
    main()
