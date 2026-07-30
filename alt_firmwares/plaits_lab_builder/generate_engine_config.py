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

# Keep this ceiling in lockstep with src/contract.ts. Feature gates use minimum
# versions so adding a schema at the ceiling does not require extending a trail
# of "10, 11, 12..." whitelists in the build container.
MIN_RECIPE_SCHEMA_VERSION = 2
MAX_RECIPE_SCHEMA_VERSION = 15
CONFIGURATION_MIN_SCHEMA_VERSION = 4
RESOURCES_MIN_SCHEMA_VERSION = 5
FOUR_BANK_MIN_SCHEMA_VERSION = 6
SPARSE_SLOT_MIN_SCHEMA_VERSION = 7
STEREO_ENGINES_MIN_SCHEMA_VERSION = 10
SPARSE_BANK_MIN_SCHEMA_VERSION = 11
SLOT_BANK_MIN_SCHEMA_VERSION = 12
SHORT_BANK_MIN_SCHEMA_VERSION = 13
CALIBRATION_MIN_SCHEMA_VERSION = 14
ROVED_MIN_SCHEMA_VERSION = 15
COLOR_BLIND_MODE_MIN_SCHEMA_VERSION = 15

# Bumped whenever a stored option value changes meaning in the firmware (see the
# value tables in plaits/dsp/voice.h). It seeds the options profile-id fold, so a
# bump moves every build to an id no old firmware ever minted — which is what
# makes a module reset its saved options to the recipe's exactly once instead of
# reinterpreting the old numbers. Version 1: options menu reordered, and the
# fourth macro / stereo promoted to value 1 on their lights (2026-07). Version 2:
# the aux output holds one suboscillator value instead of two (square/sine), and
# the suboscillator light carries shape and octave together (2026-07).
OPTIONS_LAYOUT_VERSION = 2


@dataclass(frozen=True)
class BuildRecipe:
    public_slots: list[str]
    chord_tables: list[dict[str, Any]]
    navigation_mode: int
    locked_frequency_pot_option: int
    model_cv_option: int
    level_cv_option: int
    aux_output_option: int
    aux_subosc_option: int
    chord_set_option: int
    hold_on_trigger_option: int
    options_profile_id: int
    # v14: 1 compiles the CV calibration procedure in (held right button at
    # power-up), 0 leaves it out. Not an options-menu setting and not part of the
    # saved State — it is present in the binary or it is not.
    enable_calibration: int = 0
    # v15: 1 compiles the four-clickable-knob Plum Audio Ro'Ved panel UI and
    # extra switch GPIOs; 0 keeps the two-button Mutable Instruments panel.
    roved_panel: int = 0
    # v15: 1 bakes the accessible bank display into the firmware (one yellow hue,
    # four brightness levels), 0 keeps the normal bank colors. Like calibration,
    # this is not stored and does not belong in the options profile-id fold.
    color_blind_mode: int = 0
    # v6 (index-keyed): built-in bank index (0..2) -> 4096 packed bytes, overriding
    # a factory bank globally for every slot that uses it.
    user_data_bank_overrides: tuple[tuple[int, bytes], ...] = ()
    # v12 (slot-keyed): public slot index -> 4096 packed bytes, a bank baked for
    # THAT slot alone. Each gets its own bank index above the factory three, so a
    # palette can carry as many distinct FM banks as flash allows.
    slot_bank_overrides: tuple[tuple[int, bytes], ...] = ()
    # Catalog ids of engines built with the stereo (OUT/AUX L/R) render path.
    # None means "not specified" — a pre-per-engine (schema <= 9) recipe, which
    # the builder treats as all stereo-capable engines when the aux option is
    # stereo. A tuple (schema 10+) lists exactly the enabled engines.
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


