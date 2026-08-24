#!/usr/bin/env python3
"""Generate the compile-time Plaits engine registry for an approved recipe.

The input recipe is always expressed in the public green/red/amber order.  A
three-bank registry keeps Plaits' legacy amber/green/red rotation.  A four-bank
registry rotates orange/green/red/amber so stepping forward from green still
follows the public green/red/amber/orange order.  Only identifiers in this
file's catalog can influence C++ output.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from speech_banks import validate_speech_banks


REPO_ROOT = Path(__file__).resolve().parents[2]
RESOURCES_PATH = REPO_ROOT / "plaits/resources.cc"
CATALOG_PATH = Path(__file__).resolve().parents[1] / "plaits_lab_catalog/catalog.json"
PUBLIC_CATALOG_PATH = Path(__file__).resolve().parents[1] / "plaits_lab_catalog/public_catalog.json"
CHORD_CATALOG_PATH = Path(__file__).resolve().parents[1] / "plaits_lab_chord_tables/catalog.json"
RANDOMIZER_PROFILES_PATH = Path(__file__).resolve().parents[1] / "plaits_lab_sdk/randomizer_profiles.json"


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
# STM32F37x flash page (stmlib/system/flash_programming.h). A swappable bank is
# erased a page at a time, so PACKED_BANK_SIZE must be a whole number of pages
# and each bank must start on one — see the alignment note in render_config.
FLASH_PAGE_SIZE = 2048
assert PACKED_BANK_SIZE % FLASH_PAGE_SIZE == 0

# Keep this ceiling in lockstep with src/contract.ts. Feature gates use minimum
# versions so adding a schema at the ceiling does not require extending a trail
# of "10, 11, 12..." whitelists in the build container.
MIN_RECIPE_SCHEMA_VERSION = 2
MAX_RECIPE_SCHEMA_VERSION = 24
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
SCALE_BANK_MIN_SCHEMA_VERSION = 16
LEVEL_AUTO_MIN_SCHEMA_VERSION = 16
SPEECH_BANKS_MIN_SCHEMA_VERSION = 17
ATTENUVERTER_MODE_MIN_SCHEMA_VERSION = 18
ONE_KNOB_ENVELOPE_MIN_SCHEMA_VERSION = 19
SWAPPABLE_FM_BANKS_MIN_SCHEMA_VERSION = 20
SYNC_INPUT_MIN_SCHEMA_VERSION = 21
# v22 moves Sync In's COMPILE-TIME switch off the starting value and onto its own
# preference. Starting Options are the module's initial RUNTIME values and the
# user can change them on the hardware afterwards, so deriving compiled
# capability from one meant Sync In only ever existed on a module whose owner
# happened to pick it at build time — change your mind later and it is not in
# the menu (ui.cc sizes that menu as 4 + PLAITS_BUILD_ENABLE_SYNC_INPUT).
SYNC_INPUT_PREFERENCE_MIN_SCHEMA_VERSION = 22
# v24: reduce the FREQUENCY range selector to octave switching, fine tuning and
# coarse. Compile-time only, and it changes no stored state -- only the mapping
# in PitchRangeFromControl -- so a module moves between the two layouts without
# losing its tuned root or locked octave.
SIMPLIFIED_PITCH_RANGES_MIN_SCHEMA_VERSION = 24
# v23 adds two independent experimental FM preferences. Linear TZFM changes
# the attenuverter law on supporting engines; Fast FM changes SDADC2 acquisition
# and therefore makes LEVEL CV unavailable. Keeping them separate allows all
# four combinations and makes the hardware tradeoff explicit.
EXPERIMENTAL_FM_MIN_SCHEMA_VERSION = 23

# These two stock physical-model engines can treat the alternate contour as
# energy entering a resonator rather than as a VCA after it.  Keep this list in
# the trusted generator rather than accepting an arbitrary recipe flag: a
# public recipe chooses engines, but cannot change firmware routing semantics.
RESONATOR_ENVELOPE_ENGINE_IDS = frozenset({
    "inharmonic-string",
    "modal-resonator",
})

MIN_SCALE_BANK_SIZE = 1
MAX_SCALE_BANK_SIZE = 16
MIN_SCALE_DEGREES = 2
MAX_SCALE_DEGREES = 7
SCALE_UNITS_PER_SEMITONE = 128
SCALE_UNITS_PER_OCTAVE = 12 * SCALE_UNITS_PER_SEMITONE

# Bumped whenever a stored option value changes meaning in the firmware (see the
# value tables in plaits/dsp/voice.h). It seeds the options profile-id fold, so a
# bump moves every build to an id no old firmware ever minted — which is what
# makes a module reset its saved options to the recipe's exactly once instead of
# reinterpreting the old numbers. Version 1: options menu reordered, and the
# fourth macro / stereo promoted to value 1 on their lights (2026-07). Version 2:
# the aux output holds one suboscillator value instead of two (square/sine), and
# the suboscillator light carries shape and octave together (2026-07). Version 3:
# the LEVEL light adds Auto as value 2, expanding that digit's radix (2026-07).
# Version 4 adds LIGHT 8's attenuverter mode to recipe starting options and
# moves the collision-free profile identity into three persisted bytes.
# Version 5 appends the triggered and gated contours to LIGHT 4 and expands its
# profile digit from four values to six. Version 6 appends Sync In to LIGHT 5
# and expands that profile digit from four values to five.
OPTIONS_LAYOUT_VERSION = 6


@dataclass(frozen=True)
class BuildRecipe:
    public_slots: list[str]
    chord_tables: list[dict[str, Any]]
    scale_bank: list[dict[str, Any]]
    navigation_mode: int
    locked_frequency_pot_option: int
    model_cv_option: int
    level_cv_option: int
    aux_output_option: int
    aux_subosc_option: int
    chord_set_option: int
    hold_on_trigger_option: int
    attenuverter_mode: int
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
    # v17: selected stock LPC banks followed by custom decoded-frame banks.
    speech_banks: dict[str, Any] | None = None
    # v22: 1 compiles Sync In in, making it selectable as a MODEL-input mode at
    # RUNTIME. Before v22 this was derived from the starting value, which meant a
    # user who did not choose Sync In at build time could never reach it.
    sync_input: int = 0
    # v24: 1 keeps only the three most clockwise FREQUENCY ranges. Like
    # calibration and the panel choice, it is a firmware shape rather than a
    # stored setting, so it stays out of the options profile-id fold.
    simplified_pitch_ranges: int = 0
    # v23: independent experimental FM capabilities. Neither is a saved runtime
    # option, so neither belongs in the options profile-id fold.
    linear_tzfm: int = 0
    fast_fm: int = 0
    # v20: 1 makes every FM bank replaceable over TIMBRE — each bank's baked
    # array becomes the flash region a transfer erases and reprograms. 0 (the
    # DEFAULT) is the historical layout, byte-for-byte: banks at their natural
    # length, unaligned, and the module's single legacy user-data region.
    #
    # Opt-IN rather than opt-out because the cost, though small, is not free:
    # page-aligning the banks costs ~832 bytes on a full palette, and the stock
    # 24-model preset has under 800 bytes of headroom. Defaulting it on would
    # have pushed the DEFAULT build over the flash limit.
    swappable_fm_banks: int = 0


_FACTORY_BANK_RE = re.compile(
    r"const uint8_t syx_bank_(\d+)\[\] = \{(.*?)\};", re.DOTALL)
_factory_bank_cache: dict[int, bytes] | None = None


def load_factory_bank_bytes() -> dict[int, bytes]:
    """The three built-in DX7 banks, read out of the generated resources.cc.

    A swappable build re-emits these rather than linking resources.cc's arrays
    in place, so it can clear the tag bytes (see render_config). resources.cc is
    itself generated ("make resources") and its shape is stable; every bank is
    asserted to be exactly one region so a format drift fails the build loudly
    instead of silently producing a mis-sized region.
    """
    global _factory_bank_cache
    if _factory_bank_cache is None:
        source = RESOURCES_PATH.read_text(encoding="utf-8")
        banks: dict[int, bytes] = {}
        for match in _FACTORY_BANK_RE.finditer(source):
            values = [int(token) for token in match.group(2).split(",") if token.strip()]
            if len(values) != PACKED_BANK_SIZE:
                raise ValueError(
                    f"syx_bank_{match.group(1)} is {len(values)} bytes, "
                    f"expected {PACKED_BANK_SIZE}")
            banks[int(match.group(1))] = bytes(values)
        if len(banks) != MAX_USER_DATA_BANKS:
            raise ValueError(
                f"expected {MAX_USER_DATA_BANKS} factory banks in "
                f"{RESOURCES_PATH.name}, found {len(banks)}")
        _factory_bank_cache = banks
    return _factory_bank_cache


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
RANDOMIZER_REGISTRY = json.loads(RANDOMIZER_PROFILES_PATH.read_text(encoding="utf-8"))
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
DEFAULT_SCALE_BANK = [
    {"id": "major", "name": "Major", "description": "The familiar seven-note major scale.",
     "pitches": [0, 256, 512, 640, 896, 1152, 1408], "tuning": "12-TET", "source": "Shipped"},
    {"id": "natural-minor", "name": "Natural minor",
     "description": "The familiar seven-note natural minor scale.",
     "pitches": [0, 256, 384, 640, 896, 1024, 1280], "tuning": "12-TET", "source": "Shipped"},
    {"id": "dorian", "name": "Dorian", "description": "A minor mode with a raised sixth.",
     "pitches": [0, 256, 384, 640, 896, 1152, 1280], "tuning": "12-TET", "source": "Shipped"},
    {"id": "mixolydian", "name": "Mixolydian",
     "description": "A major mode with a lowered seventh.",
     "pitches": [0, 256, 512, 640, 896, 1152, 1280], "tuning": "12-TET", "source": "Shipped"},
    {"id": "harmonic-minor", "name": "Harmonic minor",
     "description": "Natural minor with a raised seventh.",
     "pitches": [0, 256, 384, 640, 896, 1024, 1408], "tuning": "12-TET", "source": "Shipped"},
    {"id": "melodic-minor", "name": "Jazz melodic minor",
     "description": "The ascending melodic-minor collection, used unchanged in jazz.",
     "pitches": [0, 256, 384, 640, 896, 1152, 1408], "tuning": "12-TET", "source": "Shipped"},
    {"id": "major-pentatonic", "name": "Major pentatonic",
     "description": "An open five-note major scale.",
     "pitches": [0, 256, 512, 896, 1152], "tuning": "12-TET", "source": "Shipped"},
    {"id": "whole-tone", "name": "Whole tone",
     "description": "Six evenly spaced whole tones.",
     "pitches": [0, 256, 512, 768, 1024, 1280], "tuning": "12-TET", "source": "Shipped"},
]


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


def validate_scale_bank(value: Any) -> list[dict[str, Any]]:
    if not isinstance(value, list) or not MIN_SCALE_BANK_SIZE <= len(value) <= MAX_SCALE_BANK_SIZE:
        raise ValueError(
            f"recipe must contain between {MIN_SCALE_BANK_SIZE} and "
            f"{MAX_SCALE_BANK_SIZE} scales")
    result: list[dict[str, Any]] = []
    scale_ids: set[str] = set()
    for scale in value:
        if not isinstance(scale, dict) or set(scale) != {
            "id", "name", "description", "pitches", "tuning", "source",
        }:
            raise ValueError("recipe contains invalid scale metadata")
        scale_id = scale.get("id")
        if (not isinstance(scale_id, str) or not scale_id or len(scale_id) > 80
                or scale_id in scale_ids
                or any(not (character.islower() or character.isdigit() or character == "-")
                       for character in scale_id)):
            raise ValueError("recipe contains an invalid or duplicate scale ID")
        if (not isinstance(scale.get("name"), str) or not scale["name"]
                or len(scale["name"]) > 80
                or not isinstance(scale.get("description"), str) or not scale["description"]
                or len(scale["description"]) > 240
                or scale.get("tuning") not in ("12-TET", "Microtonal")
                or scale.get("source") not in ("Shipped", "Braids", "Rubato", "Local")):
            raise ValueError("recipe contains invalid scale metadata")
        pitches = scale.get("pitches")
        if (not isinstance(pitches, list)
                or not MIN_SCALE_DEGREES <= len(pitches) <= MAX_SCALE_DEGREES
                or any(type(pitch) is not int for pitch in pitches)
                or pitches[0] != 0
                or any(right <= left for left, right in zip(pitches, pitches[1:]))
                or pitches[-1] >= SCALE_UNITS_PER_OCTAVE):
            raise ValueError(
                f"a scale must contain {MIN_SCALE_DEGREES} to {MAX_SCALE_DEGREES} "
                "strictly ascending pitches below the octave")
        expected_tuning = (
            "12-TET"
            if all(pitch % SCALE_UNITS_PER_SEMITONE == 0 for pitch in pitches)
            else "Microtonal"
        )
        if scale["tuning"] != expected_tuning:
            raise ValueError("a scale's tuning label does not match its pitches")
        scale_ids.add(scale_id)
        result.append(scale)
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
    if value.get("output") not in ("audio-wav", "intel-hex"):
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
    speech_banks: dict[str, Any] | None = None
    scale_bank = validate_scale_bank(DEFAULT_SCALE_BANK)
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
        # Schema 16 can be reached either by a custom scale bank or by another
        # v16 feature such as automatic LEVEL routing. A missing scaleBank means
        # "compile the shipped default bank"; it is not an invalid v16 recipe.
        carries_scale_bank = (
            schema_version >= SCALE_BANK_MIN_SCHEMA_VERSION
            and isinstance(resources, dict)
            and "scaleBank" in resources
        )
        carries_speech_banks = (
            schema_version >= SPEECH_BANKS_MIN_SCHEMA_VERSION
            and isinstance(resources, dict)
            and "speechBanks" in resources
        )
        base_resource_keys = {"chordTables"}
        if carries_scale_bank:
            base_resource_keys.add("scaleBank")
        if carries_speech_banks:
            base_resource_keys.add("speechBanks")
        carries_user_data_banks = expect_user_data_banks or (
            schema_version >= CALIBRATION_MIN_SCHEMA_VERSION
            and isinstance(resources, dict)
            and set(resources) == base_resource_keys | {"userDataBanks"})
        expected_resource_keys = (
            base_resource_keys | {"userDataBanks"}
            if carries_user_data_banks else base_resource_keys
        )
        if not isinstance(resources, dict) or set(resources) != expected_resource_keys:
            raise ValueError("recipe must contain only supported firmware resources")
        chord_tables = validate_chord_tables(resources.get("chordTables"))
        if carries_scale_bank:
            scale_bank = validate_scale_bank(resources.get("scaleBank"))
        if carries_speech_banks:
            if not any(
                engine_id in public_slots for engine_id in ("speech", "lpc-speech")
            ):
                raise ValueError(
                    "speechBanks requires Speech or LPC Words in the palette")
            speech_banks = validate_speech_banks(resources.get("speechBanks"))
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
    legacy_option_keys = {
        "lockedFrequencyKnob", "modelInput", "levelInput", "auxOutput",
        "suboscillatorOctave", "chordTable", "holdOnTrigger",
    }
    current_option_keys = legacy_option_keys | {"attenuverterMode"}
    carries_attenuverter_mode = set(options) == current_option_keys
    if set(preferences) not in (
        {"navigationMode"},
        {"navigationMode", "calibration"},
        {"navigationMode", "calibration", "colorBlindMode"},
        {"navigationMode", "calibration", "colorBlindMode", "replaceableFmBanks"},
        {"navigationMode", "calibration", "colorBlindMode", "replaceableFmBanks",
         "syncInput"},
        {"navigationMode", "calibration", "colorBlindMode", "replaceableFmBanks",
         "syncInput", "linearTzfm", "fastFm"},
        {"navigationMode", "calibration", "colorBlindMode", "replaceableFmBanks",
         "syncInput", "linearTzfm", "fastFm", "simplifiedPitchRanges"},
    ) or (set(options) != legacy_option_keys and not carries_attenuverter_mode):
        raise ValueError("recipe contains an unsupported firmware option")
    # The Worker stores a fully normalized option profile and therefore adds
    # the legacy-equivalent `stock` value before handing a pre-v18 recipe to
    # this private container. Accept that no-op representation, but keep Drift
    # and Step gated to schema 18 just as they are at the public boundary.
    carries_new_attenuverter_mode = (
        carries_attenuverter_mode and options.get("attenuverterMode") != "stock"
    )
    if ((schema_version >= ATTENUVERTER_MODE_MIN_SCHEMA_VERSION
            and not carries_attenuverter_mode)
            or (schema_version < ATTENUVERTER_MODE_MIN_SCHEMA_VERSION
                and carries_new_attenuverter_mode)):
        raise ValueError(
            f"unpatched attenuverter starting mode requires schemaVersion "
            f"{ATTENUVERTER_MODE_MIN_SCHEMA_VERSION}")

    mappings = {
        "navigation_mode": (preferences.get("navigationMode"), {"linear": 0, "banked": 1}),
        # Value order is the firmware's (plaits/dsp/voice.h), not a display
        # order: it decides which LED color each setting shows and is baked into
        # the DSP's comparisons. Recipes bind by NAME, so reordering here is
        # invisible to the recipe format - but see OPTIONS_LAYOUT_VERSION.
        "locked_frequency_pot_option": (options.get("lockedFrequencyKnob"), {
            "octaves": 0,
            "macro-4": 1,
            "aux-crossfade": 2,
            "decay": 3,
            "triggered-envelope": 4,
            "gated-envelope": 5,
        }),
        "model_cv_option": (options.get("modelInput"), {
            "model": 0,
            "macro-4": 1,
            "aux-crossfade": 2,
            "lpg-colour": 3,
            "sync-in": 4,
        }),
        "level_cv_option": (
            options.get("levelInput"),
            {"level": 0, "decay": 1, "auto": 2},
        ),
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
        "attenuverter_mode": (
            options.get("attenuverterMode", "stock"),
            {"stock": 0, "drift": 1, "step": 2},
        ),
    }
    normalized_options: dict[str, int] = {}
    for name, (selected, allowed) in mappings.items():
        if selected not in allowed or (name == "hold_on_trigger_option" and not isinstance(selected, bool)):
            raise ValueError("recipe contains an unsupported firmware option")
        normalized_options[name] = allowed[selected]
    if options.get("levelInput") == "auto" and schema_version < LEVEL_AUTO_MIN_SCHEMA_VERSION:
        raise ValueError(
            f"automatic LEVEL routing requires schemaVersion "
            f"{LEVEL_AUTO_MIN_SCHEMA_VERSION} or newer")
    if (normalized_options["locked_frequency_pot_option"] >= 4
            and schema_version < ONE_KNOB_ENVELOPE_MIN_SCHEMA_VERSION):
        raise ValueError(
            f"one-knob envelopes require schemaVersion "
            f"{ONE_KNOB_ENVELOPE_MIN_SCHEMA_VERSION} or newer")
    if (normalized_options["model_cv_option"] == 4
            and schema_version < SYNC_INPUT_MIN_SCHEMA_VERSION):
        raise ValueError(
            f"Sync In requires schemaVersion "
            f"{SYNC_INPUT_MIN_SCHEMA_VERSION} or newer")
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
    # numbers under new meanings. Appending Auto to LEVEL changes this fold's
    # radix from two to three, so every profile id moves without renumbering the
    # two existing stored values.
    profile_code = OPTIONS_LAYOUT_VERSION
    profile_code = profile_code * 6 + normalized_options["locked_frequency_pot_option"]
    for name, radix in (
        ("model_cv_option", 5),
        ("level_cv_option", 3),
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
        ("attenuverter_mode", 3),
    ):
        profile_code = profile_code * radix + normalized_options[name]
    # The reversible encoding now occupies three bytes. The high byte reuses the
    # retired extra-fine-tune state byte, so every v4 profile is disjoint from
    # every older 16-bit marker and forces one correct defaults apply after the
    # upgrade. Low bytes 0 and 1 remain reserved for legacy navigation states.
    if profile_code >= 254 * 65536:
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

    # Sync In (v22). Compile-time-only, and deliberately NOT derived from the
    # starting value any more — see SYNC_INPUT_PREFERENCE_MIN_SCHEMA_VERSION.
    sync_input = bool(preferences.get("syncInput", False))
    if not isinstance(preferences.get("syncInput", False), bool):
        raise ValueError("recipe contains an unsupported firmware option")
    if sync_input and schema_version < SYNC_INPUT_PREFERENCE_MIN_SCHEMA_VERSION:
        raise ValueError(
            f"the Sync In preference requires schemaVersion "
            f"{SYNC_INPUT_PREFERENCE_MIN_SCHEMA_VERSION}")

    if normalized_options["model_cv_option"] == 4 and not sync_input:
        # Starting the module in a mode whose code was not compiled would leave
        # model_cv_option pointing past the end of the MODEL menu. Since v22 the
        # capability comes from the preference alone, so the pair must agree.
        raise ValueError(
            "starting in Sync In requires the syncInput preference")

    # Simplified pitch ranges (v24). Compile-time only, and independent of every
    # option: it changes which selector positions exist, not what any of them do.
    simplified_pitch_ranges = bool(preferences.get("simplifiedPitchRanges", False))
    if not isinstance(preferences.get("simplifiedPitchRanges", False), bool):
        raise ValueError("recipe contains an unsupported firmware option")
    if (simplified_pitch_ranges
            and schema_version < SIMPLIFIED_PITCH_RANGES_MIN_SCHEMA_VERSION):
        raise ValueError(
            f"the simplified pitch-range preference requires schemaVersion "
            f"{SIMPLIFIED_PITCH_RANGES_MIN_SCHEMA_VERSION}")

    # Experimental FM (v23). Linear TZFM chooses the modulation law; Fast FM
    # chooses the converter mode. They are intentionally independent. Fast FM
    # disables LEVEL CV for the entire firmware because FM and LEVEL share
    # SDADC2, while engines without enough callback headroom fall back safely to
    # their ordinary control-rate FM path.
    linear_tzfm = bool(preferences.get("linearTzfm", False))
    fast_fm = bool(preferences.get("fastFm", False))
    if (not isinstance(preferences.get("linearTzfm", False), bool)
            or not isinstance(preferences.get("fastFm", False), bool)):
        raise ValueError("recipe contains an unsupported firmware option")
    if ((linear_tzfm or fast_fm)
            and schema_version < EXPERIMENTAL_FM_MIN_SCHEMA_VERSION):
        raise ValueError(
            f"experimental FM preferences require schemaVersion "
            f"{EXPERIMENTAL_FM_MIN_SCHEMA_VERSION}")

    # Replaceable FM banks (v20). Compile-time-only like the two above, so it
    # must not touch the profile-id fold either. Default FALSE — see the field
    # comment on BuildRecipe: page-aligning the banks costs more than the stock
    # preset has spare, so this is opt-in and an un-flagged recipe keeps the
    # historical layout exactly.
    swappable_fm_banks = bool(preferences.get("replaceableFmBanks", False))
    if not isinstance(preferences.get("replaceableFmBanks", False), bool):
        raise ValueError("recipe contains an unsupported firmware option")
    if swappable_fm_banks and schema_version < SWAPPABLE_FM_BANKS_MIN_SCHEMA_VERSION:
        raise ValueError(
            f"replaceable FM banks require schemaVersion "
            f"{SWAPPABLE_FM_BANKS_MIN_SCHEMA_VERSION}")

    return BuildRecipe(
        public_slots=public_slots,
        chord_tables=chord_tables,
        scale_bank=scale_bank,
        options_profile_id=profile_id,
        enable_calibration=1 if enable_calibration else 0,
        roved_panel=1 if target == "plum-audio-roved" else 0,
        color_blind_mode=1 if color_blind_mode else 0,
        swappable_fm_banks=1 if swappable_fm_banks else 0,
        sync_input=1 if sync_input else 0,
        simplified_pitch_ranges=1 if simplified_pitch_ranges else 0,
        linear_tzfm=1 if linear_tzfm else 0,
        fast_fm=1 if fast_fm else 0,
        user_data_bank_overrides=tuple(user_data_banks),
        slot_bank_overrides=tuple(slot_banks),
        stereo_engines=stereo_engines,
        speech_banks=speech_banks,
        **normalized_options,
    )


def cpp_float(value: float) -> str:
    return f"{value:.1f}f"


def cpp_precise_float(value: float) -> str:
    literal = f"{float(value):.9g}"
    if "." not in literal and "e" not in literal:
        literal += ".0"
    return literal + "f"


def resolved_randomizer_parameter(engine_id: str, parameter: str) -> tuple[float, ...]:
    model = RANDOMIZER_REGISTRY["models"].get(
        engine_id, RANDOMIZER_REGISTRY["fallback"])
    specification = model[parameter]
    if isinstance(specification, str):
        archetype = specification
        overrides: dict[str, float] = {}
    else:
        archetype = specification["archetype"]
        overrides = specification.get("overrides", {})
    profile = {
        **RANDOMIZER_REGISTRY["parameterArchetypes"][archetype],
        **overrides,
    }
    return tuple(float(profile[field]) for field in (
        "nearSpan", "farSpan", "nearRate", "farRate", "farCenterRelease",
    ))


def cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def render_config(recipe: BuildRecipe) -> str:
    public_slots = recipe.public_slots
    # Public order is green, red, amber (+ optional orange). Three-bank builds
    # retain Plaits' legacy amber/green/red registry rotation. Four-bank builds
    # put orange in that leading position instead: orange/green/red/amber. This
    # keeps green and red at their legacy internal indices while making the
    # forward cycle from green read green/red/amber/orange (rather than the
    # reported green/red/orange/amber). Empty slots (None — v7 short banks) are
    # dropped, and each internal bank's engine count becomes PLAITS_BANK_SIZES
    # so navigation wraps at the real size.
    public_banks = [public_slots[i:i + 8] for i in range(0, len(public_slots), 8)]
    internal_order = [3, 0, 1, 2] if len(public_banks) > 3 else [2, 0, 1]
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
    # Drop trailing empty banks in the legacy three-bank layout (no engines, and
    # no later bank whose LED color would shift). Four-bank layouts always keep
    # all four entries: amber is last in that rotation, and kNumBanks == 4 tells
    # Ui::BankToColor to use the four-bank orange/green/red/amber color map even
    # when amber itself is empty.
    while len(public_banks) < 4 and len(bank_sizes) > 1 and bank_sizes[-1] == 0:
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
    lpc_words_mask = sum(1 << index for index, item in enumerate(selected) if item.behavior == "lpc-words")
    chiptune_mask = sum(1 << index for index, item in enumerate(selected) if item.behavior == "chiptune")
    resonator_envelope_mask = sum(
        1 << index
        for index, engine_id in enumerate(internal_slots)
        if engine_id in RESONATOR_ENVELOPE_ENGINE_IDS
    )

    # Resolve the catalog's semantic archetypes into a compact firmware table.
    # Only numeric profiles reached by this palette are emitted; duplicate
    # archetypes and duplicate slots share one five-float record.
    randomizer_profiles: list[tuple[float, ...]] = []
    randomizer_profile_index: dict[tuple[float, ...], int] = {}
    randomizer_pairs: list[tuple[int, int]] = []
    for engine_id in internal_slots:
        pair: list[int] = []
        for parameter in ("timbre", "morph"):
            profile = resolved_randomizer_parameter(engine_id, parameter)
            if profile not in randomizer_profile_index:
                randomizer_profile_index[profile] = len(randomizer_profiles)
                randomizer_profiles.append(profile)
            pair.append(randomizer_profile_index[profile])
        randomizer_pairs.append((pair[0], pair[1]))
    randomizer_profile_values = ", ".join(
        "{ " + ", ".join(cpp_precise_float(value) for value in profile) + " }"
        for profile in randomizer_profiles
    )
    randomizer_pair_values = ", ".join(
        f"{{ {timbre}, {morph} }}" for timbre, morph in randomizer_pairs
    )
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

    scale_entries: list[str] = []
    for scale in recipe.scale_bank:
        padded = [*scale["pitches"], *([0] * (MAX_SCALE_DEGREES - len(scale["pitches"])))]
        scale_entries.append(
            "{ { " + ", ".join(str(pitch) for pitch in padded)
            + f" }}, {len(scale['pitches'])} }}"
        )

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
    # The pointer table must span every bank index the engine table can reference:
    # indices 0..2 (factory, possibly overridden) plus every allocated custom one.
    table_size = next_custom_index
    has_user_data_bank = any(item.user_data_bank >= 0 for item in selected)

    # Every FM bank in the build is REPLACEABLE over audio: a bank's baked array
    # IS the flash region a transfer erases and reprograms. Nothing is reserved —
    # the 4 KB a bank already occupies simply becomes rewritable — which is why
    # this is free, and why an un-transferred slot needs no empty state: it reads
    # its baked bank like any other slot.
    #
    # The one cost is alignment. UserData::Save erases 2 KB flash PAGES, so a
    # region must cover whole pages with nothing else in them; a region sharing a
    # page with code or rodata would destroy the firmware on the first transfer.
    # So each bank is padded to PACKED_BANK_SIZE (= two pages) and 2 KB-aligned
    # in its OWN .user_data_banks.<i> section. Per-bank sections matter: one
    # shared section would defeat --gc-sections' ability to drop an unreferenced
    # bank individually, undoing the factory-bank strip.
    #
    # All of this is OPT-IN (the v20 "replaceable FM banks" preference). Left
    # off, the build keeps the historical layout byte-for-byte.
    swappable = has_user_data_bank and recipe.swappable_fm_banks

    # A LIVE STOCK bank normally links straight to resources.cc's syx_bank_N. A
    # swappable build cannot use those in place: their last bytes are real DX7
    # name characters, and 'U' followed by ' ' + bank would make a baked bank
    # read back as a transferred one. (None of the three alias today — their
    # tails are " 4  ", "SNAR", "MPET" — but that is luck, not a guarantee, and
    # the failure would be silent and data-dependent.) So the generator emits its
    # own copy with those bytes cleared. syx_bank_N then goes unreferenced and
    # --gc-sections drops it, so this costs nothing.
    if swappable:
        stock_live = sorted(
            index for index in used_factory
            if index not in {i for i, _ in override_arrays_all}
        )
        if stock_live:
            factory_banks = load_factory_bank_bytes()
            override_arrays_all = override_arrays_all + [
                (index, factory_banks[index]) for index in stock_live
            ]
    override_by_index = {index for index, _ in override_arrays_all}
    # Each baked bank's REAL content length — the number of packed patch bytes
    # behind the pointer, NOT the emitted array size. A swappable short bank is
    # padded up to a whole region, but voice.cc must still size the engine's
    # Harmonics quantizer to the patches that are actually there, or a 3-patch
    # bank would sweep 32 steps with 29 silent ones and schema v13's short banks
    # would be undone.
    size_by_index = {index: len(data) for index, data in override_arrays_all}

    def _bank_bytes(data: bytes) -> bytes:
        """The bytes actually emitted for a baked bank array."""
        if not swappable:
            return bytes(data)
        padded = bytearray(data) + bytes(PACKED_BANK_SIZE - len(data))
        # Clear the count/tag bytes so a BAKED bank can never be misread as a
        # TRANSFERRED one by UserData::ptr. They are the tail of voice 32's name
        # field, which Plaits never displays.
        padded[PACKED_BANK_SIZE - 4:] = b"\x00\x00\x00\x00"
        return bytes(padded)

    def _bank_attribute(index: int) -> str:
        if not swappable:
            return ""
        return (f' __attribute__((section(".user_data_banks.{index}"),'
                f" aligned({FLASH_PAGE_SIZE})))")

    def _render_bank_array(index: int, data: bytes) -> str:
        body = ", ".join(str(byte) for byte in _bank_bytes(data))
        size = len(_bank_bytes(data))
        if not swappable:
            return (f"static const uint8_t kUserDataBankOverride_{index}[{size}]"
                    f" = {{ {body} }};")
        # ONE definition program-wide, not a static per translation unit: the
        # region table hands its address to UserData::Save (plaits.cc) while
        # voice.cc reads through the same pointer, and two copies at different
        # addresses would have Save write one and the engine read the other. The
        # makefile gives voice.o the owning define; every other unit that force-
        # includes this header sees a declaration and skips the initializer.
        # `extern` on the DEFINITION is load-bearing, not decoration: a
        # namespace-scope const has INTERNAL linkage by default in C++, so
        # without it voice.o's definition stays file-local and plaits.o's
        # declaration fails to link.
        return (
            f"#ifdef PLAITS_ENGINE_CONFIG_OWNS_USER_DATA_BANKS\n"
            f"extern const uint8_t kUserDataBankOverride_{index}[{size}]"
            f"{_bank_attribute(index)};\n"
            f"extern const uint8_t kUserDataBankOverride_{index}[{size}]"
            f" = {{ {body} }};\n"
            f"#else\n"
            f"extern const uint8_t kUserDataBankOverride_{index}[{size}];\n"
            f"#endif"
        )

    override_arrays = "\n".join(
        _render_bank_array(index, data) for index, data in override_arrays_all
    )
    # Resolve every reachable bank index to its actual data pointer AT BUILD TIME,
    # so the firmware never NAMES a factory blob it can't reach. Per index:
    #   * overridden (a v6 whole-bank override, or a v12 per-slot custom bank) ->
    #     its baked kUserDataBankOverride_i array;
    #   * a live stock factory bank (a placed, un-customized slot maps to it) ->
    #     syx_bank_i, or — in a swappable build — the generator's own cleared,
    #     page-aligned copy of it, which is likewise a kUserDataBankOverride_i;
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
    # One region per BANK the firmware actually carries — the flash that bank's
    # baked array occupies, which a TIMBRE transfer erases and reprograms in
    # place. Keyed on bank, not slot: two placed slots may map to one factory
    # bank and share its 4 KB, and both must see a transfer made through either.
    # An absent (NULL) bank gets no region, so Save refuses that slot instead of
    # erasing an address it does not own.
    region_banks = [i for i in range(table_size) if _resolved_pointer(i) != "NULL"]
    regions_table = (
        "static const UserDataRegion kUserDataRegions[{count}] = {{ {entries} }};\n"
        "static const int kNumUserDataRegions = {count};".format(
            count=len(region_banks),
            entries=", ".join(
                f"{{ {_resolved_pointer(i)}, {i} }}" for i in region_banks),
        )
    ) if swappable and region_banks else ""
    user_data_bank_override_block = (
        f"\n#if PLAITS_HAS_USER_DATA_BANK\n{override_arrays}\n{resolved_table}\n"
        f"{resolved_size_table}\n{regions_table}\n#endif\n"
        if has_user_data_bank else ""
    )

    # Compiled in solely from the v22 preference. The starting value does NOT
    # imply it: validate_recipe rejects sync-in-without-the-preference outright,
    # because emitting that pair would build a 4-entry MODEL menu (ui.cc sizes it
    # 4 + PLAITS_BUILD_ENABLE_SYNC_INPUT) while starting the module at index 4.
    sync_input_enabled = 1 if recipe.sync_input else 0

    registry_order = (
        "orange, green, red, amber"
        if len(public_banks) > 3
        else "amber, green, red"
    )

    return f"""// Generated by alt_firmwares/plaits_lab_builder/generate_engine_config.py.
