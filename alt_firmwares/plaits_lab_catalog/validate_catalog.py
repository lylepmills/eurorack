#!/usr/bin/env python3
"""Validate the authoritative Plaits Lab package catalog."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any


CATALOG_DIR = Path(__file__).resolve().parent
REPO_ROOT = CATALOG_DIR.parents[1]
CATALOG_PATH = CATALOG_DIR / "catalog.json"
SHARED_MODULES_PATH = CATALOG_DIR / "shared_modules.json"
REFERENCE_PACKAGES_PATH = (
    REPO_ROOT / "alt_firmwares" / "plaits_lab_sdk" / "packages" /
    "mutable-instruments"
)
PACKAGES_PATH = REPO_ROOT / "alt_firmwares" / "plaits_lab_sdk" / "packages"
ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*$")
PACKAGE_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9-]*$")
CONTROL_IDS = ("harmonics", "timbre", "morph", "macro")

# These panel-order mappings were audited against Mutable Instruments' Plaits
# manual and the corresponding engine source. Keep them explicit: a generic
# four-item length check cannot detect a TIMBRE/MORPH swap.
AUDITED_STOCK_CONTROLS = {
    "virtual-analog": ["Detune", "Variable square", "Variable saw", "Oscillator balance"],
    "waveshaping": ["Waveshaper waveform", "Fold amount", "Waveform asymmetry", "Wavefolder symmetry"],
    "granular-formant": ["Formant ratio", "Formant frequency", "Formant width and shape", "Carrier bleed"],
    "harmonic": ["Spectral bumps", "Prominent harmonic", "Bump shape", "Odd/even balance"],
    "chords": ["Chord", "Inversion and transposition", "Waveform", "Voice-group balance"],
    "speech": ["Model and word bank", "Species", "Phoneme or word", "Spectral emphasis"],
    "swarm": ["Pitch randomization", "Grain density", "Grain duration and overlap", "Size dispersion"],
    "filtered-noise": ["Filter response", "Clock frequency", "Resonance", "Filter position"],
    "particle-noise": ["Frequency randomization", "Density", "Filter type", "Diffusion"],
    "analog-bass-drum": ["Attack and overdrive", "Brightness", "Decay", "Pitch-envelope punch"],
    "analog-snare": ["Harmonic/noise balance", "Mode balance", "Decay", "Shell inharmonicity"],
    "analog-hi-hat": ["Metal/noise balance", "High-pass cutoff", "Decay", "Oscillator spacing"],
    "virtual-analog-vcf": ["Resonance and filter character", "Filter cutoff", "Waveform and sub level", "Drive"],
    "phase-distortion": ["Distortion frequency", "Distortion amount", "Asymmetry", "Modulator ratio"],
    "dx7-bank-a": ["Patch", "Brightness / carrier tilt", "Envelope", "Network detune"],
    "dx7-bank-b": ["Patch", "Brightness / carrier tilt", "Envelope", "Network detune"],
    "dx7-bank-c": ["Patch", "Brightness / carrier tilt", "Envelope", "Network detune"],
    "wave-terrain": ["Terrain", "Path radius", "Path offset", "Y offset"],
    "string-machine": ["Chord", "Filter and chorus", "Waveform and registration", "Ensemble amount"],
    "chiptune": ["Chord", "Arpeggio or inversion", "Pulse width and sync", "Register preset"],
}

# All 34 Braids-derived catalog entries, audited in panel order against their
# Render() implementations and the corresponding Braids source. Several entries
# merge related Braids shapes, so this covers the 48 panel models plus the hidden
# Question Mark model.
AUDITED_BRAIDS_CONTROLS = {
    "blown": ["Body", "Focus", "Blow", "Reed"],
    "bowed": ["Bow position", "Bow pressure", "Nut damping", "Body"],
    "buzz": ["Detune", "Buzz", "Spread", "Reach"],
    "csaw": ["Depth", "Width", "Bend", "Tilt"],
    "cymbal": ["Color", "Tone", "Spread", "Resonance"],
    "digital-modulation": ["Frame", "Symbol rate", "Shaping", "Payload"],
    "dual-sync": ["Balance", "Interval", "Shape", "Reset"],
    "fluted": ["Embouchure", "Air", "Blow", "Body"],
    "fold": ["Blend", "Fold", "Symmetry", "Drive"],
    "granular-cloud": ["Scatter", "Grain", "Shape", "Density"],
    "harmonics": ["Colour", "Peak", "Spread", "Width"],
    "kick": ["Tone", "Decay", "Balance", "Punch"],
    "morph": ["Fuzz", "Shape", "Tone", "Drive"],
    "noise-bank": ["Model", "Colour", "Shape", "Drive"],
    "particle-burst": ["Scatter", "Density", "Chord width", "Decay"],
    "plucked": ["Pluck", "Damping", "Spread", "Stretch"],
    "question-mark": ["Static", "Speed", "Bed", "Grit"],
    "raw-fm": ["Ratio", "Index", "Character", "Depth"],
    "ring-mod": ["Detune 1", "Detune 2", "Depth", "Drive"],
    "saw-comb": ["Resonance", "Comb pitch", "Exciter", "Loop tilt"],
    "saw-square": ["Blend", "Shape", "Phase", "Square Level"],
    "saw-swarm": ["Color", "Detune", "Filter", "Resonance"],
    "snare": ["Snap", "Tone", "Snap balance", "Spread"],
    "struck-bell": ["Detune", "Decay", "Brightness", "Decay spread"],
    "struck-drum": ["Color", "Decay", "Spread", "Decay spread"],
    "sub-oscillator": ["Shape", "Width", "Sub level", "Sub width"],
    "toy": ["Mangle", "Crush", "Clock", "Fold"],
    "triple": ["Spread", "Interval", "Waveform", "Shape"],
    "vosim": ["Formant 2", "Formant 1", "Window", "Balance"],
    "vowel": ["Spread", "Shift", "Vowel", "Grain"],
    "vowel-fof": ["Brightness", "Vowel", "Register", "Formant tilt"],
    "wave-paraphonic": ["Chord", "Wave", "Fan", "Spread"],
    "wave-scan": ["Model", "Scan", "Shape", "Snap"],
    "z-filter": ["Model", "Cutoff", "Shape", "Bend"],
}

# These engines always render one fixed MAIN/AUX pair. They do not branch on
# EngineParameters::stereo, so the editor's stereo selection cannot change the
# pair. Every other Braids port has a mode-dependent stereo path and must say so
# in its output documentation.
BRAIDS_FIXED_PAIR_ENGINES = {
    "fluted", "noise-bank", "raw-fm", "saw-comb", "sub-oscillator",
    "triple", "z-filter",
}

# These are the engines outside the previously audited stock and Braids
# sets. Each digest locks every user-facing identity and documentation field:
# name, credit, origin, family, description, tags, control labels, output
# behavior, and the complete panel manual. A prose edit therefore requires an
# explicit implementation-level re-audit rather than silently weakening the
# coverage back to schema validation.
AUDITED_REMAINING_METADATA_DIGESTS = {
    "virtual-analog-dual": "sha256:3cf2c0e87174b5379ed9ab943a1b2dc7d9c995453bc103b17ab13f4d175799f9",
    "virtual-analog-crossfade": "sha256:e262651501514c05a20e8a9f633472d3ded280e664f3144c54e57421884a7c2a",
    "formant-speech": "sha256:5d8a76de71896ce2e58927317e2f65015a10c03f7f7d55637e0b57cdc24ced21",
    "lpc-speech": "sha256:f671ab3b21119cbaa99103a5ba2d15b363891149be21917dbbc4156fe62fe862",
    "glisson": "sha256:939633e80e2942e22599767bfdeef3b1b422fb06df32224fa2072198be5ee6c3",
    # Natural Speech: WORLD-analysed word banks through an order-18 vocal
    # tract. Metadata audited against the engine 2026-08-29.
    "natural-speech": "sha256:728890057d1922e7f5a64394a062db81eae1dbc0081e0c998f8218b072385899",
    "gendy": "sha256:045f7174602a8bb3e6beeb8cb8021d1eacfc19b11fc36f7edcedcafb7928a1ca",
    "scanned": "sha256:c9dd679aca7e01a9ec7ca5edc15bf4c743e727c7859ebe6276c31fe417fb744e",
    "pulsar": "sha256:a0315377a7a47b4baaf0d39f6364dcc16a03ee17c515652f1ed298b62db00821",
    "loopback": "sha256:8a911176af0bfe0e293039804e3ca05e2bb1631616edb0fa454c481aeeecbaa0",
    "lockstep": "sha256:00cb95b7c04fd4301fb24d63365f6f1e9856d1a37818edbe3af5a326fcc24a78",
    "tapfield": "sha256:6578f7d5917bb2ec47614f15bb6e9eb2cffb4134e1a7762079cc10f794cc66c3",
    "phase-weave": "sha256:4d3c9735a774a99ec4083f1f4a771b36b85ee478411c7f5bc3d30a9d8af781e6",
    "sideband-bank": "sha256:8e7e18b53e53f0a4c691b96c8442dac9325fe88a00d8b8d69f5cc3bf91d1d8b8",
    "attractor": "sha256:8876de4803dacb7ffd801b56e4cffcbd9ee27d5ece2d14b84f8b0ec2faecd6c0",
    "undertow": "sha256:355e1efde585a8822ff8c8aaa1f53b25798e32b4878d24233d872f425f3a80a6",
    "reed-pipe": "sha256:08eb4b2000fff1efcd710089749689b251ed74e8d682f7ab325c01563e5d9476",
    "phase-flock": "sha256:881288d0c617006b8718438b5f1f917716b872bb9ee7f7b08838eab759a6ee1c",
    "rulefield": "sha256:1463741d326d01c87240f534c4a955986f7252bcb0907ef70a5b07ff8b43c9b0",
    "spectral-spiral": "sha256:16ccf227fdceea2257d2bb262ef4deeeb1ef50313ec6dba150fad36dce72d86c",
    "bytebeat": "sha256:cdd0f83830b959013dc0b5cebf635def32475b97d3ad36d5ff682db008df4ebf",
    "diatonic-chord": "sha256:a371bf948b5dec7234ae430b96e7995f913b1f5881e4a829c23ff75135fca3b2",
    "scale-stack": "sha256:c480bab7debf258bd43fefea77d2cbc4e174284de48374c4769d2d8d49764682",
    "wavetable-chord": "sha256:cf8f1483af74d2d6fdf76fbc02c2a514ea1cf5a9a59cf18e7186cfe0329c1d6f",
    "wavetable-scale-stack": "sha256:df642cdb8d7eba16ed2befd57b5735a45ab889c69a0288e0f26dacc7a5c2fcfe",
    "shakers": "sha256:dc3b8a2a7e0daa8ae80e1dd85ff39377e1b1d79fd6efc753d8cb712160481448",
    "brass": "sha256:bf2dbd2b7a717b28f18e9d4a28e7e61b3ee376da438c2ec2a6cc4f528cfa286d",
    "helix": "sha256:f15a76e28e45bb7d7ee18d07c62d3367d60b0d463bafaf603ba975af3476836d",
    # Rubato percussion trio: control behavior, trigger semantics, mono AUX,
    # and the dedicated stereo paths audited during the 2026-09-01 review.
    "skins": "sha256:07e1a710343cca0d2f806a01af172c8e80430a5b16ddc1b1eb353b080f75c78c",
    "circuit-zaps": "sha256:80510a65f159b4eb2dffab2144112b7089be74c0684c67ca44f6cc73b01dd19d",
    "metalwork": "sha256:fac00d54bcbbb807a6e83699cd8c0a834957ad7219d5d30715427e6c9dcd2687",
    "clap": "sha256:42049635c356f8518329a9d9fd5be5285d2ebaef89c29676e0f785f7053be173",
    "analog-percussion": "sha256:edc5a2223b0862c75f310357717ef162927c29eddfd2ec6da1ac308aa8346cd1",
    "freshets-formant": "sha256:5b17fc800bf990df2a8091a893999d6493974a8f139119eaef188039fd8f7306",
    # Combust community engines: source, stereo behavior, controls, and
    # hardware CPU diagnostics audited during the 2026-08-31 review.
    "bubbletime": "sha256:ab82314d6d6e98a1e195513e7a2300beb6b9e1302352fabbe38428224bae387f",
    "zxphase48k": "sha256:68fd122b61ce457a0481760bf6348308495686d87a4b1a7447a4b2e09786c151",
    "zxpulse48k": "sha256:cdcdf57550901493db4ded937e0085683922a8d428b94a2c8c920f011d5c896a",
    # Acid: software gauntlet and stereo hardware CPU diagnostics audited
    # during the 2026-09-01 community review.
    "acid": "sha256:0393a1306b7b8ace98e2bbd9924346fbff710635eeb0df2f48e0280adb6c0fe8",
}

REMAINING_FIXED_PAIR_ENGINES = {
    "pulsar", "attractor", "spectral-spiral", "bytebeat",
    "diatonic-chord", "scale-stack", "wavetable-chord",
    "wavetable-scale-stack", "shakers", "brass",
}

AUDITED_STOCK_OUTPUTS = {
    "two-op-fm": ["FM voice", "Sub-oscillator"],
    "filtered-noise": [
        "Filtered noise",
        "Mono: two separated band-pass voices. Stereo: decorrelated matching filter.",
    ],
    "dx7-bank-a": [
        "Six-operator FM voice; left channel in stereo",
        "Same voice mix in mono; right channel in stereo",
    ],
    "dx7-bank-b": [
        "Six-operator FM voice; left channel in stereo",
        "Same voice mix in mono; right channel in stereo",
    ],
    "dx7-bank-c": [
        "Six-operator FM voice; left channel in stereo",
        "Same voice mix in mono; right channel in stereo",
    ],
    "string-machine": ["Voices 1 and 3 predominantly", "Voices 2 and 4 predominantly"],
    "chords": [
        "Mono: full chord. Stereo: left side of the chord spread.",
        "Mono: root or alternate voice group. Stereo: right side of the chord spread.",
    ],
    "chiptune": ["Chiptune chord or arpeggio", "NES triangle bass"],
}

AUDITED_STOCK_TRIGGERS = {
    "chiptune": "Advances the arpeggiator and restarts its envelope on each patched TRIG pulse.",
}


def load_catalog() -> dict[str, Any]:
    value = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if value.get("schemaVersion") != 1 or value.get("sdk") != "plaits-engine-cpp-v1":
        raise ValueError("unsupported catalog schema")
    return value


def package_digest(engine: dict[str, Any]) -> str:
    digest = hashlib.sha256()
    package_record = {key: value for key, value in engine.items() if key != "digest"}
    digest.update(json.dumps(package_record, sort_keys=True, separators=(",", ":")).encode("utf-8"))
    source_paths = [engine["source"]["header"], *engine["source"]["files"]]
    for relative in sorted(set(source_paths)):
        path = (REPO_ROOT / relative).resolve()
        path.relative_to(REPO_ROOT)
        digest.update(relative.encode("utf-8"))
        digest.update(path.read_bytes())
    return "sha256:" + digest.hexdigest()


def documentation_digest(engine: dict[str, Any], manual: dict[str, Any]) -> str:
    record = {
        **{key: value for key, value in engine.items() if key not in {"source", "postProcessing"}},
        "manual": manual,
    }
    payload = json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return "sha256:" + hashlib.sha256(payload).hexdigest()


def audited_metadata_digest(engine: dict[str, Any], manual: dict[str, Any]) -> str:
    record = {
        key: engine[key]
        for key in (
            "name", "author", "origin", "family", "description", "tags",
            "controls", "outputs",
        )
    }
    record["manual"] = manual
    payload = json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return "sha256:" + hashlib.sha256(payload).hexdigest()


def load_shared_modules() -> dict[str, Any]:
    value = json.loads(SHARED_MODULES_PATH.read_text(encoding="utf-8"))
    modules = value.get("modules")
    if not isinstance(modules, dict):
        raise ValueError("shared_modules.json must contain a modules object")
    for module_id, module in modules.items():
        if not ID_PATTERN.fullmatch(module_id):
            raise ValueError(f"invalid shared module ID: {module_id}")
        if not module.get("sources"):
            raise ValueError(f"shared module {module_id} must declare at least one source")
        for relative in [*module.get("headers", []), *module["sources"]]:
            path = (REPO_ROOT / relative).resolve()
            path.relative_to(REPO_ROOT)
            if not path.is_file():
                raise ValueError(f"shared module {module_id} file does not exist: {relative}")
    return modules


def load_braids_manifests() -> dict[str, dict[str, Any]]:
    manifests = {}
    for path in sorted(REFERENCE_PACKAGES_PATH.glob("*/plaits-engine.json")):
        value = json.loads(path.read_text(encoding="utf-8"))
        if "braids" not in value.get("tags", []):
            continue
        catalog_id = value.get("catalogId")
        if not isinstance(catalog_id, str) or catalog_id in manifests:
            raise ValueError(f"invalid or duplicate Braids catalogId in {path}")
        if not isinstance(value.get("trigger"), str):
            raise ValueError(f"{catalog_id} Braids manifest must declare trigger metadata")
        manifests[catalog_id] = value
    if set(manifests) != set(AUDITED_BRAIDS_CONTROLS):
        raise ValueError("audited Braids controls must cover every Braids manifest")
    return manifests


def load_audited_remaining_manifests() -> dict[str, dict[str, Any]]:
    manifests = {}
    for path in sorted(PACKAGES_PATH.glob("*/*/plaits-engine.json")):
        value = json.loads(path.read_text(encoding="utf-8"))
        catalog_id = value.get("catalogId")
        if catalog_id not in AUDITED_REMAINING_METADATA_DIGESTS:
            continue
        if catalog_id in manifests:
            raise ValueError(f"duplicate audited package manifest for {catalog_id}")
        if not isinstance(value.get("trigger"), str):
            raise ValueError(f"{catalog_id} audited package must declare trigger metadata")
        manifests[catalog_id] = value
    return manifests


def validate_catalog(catalog: dict[str, Any]) -> None:
    engines = catalog.get("engines")
    if not isinstance(engines, list) or not engines:
        raise ValueError("catalog engines must be a non-empty array")
    known_modules = set(load_shared_modules())
    ids: set[str] = set()
    packages: set[str] = set()
    engines_by_id: dict[str, dict[str, Any]] = {}
    for engine in engines:
        engine_id = engine.get("id")
        package_id = engine.get("packageId")
        if not isinstance(engine_id, str) or not ID_PATTERN.fullmatch(engine_id):
            raise ValueError(f"invalid engine ID: {engine_id}")
        if engine_id in ids:
            raise ValueError(f"duplicate engine ID: {engine_id}")
        if not isinstance(package_id, str) or not PACKAGE_PATTERN.fullmatch(package_id):
            raise ValueError(f"invalid package ID: {package_id}")
        if package_id in packages:
            raise ValueError(f"duplicate package ID: {package_id}")
        ids.add(engine_id)
        packages.add(package_id)
        engines_by_id[engine_id] = engine
        if len(engine.get("controls", [])) != 4 or len(engine.get("outputs", [])) != 2:
            raise ValueError(f"{engine_id} must declare four controls and two outputs")
        source = engine.get("source", {})
        for key in ("header", "className", "member", "files"):
            if key not in source:
                raise ValueError(f"{engine_id} source is missing {key}")
        for relative in [source["header"], *source["files"]]:
            path = (REPO_ROOT / relative).resolve()
            path.relative_to(REPO_ROOT)
            if not path.is_file():
                raise ValueError(f"{engine_id} source does not exist: {relative}")
        post = engine.get("postProcessing", {})
        if set(post) != {"alreadyEnveloped", "outGain", "auxGain"}:
            raise ValueError(f"{engine_id} has invalid post-processing metadata")
        modules = engine.get("sharedModules")
        if modules is not None:
            if (not isinstance(modules, list)
                    or len(modules) != len(set(modules))
                    or any(module_id not in known_modules for module_id in modules)):
                raise ValueError(f"{engine_id} references an unknown or duplicate shared module")

    for engine_id, expected in AUDITED_STOCK_CONTROLS.items():
        actual = engines_by_id.get(engine_id, {}).get("controls")
        if actual != expected:
            raise ValueError(
                f"{engine_id} controls no longer match the audited "
                "HARMONICS/TIMBRE/MORPH/Macro order")
    for engine_id, expected in AUDITED_BRAIDS_CONTROLS.items():
        actual = engines_by_id.get(engine_id, {}).get("controls")
        if actual != expected:
            raise ValueError(
                f"{engine_id} controls no longer match the audited "
                "Braids HARMONICS/TIMBRE/MORPH/Macro order")
    for engine_id, expected in AUDITED_STOCK_OUTPUTS.items():
        actual = engines_by_id.get(engine_id, {}).get("outputs")
        if actual != expected:
            raise ValueError(f"{engine_id} outputs no longer match the audited behavior")

    fm_capabilities = catalog.get("fmCapabilities")
    if not isinstance(fm_capabilities, dict) or set(fm_capabilities) != {
            "linearTzfm", "fastFm"}:
        raise ValueError("fmCapabilities must declare linearTzfm and fastFm")
    for capability, engine_ids in fm_capabilities.items():
        if (not isinstance(engine_ids, list)
                or len(engine_ids) != len(set(engine_ids))
                or any(engine_id not in ids for engine_id in engine_ids)):
            raise ValueError(
                f"fmCapabilities.{capability} must list unique approved engine IDs")

    for name, slots in catalog.get("presets", {}).items():
        if len(slots) not in (24, 32) or any(engine_id not in ids for engine_id in slots):
            raise ValueError(f"preset {name} must contain 24 or 32 approved engine IDs")

    manuals = catalog.get("manuals")
    if not isinstance(manuals, dict) or set(manuals) != ids:
        raise ValueError("manuals must contain exactly one entry for every engine")
    for engine_id, manual in manuals.items():
        if not isinstance(manual, dict) or set(manual) != {"controls", "trigger"}:
            raise ValueError(f"{engine_id} manual must contain controls and trigger")
        controls = manual["controls"]
        if not isinstance(controls, dict) or tuple(controls) != CONTROL_IDS:
            raise ValueError(f"{engine_id} manual controls must be in panel order")
        for control_id, description in controls.items():
            if not isinstance(description, str) or not 12 <= len(description) <= 180:
                raise ValueError(f"{engine_id} manual {control_id} description must contain 12-180 characters")
        trigger = manual["trigger"]
        if not isinstance(trigger, str) or not 12 <= len(trigger) <= 180:
            raise ValueError(f"{engine_id} manual trigger description must contain 12-180 characters")
        expected_trigger = AUDITED_STOCK_TRIGGERS.get(engine_id)
        if expected_trigger is not None and trigger != expected_trigger:
            raise ValueError(f"{engine_id} trigger no longer matches the audited behavior")

    stock_ids = set(catalog.get("presets", {}).get("stock", []))
    audited_sets = stock_ids | set(AUDITED_BRAIDS_CONTROLS) | set(AUDITED_REMAINING_METADATA_DIGESTS)
    if audited_sets != ids:
        missing = sorted(ids - audited_sets)
        extra = sorted(audited_sets - ids)
        raise ValueError(
            f"documentation audit must cover every engine; missing={missing}, extra={extra}")

    for engine_id, expected_digest in AUDITED_REMAINING_METADATA_DIGESTS.items():
        engine = engines_by_id[engine_id]
        actual_digest = audited_metadata_digest(engine, manuals[engine_id])
        if actual_digest != expected_digest:
            raise ValueError(
                f"{engine_id} user-facing metadata changed after its implementation audit")
        output_copy = " ".join(engine["outputs"]).lower()
        if engine_id in REMAINING_FIXED_PAIR_ENGINES:
            if "stereo toggle does not change" not in output_copy:
                raise ValueError(
                    f"{engine_id} fixed-pair output docs must explain the stereo toggle")
        elif "mono:" not in output_copy or "stereo:" not in output_copy:
            raise ValueError(
                f"{engine_id} mode-dependent output docs must explain mono and stereo behavior")

    for engine_id, manifest in load_audited_remaining_manifests().items():
        engine = engines_by_id[engine_id]
        expected_engine_fields = {
            "description": manifest["description"],
            "tags": manifest["tags"],
            "controls": [control["label"] for control in manifest["controls"]],
            "outputs": [manifest["outputs"]["main"], manifest["outputs"]["aux"]],
        }
        for field, expected in expected_engine_fields.items():
            if engine.get(field) != expected:
                raise ValueError(
                    f"{engine_id} catalog {field} has drifted from its package manifest")
        expected_manual = {
            "controls": {
                control["id"]: control["description"]
                for control in manifest["controls"]
            },
            "trigger": manifest["trigger"],
        }
        if manuals[engine_id] != expected_manual:
            raise ValueError(
                f"{engine_id} catalog manual has drifted from its package manifest")

    braids_manifests = load_braids_manifests()
    for engine_id, manifest in braids_manifests.items():
        engine = engines_by_id[engine_id]
        expected_engine_fields = {
            "description": manifest["description"],
            "tags": manifest["tags"],
            "controls": [control["label"] for control in manifest["controls"]],
            "outputs": [manifest["outputs"]["main"], manifest["outputs"]["aux"]],
        }
        for field, expected in expected_engine_fields.items():
            if engine.get(field) != expected:
                raise ValueError(
                    f"{engine_id} catalog {field} has drifted from its package manifest")
        expected_manual = {
            "controls": {
                control["id"]: control["description"]
                for control in manifest["controls"]
            },
            "trigger": manifest["trigger"],
        }
        if manuals[engine_id] != expected_manual:
            raise ValueError(
                f"{engine_id} catalog manual has drifted from its package manifest")

        output_copy = " ".join(expected_engine_fields["outputs"]).lower()
        if engine_id in BRAIDS_FIXED_PAIR_ENGINES:
            if "stereo toggle does not change" not in output_copy:
                raise ValueError(
                    f"{engine_id} fixed-pair output docs must explain the stereo toggle")
        elif "stereo" not in output_copy:
            raise ValueError(
                f"{engine_id} mode-dependent output docs must explain stereo behavior")


def web_catalog(catalog: dict[str, Any]) -> dict[str, Any]:
    return {
        "schemaVersion": catalog["schemaVersion"],
        "sdk": catalog["sdk"],
        "packageVersion": catalog["packageVersion"],
        "engines": [
            {
                **{key: value for key, value in engine.items() if key not in {"source", "postProcessing"}},
                "version": catalog["packageVersion"],
                "digest": package_digest(engine),
                "documentationDigest": documentation_digest(engine, catalog["manuals"][engine["id"]]),
                "manual": catalog["manuals"][engine["id"]],
                "implementation": {
                    "className": engine["source"]["className"],
                    **({"userDataBank": engine["source"]["userDataBank"]} if "userDataBank" in engine["source"] else {}),
                },
            }
            for engine in catalog["engines"]
        ],
        "fmCapabilities": catalog["fmCapabilities"],
        "presets": catalog["presets"],
    }


DIGESTS_PATH = Path(__file__).with_name("digests.json")


def current_digests(catalog: dict[str, Any]) -> dict[str, str]:
    return {
        engine["id"]: package_digest(engine)
        for engine in catalog["engines"]
    }


def digest_drift(catalog: dict[str, Any]) -> tuple[list[str], list[str], list[str]]:
    """Engines whose digest moved, plus those added and removed.

    package_digest hashes the catalog record AND the raw bytes of
    source.header and every source.file -- so editing a COMMENT in an engine's
    header moves it, and the engine stops matching what the builder shipped.
    Nothing detected that until 2026-07: validate_catalog recomputes digests on
    export and never compares them to anything, so eight shipped engines were
    invalidated by comment corrections without a single check failing.

    digests.json is the committed snapshot that makes it visible. A moved
    digest is not automatically wrong -- it is exactly what landing a real fix
    looks like -- but it MUST be deliberate, because it means the builder needs
    a rollout before the website can serve that engine.
    """
    if not DIGESTS_PATH.exists():
        return [], [], []
    recorded = json.loads(DIGESTS_PATH.read_text(encoding="utf-8"))
    current = current_digests(catalog)
    moved = sorted(k for k in recorded.keys() & current.keys()
                   if recorded[k] != current[k])
    added = sorted(current.keys() - recorded.keys())
    removed = sorted(recorded.keys() - current.keys())
    return moved, added, removed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-digests", action="store_true",
                        help="fail if any engine's digest drifted from digests.json")
    parser.add_argument("--snapshot-digests", action="store_true",
                        help="rewrite digests.json from the current sources")
    arguments = parser.parse_args()

    catalog = load_catalog()
    validate_catalog(catalog)
    print(f"catalog ok: {len(catalog['engines'])} immutable packages")

    if arguments.snapshot_digests:
        DIGESTS_PATH.write_text(
            json.dumps(current_digests(catalog), indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        print(f"digests snapshotted: {DIGESTS_PATH.name}")
        return

    if arguments.check_digests:
        moved, added, removed = digest_drift(catalog)
        if not DIGESTS_PATH.exists():
            raise SystemExit(f"{DIGESTS_PATH.name} is missing; run --snapshot-digests")
        for engine_id in moved:
            print(f"  DRIFT  {engine_id}: digest moved -- needs a builder rollout")
        for engine_id in added:
            print(f"  new    {engine_id}")
        for engine_id in removed:
            print(f"  gone   {engine_id}")
        if moved:
            raise SystemExit(
                f"{len(moved)} shipped engine(s) no longer match digests.json. "
                "If the change was deliberate, re-run with --snapshot-digests "
                "and roll the builder.")
        print("digests match the snapshot")


if __name__ == "__main__":
    main()