def _packed_bank_bytes(bank: Any) -> bytes:
    """Validate one custom bank's voices into packed bytes, checked exactly
    (128 bytes/patch, every byte a 7-bit value) since they are baked verbatim
    into the firmware. Metadata never reaches the ARM build. A bank may hold
    1..32 patches; a bank with fewer than 32 is a "short" bank and the recipe
    must be schemaVersion 13 (enforced by the caller). The returned length is
    len(voices) * 128, so the firmware's Harmonics quantizer sizes to the real
    patch count and the baked array is only as large as the bank needs."""
    if not isinstance(bank, dict):
        raise ValueError("recipe contains an invalid custom bank")
    voices = bank.get("voices")
    if not isinstance(voices, list) or not 1 <= len(voices) <= PATCHES_PER_BANK:
        raise ValueError("a custom bank must contain between 1 and 32 voices")
    packed = bytearray()
    for voice in voices:
        if not isinstance(voice, dict):
            raise ValueError("a custom-bank voice is invalid")
        data = voice.get("packed")
        if not isinstance(data, list) or len(data) != PACKED_PATCH_SIZE \
                or any(type(byte) is not int or byte < 0 or byte > 127 for byte in data):
            raise ValueError("a custom-bank voice must have 128 packed 7-bit bytes")
        packed.extend(data)
    return bytes(packed)


def validate_user_data_banks(value: Any) -> list[tuple[int, bytes]]:
    """Validate a v6 recipe's custom FM banks into (index, 4096 packed bytes),
    index-keyed onto the three built-in banks (0-2)."""
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
        result.append((index, _packed_bank_bytes(entry["bank"])))
    return result


def validate_user_data_banks_v12(value: Any, num_slots: int) -> list[tuple[int, bytes]]:
    """Validate a v12 recipe's per-SLOT custom FM banks into (slot, 4096 packed
    bytes). A bank is keyed by the palette slot it belongs to; each customized FM
    slot gets its own bank, so the only bound is one bank per slot (the flash
    budget, enforced by the ARM build, is the real limit)."""
    if not isinstance(value, list) or len(value) > num_slots:
        raise ValueError("recipe contains an unsupported set of custom banks")
    result: list[tuple[int, bytes]] = []
    seen: set[int] = set()
    for entry in value:
        if not isinstance(entry, dict) or set(entry) != {"slot", "bank"}:
            raise ValueError("recipe contains an invalid custom-bank assignment")
        slot = entry["slot"]
        if type(slot) is not int or not 0 <= slot < num_slots or slot in seen:
            raise ValueError("a custom bank must target a distinct palette slot")
        seen.add(slot)
        result.append((slot, _packed_bank_bytes(entry["bank"])))
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
    # Worker had accepted, e.g. a stereo palette with a model deleted. Same
    # reasoning admits every later schema too.)
    normalized: list[str | None] = []
    for reference in slots:
        if reference is None:
            if schema_version < SPARSE_SLOT_MIN_SCHEMA_VERSION:
                raise ValueError(
                    f"empty slots require schemaVersion {SPARSE_SLOT_MIN_SCHEMA_VERSION} or newer")
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
    if has_sparse_bank(public_slots) and schema_version < SPARSE_BANK_MIN_SCHEMA_VERSION:
        raise ValueError("a bank's engines must be contiguous (empty slots only "
                         f"at the end) unless the recipe uses schemaVersion "
                         f"{SPARSE_BANK_MIN_SCHEMA_VERSION}")