// Public recipe order: green, red, amber, optional orange. Registry order: {registry_order}.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

// These feature gates must precede the engine includes: engine.h uses them to
// erase the optional frequency-offset interface from ordinary builds.
#define PLAITS_BUILD_LINEAR_TZFM {recipe.linear_tzfm}
#define PLAITS_BUILD_FAST_FM {recipe.fast_fm}

{includes}
#include "plaits/resources.h"
#include "plaits/user_data_region.h"

#define PLAITS_ENGINE_COUNT {len(selected)}
#define PLAITS_BANK_SIZES {{ {", ".join(str(size) for size in bank_sizes)} }}
#define PLAITS_ENGINE_ROWS {{ {", ".join(str(row) for row in engine_rows)} }}
#define PLAITS_HAS_SPEECH_ENGINE {1 if any(item.behavior == 'speech' for item in selected) else 0}
#define PLAITS_HAS_LPC_WORDS_ENGINE {1 if any(item.behavior == 'lpc-words' for item in selected) else 0}
#define PLAITS_HAS_CHIPTUNE_ENGINE {1 if any(item.behavior == 'chiptune' for item in selected) else 0}
#define PLAITS_HAS_USER_DATA_BANK {1 if has_user_data_bank else 0}
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE {1 if override_arrays_all else 0}
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK {1 if has_user_data_bank else 0}
#define PLAITS_RESONATOR_ENVELOPE_ENGINE_MASK 0x{resonator_envelope_mask:08x}u
// Every FM bank's baked array doubles as the flash region a TIMBRE transfer
// erases and reprograms, so any bank can be replaced without a reflash. 0 when
// the Advanced "lock FM banks" preference is set (or there are no FM banks):
// banks are then emitted at their natural length, unaligned and unreplaceable.
#define PLAITS_HAS_SWAPPABLE_USER_DATA_BANKS {1 if swappable and region_banks else 0}