def validate_recipe(value: Any) -> BuildRecipe:
    if not isinstance(value, dict):
        raise ValueError("recipe must be a JSON object")
    schema_version = value.get("schemaVersion")
    if (type(schema_version) is not int
            or not MIN_RECIPE_SCHEMA_VERSION <= schema_version <= MAX_RECIPE_SCHEMA_VERSION):
        raise ValueError(
            f"recipe schemaVersion must be {MIN_RECIPE_SCHEMA_VERSION} through "
            f"{MAX_RECIPE_SCHEMA_VERSION}")
    target = value.get("target")
    if target not in ("mutable-instruments-plaits", "plum-audio-roved"):
        raise ValueError("unsupported firmware target")
    if target == "plum-audio-roved" and schema_version < ROVED_MIN_SCHEMA_VERSION:
        raise ValueError(
            f"Ro'Ved builds require schemaVersion {ROVED_MIN_SCHEMA_VERSION}")
    if value.get("firmware") != "rubato-plaits":
        raise ValueError("unsupported firmware family")
    if value.get("output") != "audio-wav":
        raise ValueError("unsupported output format")
    slots = value.get("slots")
    if not isinstance(slots, list) or len(slots) not in (24, 32):
        raise ValueError("recipe must contain 24 slots, or 32 for a four-bank build")
    if len(slots) == 32 and schema_version < FOUR_BANK_MIN_SCHEMA_VERSION:
        raise ValueError(
            f"32-slot recipes require schemaVersion {FOUR_BANK_MIN_SCHEMA_VERSION} or newer")
    public_slots = normalize_slots(slots, schema_version)
    validate_bank_shape(public_slots, schema_version)
    user_data_banks: list[tuple[int, bytes]] = []   # v6 index-keyed
    slot_banks: list[tuple[int, bytes]] = []        # v12 slot-keyed
    if schema_version >= RESOURCES_MIN_SCHEMA_VERSION:
        resources = value.get("resources")
        # v6 always carries the custom-FM-banks resource (its defining feature), and
        # v12 always carries per-slot banks (its defining feature — 24 or 32 slots).
        # v7-v11 mirror the editor: userDataBanks only for a 32-slot (fourth-bank)
        # recipe; a 24-slot v7-v11 carries chord tables only, like v5.
        expect_user_data_banks = schema_version in (
            FOUR_BANK_MIN_SCHEMA_VERSION,
            SLOT_BANK_MIN_SCHEMA_VERSION,
            SHORT_BANK_MIN_SCHEMA_VERSION,
        ) or (
            SPARSE_SLOT_MIN_SCHEMA_VERSION <= schema_version < SLOT_BANK_MIN_SCHEMA_VERSION
            and len(slots) == 32
        )
        # v14+ compile-time features say nothing about resources: calibration,
        # the Ro'Ved panel, and the color-blind display can each compose with any
        # palette, with or without custom FM banks. Mirrors the Worker contract.
        carries_user_data_banks = expect_user_data_banks or (
            schema_version >= CALIBRATION_MIN_SCHEMA_VERSION and isinstance(resources, dict)
            and set(resources) == {"chordTables", "userDataBanks"})
        expected_resource_keys = {"chordTables", "userDataBanks"} if carries_user_data_banks else {"chordTables"}
        if not isinstance(resources, dict) or set(resources) != expected_resource_keys:
            raise ValueError("recipe must contain only supported firmware resources")
        chord_tables = validate_chord_tables(resources.get("chordTables"))
        if carries_user_data_banks:
            if schema_version >= SLOT_BANK_MIN_SCHEMA_VERSION:
                slot_banks = validate_user_data_banks_v12(resources.get("userDataBanks"), len(slots))
            else:
                user_data_banks = validate_user_data_banks(resources.get("userDataBanks"))
        # A bank with fewer than 32 patches (a "short" bank) needs the firmware's
        # variable-length quantizer, advertised as schemaVersion 13. Older builders
        # would bake it but keep the fixed 32-step dial, so gate it here.
        short_banks = [
            length for _, length in
            ((slot, len(data)) for slot, data in (user_data_banks + slot_banks))
            if length < PACKED_BANK_SIZE
        ]
        if short_banks and schema_version < SHORT_BANK_MIN_SCHEMA_VERSION:
            raise ValueError(
                f"short (fewer than 32-patch) FM banks require schemaVersion "
                f"{SHORT_BANK_MIN_SCHEMA_VERSION}")
    else:
        chord_tables = validate_chord_tables(DEFAULT_CHORD_TABLES)
    configuration = value if schema_version >= CONFIGURATION_MIN_SCHEMA_VERSION else DEFAULT_CONFIGURATION
    preferences = configuration.get("preferences")
    options = configuration.get("initialOptions")
    if not isinstance(preferences, dict) or not isinstance(options, dict):
        raise ValueError("recipe must contain firmware preferences and starting options")
    if set(preferences) not in (
        {"navigationMode"},
        {"navigationMode", "calibration"},
        {"navigationMode", "calibration", "colorBlindMode"},
    ) or set(options) != {
        "lockedFrequencyKnob", "modelInput", "levelInput", "auxOutput",
        "suboscillatorOctave", "chordTable", "holdOnTrigger",
    }:
        raise ValueError("recipe contains an unsupported firmware option")

    mappings = {
        "navigation_mode": (preferences.get("navigationMode"), {"linear": 0, "banked": 1}),
        # Value order is the firmware's (plaits/dsp/voice.h), not a display
        # order: it decides which LED color each setting shows and is baked into
        # the DSP's comparisons. Recipes bind by NAME, so reordering here is
        # invisible to the recipe format - but see OPTIONS_LAYOUT_VERSION.
        "locked_frequency_pot_option": (options.get("lockedFrequencyKnob"), {"octaves": 0, "macro-4": 1, "aux-crossfade": 2, "decay": 3}),
        "model_cv_option": (options.get("modelInput"), {"model": 0, "macro-4": 1, "aux-crossfade": 2, "lpg-colour": 3}),
        "level_cv_option": (options.get("levelInput"), {"level": 0, "decay": 1}),
        # The recipe records the suboscillator's SHAPE inside auxOutput and its
        # octave beside it; the firmware splits the pair the other way, into one
        # aux-output setting (regular / stereo / subosc) and one suboscillator
        # setting that carries shape and octave together. The two forms are a
        # bijection, so the recipe format is unchanged and every saved recipe
        # still builds - the shape just moves across, below.
        "aux_output_option": (options.get("auxOutput"), {"alternate-model": 0, "stereo": 1, "square-subosc": 2, "sine-subosc": 2}),
        "aux_subosc_option": (options.get("suboscillatorOctave"), {0: 0, -1: 1, -2: 2}),
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
    # Shape and octave share one firmware value (plaits/dsp/voice.h): 0-2 square
    # at 0/-1/-2 octaves, 3-5 sine at the same three. The octave landed above;
    # the shape comes from auxOutput, the only place the recipe records it. It
    # is carried even when the aux output is not a suboscillator, so the recipe
    # keeps its remembered shape and the profile-id fold below stays injective
    # over every (auxOutput, suboscillatorOctave) pair.
    if options.get("auxOutput") == "sine-subosc":
        normalized_options["aux_subosc_option"] += 3

    # A module keeps its saved options across an audio reflash, and the firmware
    # only overwrites them (ApplyBuildOptionDefaults) when the recipe's profile
    # id differs from the stored one. So whenever the MEANING of a stored value
    # changes — an option's values are reordered, or one is inserted — seeding
    # the fold with a new layout version makes every id disjoint from every id
    # minted under the old numbering, forcing that reset exactly once. Without
    # it an unchanged recipe keeps its id and the module silently re-reads old
    # numbers under new meanings.
    profile_code = OPTIONS_LAYOUT_VERSION
    profile_code = profile_code * 4 + normalized_options["locked_frequency_pot_option"]
    for name, radix in (
        ("model_cv_option", 4),
        ("level_cv_option", 2),
        # Regular aux model, stereo OUT/AUX, suboscillator.
        ("aux_output_option", 3),
        # Square/sine crossed with 0, -1 and -2 octaves.
        ("aux_subosc_option", 6),
        # Nine, matching validate_chord_tables' cap. A smaller radix here would
        # let a table at index 6-8 alias into the next digit, so two different
        # recipes could mint one profile id and the second would not apply its
        # starting options.
        ("chord_set_option", 9),
        ("hold_on_trigger_option", 2),
    ):
        profile_code = profile_code * radix + normalized_options[name]
    # The reversible encoding has to stay under the reserved range (profile code
    # < 254*256 = 65024) to fit the legacy navigation and padding bytes, while
    # reserving low bytes 0 and 1 so saved states from the old navigation setting
    # can never look initialized. The option digits span 10368 values per layout
    # version, so the ceiling arrives at layout version 6 — the assertion is
    # here so that lands as a build error rather than as colliding profile ids.
    if profile_code >= 254 * 256:
        raise ValueError("options profile code has outgrown the reserved range")
    profile_id = ((profile_code // 254) << 8) | (2 + profile_code % 254)

    # Per-engine stereo (introduced in schema 10): stereoEngines lists the
    # catalog ids built with the stereo render path. Absent on schema <= 9, which
    # the container treats as "all stereo-capable engines" (back-compat with the
    # global gate).
    stereo_engines: tuple[str, ...] | None = None
    if "stereoEngines" in value:
        # v10's defining feature. Every later supported schema is a superset and
        # may also carry the list when its aux output is stereo. Gate on the
        # minimum so adding a new schema cannot strand the container behind the
        # Worker's validator again.
        if schema_version < STEREO_ENGINES_MIN_SCHEMA_VERSION:
            raise ValueError(
                f"stereoEngines requires schemaVersion "
                f"{STEREO_ENGINES_MIN_SCHEMA_VERSION} or newer")
        raw = value.get("stereoEngines")
        if not isinstance(raw, list) or not all(
            isinstance(engine_id, str) and engine_id in PUBLIC_ENGINES for engine_id in raw
        ):
            raise ValueError("stereoEngines must list approved engine ids")
        stereo_engines = tuple(dict.fromkeys(raw))
    elif schema_version == STEREO_ENGINES_MIN_SCHEMA_VERSION:
        raise ValueError("schemaVersion 10 recipes must carry a stereoEngines list")

    # The calibration procedure (v14). Deliberately NOT part of the profile-id
    # fold above: that fold exists to reset a module's SAVED options when a
    # stored value changes meaning, and calibration is not a stored option — it
    # is compiled in or it is not. Folding it in would reset every user's options
    # the first time they toggled it.
    enable_calibration = bool(preferences.get("calibration", False))
    if not isinstance(preferences.get("calibration", False), bool):
        raise ValueError("recipe contains an unsupported firmware option")
    if enable_calibration and schema_version < CALIBRATION_MIN_SCHEMA_VERSION:
        raise ValueError(
            f"the calibration procedure requires schemaVersion "
            f"{CALIBRATION_MIN_SCHEMA_VERSION}")

    # The accessible bank display (v15). Also compile-time-only, so changing it
    # must not reset the module's saved starting options.
    color_blind_mode = bool(preferences.get("colorBlindMode", False))
    if not isinstance(preferences.get("colorBlindMode", False), bool):
        raise ValueError("recipe contains an unsupported firmware option")
    if color_blind_mode and schema_version < COLOR_BLIND_MODE_MIN_SCHEMA_VERSION:
        raise ValueError(
            f"color-blind bank display requires schemaVersion "
            f"{COLOR_BLIND_MODE_MIN_SCHEMA_VERSION}")

    return BuildRecipe(
        public_slots=public_slots,
        chord_tables=chord_tables,
        options_profile_id=profile_id,
        enable_calibration=1 if enable_calibration else 0,
        roved_panel=1 if target == "plum-audio-roved" else 0,
        color_blind_mode=1 if color_blind_mode else 0,
        user_data_bank_overrides=tuple(user_data_banks),
        slot_bank_overrides=tuple(slot_banks),
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
    # The PUBLIC slot index of each selected engine (= its public bank * 8 + its
    # physical row). v12 per-slot custom banks are keyed by this, so it maps a
    # recipe's slot-keyed banks through the internal reordering to `selected`.
    public_slot_of_selected = [
        internal_order[ib] * 8 + row
        for ib, bank in enumerate(internal_banks)
        for _, row in bank
    ]
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

    # Assign every placed FM-bank engine a bank index for kEngineUserDataBank:
    #   * an un-customized preset slot keeps its factory index (0/1/2), so the
    #     firmware falls through to fm_patches_table[index];
    #   * a v12 per-slot custom bank gets a FRESH index (>= 3) with its own baked
    #     4096-byte array — this is what lets a palette hold N distinct banks.
    # v6 index-keyed overrides (a whole factory bank replaced) still resolve at
    # 0/1/2. A runtime TIMBRE-loaded user bank still takes precedence in voice.cc.
    factory_override = dict(recipe.user_data_bank_overrides)        # v6: index -> bytes
    slot_override = dict(recipe.slot_bank_overrides)                # v12: public slot -> bytes
    engine_bank_index: list[int] = []                              # per selected engine
    custom_arrays: list[tuple[int, bytes]] = []                    # (index >= 3, bytes)
    next_custom_index = MAX_USER_DATA_BANKS
    for engine, public_slot in zip(selected, public_slot_of_selected):
        if engine.user_data_bank < 0:
            engine_bank_index.append(-1)
        elif public_slot in slot_override:
            custom_arrays.append((next_custom_index, slot_override[public_slot]))
            engine_bank_index.append(next_custom_index)
            next_custom_index += 1
        else:
            engine_bank_index.append(engine.user_data_bank)
    user_data_banks = ", ".join(str(index) for index in engine_bank_index)
    # v6 factory overrides only for factory banks a placed engine actually uses
    # (an orphaned assignment, its engine removed, never bloats the firmware).
    used_factory = {index for index in engine_bank_index if 0 <= index < MAX_USER_DATA_BANKS}
    factory_arrays = [(index, factory_override[index]) for index in sorted(used_factory)
                      if index in factory_override]
    override_arrays_all = factory_arrays + custom_arrays           # unique indices
    override_by_index = {index for index, _ in override_arrays_all}
    # The pointer table must span every bank index the engine table can reference:
    # indices 0..2 (factory, possibly overridden) plus every allocated custom one.
    table_size = next_custom_index
    has_user_data_bank = any(item.user_data_bank >= 0 for item in selected)
    # Each baked override array is only as long as its bank needs (n patches * 128
    # bytes); a short bank costs proportionally less flash. voice.cc reads the
    # matching length from kResolvedUserDataBankSize (below) so the engine sizes
    # its Harmonics quantizer to that count.
    size_by_index = {index: len(data) for index, data in override_arrays_all}
    override_arrays = "\n".join(
        "static const uint8_t kUserDataBankOverride_{index}[{size}] = {{ {body} }};".format(
            index=index, size=len(data), body=", ".join(str(byte) for byte in data)
        )
        for index, data in override_arrays_all
    )
    # Resolve every reachable bank index to its actual data pointer AT BUILD TIME,
    # so the firmware never NAMES a factory blob it can't reach. Per index:
    #   * overridden (a v6 whole-bank override, or a v12 per-slot custom bank) ->
    #     its baked kUserDataBankOverride_i array;
    #   * a live stock factory bank (a placed, un-customized slot maps to it) ->
    #     syx_bank_i;
    #   * absent (customized away, or a factory bank no slot placed) -> NULL.
    # Because syx_bank_i is now referenced ONLY here and ONLY for live banks, and
    # voice.cc no longer names fm_patches_table in a generated build, --gc-sections
    # drops the ~4 KB blob of every factory bank that isn't a plain stock slot
    # (fm_patches_table itself goes too). Reverting a slot to stock re-references
    # syx_bank_i on the next build, restoring it.
    def _resolved_pointer(i: int) -> str:
        if i in override_by_index:
            return f"kUserDataBankOverride_{i}"
        if 0 <= i < MAX_USER_DATA_BANKS and i in used_factory:
            return f"syx_bank_{i}"
        return "NULL"

    # Byte length behind each resolved pointer: a custom override is only as long
    # as its patch count (a short bank < 4096); a live stock factory bank — and an
    # unreferenced NULL slot, whose length is never read — is a full 32-patch bank.
    # voice.cc reads this to size the six-op Harmonics quantizer per resident bank.
    def _resolved_size(i: int) -> int:
        return size_by_index.get(i, PACKED_BANK_SIZE)

    resolved_table = "static const uint8_t* const kResolvedUserDataBank[{count}] = {{ {pointers} }};".format(
        count=table_size,
        pointers=", ".join(_resolved_pointer(i) for i in range(table_size)),
    ) if has_user_data_bank else ""
    resolved_size_table = "static const size_t kResolvedUserDataBankSize[{count}] = {{ {sizes} }};".format(
        count=table_size,
        sizes=", ".join(str(_resolved_size(i)) for i in range(table_size)),
    ) if has_user_data_bank else ""
    user_data_bank_override_block = (
        f"\n#if PLAITS_HAS_USER_DATA_BANK\n{override_arrays}\n{resolved_table}\n"
        f"{resolved_size_table}\n#endif\n"
        if has_user_data_bank else ""
    )

    return f"""// Generated by alt_firmwares/plaits_lab_builder/generate_engine_config.py.
// Public recipe order: green, red, amber. Registry order: amber, green, red.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

{includes}
#include "plaits/resources.h"

#define PLAITS_ENGINE_COUNT {len(selected)}
#define PLAITS_BANK_SIZES {{ {", ".join(str(size) for size in bank_sizes)} }}
#define PLAITS_ENGINE_ROWS {{ {", ".join(str(row) for row in engine_rows)} }}
#define PLAITS_HAS_SPEECH_ENGINE {1 if any(item.behavior == 'speech' for item in selected) else 0}
#define PLAITS_HAS_CHIPTUNE_ENGINE {1 if any(item.behavior == 'chiptune' for item in selected) else 0}
#define PLAITS_HAS_USER_DATA_BANK {1 if has_user_data_bank else 0}
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE {1 if override_arrays_all else 0}
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK {1 if has_user_data_bank else 0}

#define PLAITS_CHORD_TABLE_COUNT {len(recipe.chord_tables)}
#define PLAITS_CHORD_COUNT {chord_offset}
#define PLAITS_CHORD_TABLE_OFFSETS {{ {", ".join(str(value) for value in chord_offsets)} }}
#define PLAITS_CHORD_TABLE_SIZES {{ {", ".join(str(value) for value in chord_sizes)} }}
#define PLAITS_CHORD_CENTS {{ {", ".join(chord_cents)} }}
#define PLAITS_CHORD_ARP_LENGTHS {{ {", ".join(chord_arp_lengths)} }}

#define PLAITS_BUILD_NAVIGATION_MODE {recipe.navigation_mode}
#define PLAITS_BUILD_COLOR_BLIND_MODE {recipe.color_blind_mode}
#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION {recipe.locked_frequency_pot_option}
#define PLAITS_BUILD_MODEL_CV_OPTION {recipe.model_cv_option}
#define PLAITS_BUILD_LEVEL_CV_OPTION {recipe.level_cv_option}
#define PLAITS_BUILD_AUX_OUTPUT_OPTION {recipe.aux_output_option}
#define PLAITS_BUILD_AUX_SUBOSC_OPTION {recipe.aux_subosc_option}
#define PLAITS_BUILD_CHORD_SET_OPTION {recipe.chord_set_option}
#define PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION {recipe.hold_on_trigger_option}
#define PLAITS_BUILD_ENABLE_CALIBRATION {recipe.enable_calibration}
#define PLAITS_ROVED_PANEL {recipe.roved_panel}
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