#define PLAITS_RANDOMIZER_PROFILES {{ {randomizer_profile_values} }}
#define PLAITS_ENGINE_RANDOMIZER_PROFILE_INDICES {{ {randomizer_pair_values} }}

#define PLAITS_CHORD_TABLE_COUNT {len(recipe.chord_tables)}
#define PLAITS_CHORD_COUNT {chord_offset}
#define PLAITS_CHORD_TABLE_OFFSETS {{ {", ".join(str(value) for value in chord_offsets)} }}
#define PLAITS_CHORD_TABLE_SIZES {{ {", ".join(str(value) for value in chord_sizes)} }}
#define PLAITS_CHORD_CENTS {{ {", ".join(chord_cents)} }}
#define PLAITS_CHORD_ARP_LENGTHS {{ {", ".join(chord_arp_lengths)} }}

#define PLAITS_SCALE_BANK_COUNT {len(recipe.scale_bank)}
#define PLAITS_SCALE_BANK {{ {", ".join(scale_entries)} }}

#define PLAITS_BUILD_NAVIGATION_MODE {recipe.navigation_mode}
#define PLAITS_BUILD_COLOR_BLIND_MODE {recipe.color_blind_mode}
#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION {recipe.locked_frequency_pot_option}
#define PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE {1 if recipe.locked_frequency_pot_option >= 4 else 0}
#define PLAITS_BUILD_MODEL_CV_OPTION {recipe.model_cv_option}
#define PLAITS_BUILD_ENABLE_SYNC_INPUT {sync_input_enabled}
#define PLAITS_BUILD_SIMPLIFIED_PITCH_RANGES {recipe.simplified_pitch_ranges}
#define PLAITS_BUILD_LEVEL_CV_OPTION {recipe.level_cv_option}
#define PLAITS_BUILD_AUX_OUTPUT_OPTION {recipe.aux_output_option}
#define PLAITS_BUILD_AUX_SUBOSC_OPTION {recipe.aux_subosc_option}
#define PLAITS_BUILD_CHORD_SET_OPTION {recipe.chord_set_option}
#define PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION {recipe.hold_on_trigger_option}
#define PLAITS_BUILD_ATTENUVERTER_MODE {recipe.attenuverter_mode}
#define PLAITS_BUILD_ENABLE_CALIBRATION {recipe.enable_calibration}
#define PLAITS_ROVED_PANEL {recipe.roved_panel}
#define PLAITS_BUILD_OPTIONS_PROFILE_ID 0x{recipe.options_profile_id:06x}u

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
#if PLAITS_HAS_LPC_WORDS_ENGINE
static const uint32_t kLPCWordsEngineMask = 0x{lpc_words_mask:08x};
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
