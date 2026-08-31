#!/usr/bin/env python3
"""Network-isolated HTTP wrapper around the pinned Plaits ARM build."""

from __future__ import annotations

import base64
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import wave
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Literal

from generate_engine_config import render_config, validate_recipe
from render_manual import manual_document, render_pdf
from natural_speech_banks import (
    render_natural_speech_config,
    validate_natural_speech_banks,
)
from speech_banks import (
    MAX_FRAMES,
    MAX_WORDS,
    render_speech_config,
    validate_speech_banks,
)
from user_data_regions import check_elf, render_linker_script


WORKSPACE = Path(os.environ.get("PLAITS_WORKSPACE", "/workspace")).resolve()
STOCK_LINKER_SCRIPT = (
    WORKSPACE / "stmlib/linker_scripts/stm32f373x_flash_application_large.ld")
_REGION_COUNT_RE = re.compile(r"^#define PLAITS_USER_DATA_REGION_COUNT (\d+)$", re.MULTILINE)


def _declared_region_count(config_text: str) -> int:
    """How many rewritable FM/model regions the generated config declares."""
    match = _REGION_COUNT_RE.search(config_text)
    return int(match.group(1)) if match else 0

BUILD_ROOT = Path(os.environ.get("PLAITS_BUILD_ROOT", "/tmp/plaits-builds")).resolve()
SOURCE_REVISION = os.environ.get("PLAITS_SOURCE_REVISION", "development")
BUILD_CONTRACT_VERSION = os.environ.get("PLAITS_BUILD_CONTRACT", "2")
# The manual contract is the WORKER's notion — it seeds the manual key, and the
# container has no independent way to know it, so /manual takes it per request
# and echoes it back. This env var is only the fallback for a caller that sends
# none (a pre-2026-07-27 Worker against a newer image). It used to be the sole
# source, which meant the header reported the image's default — "1" — while the
# Worker had long since moved on, so anyone reading it was told the wrong thing.
MANUAL_CONTRACT_FALLBACK = os.environ.get("PLAITS_MANUAL_CONTRACT", "1")
TOOLCHAIN_ID = "gcc-arm-none-eabi-4.8-2013q4"
TOOLCHAIN_BIN = os.environ.get("PLAITS_TOOLCHAIN_BIN", "/usr/local/arm-4.8.3/bin")
# The Worker has already normalized and structurally bounded the recipe before
# it reaches this private container. A schema-v12 recipe can carry one inline
# 32-voice FM bank per palette slot; those 7-bit bytes expand to decimal JSON,
# so the old 32 KiB transport cap rejected valid recipes long before the
# contract's 32-bank ceiling. The maximum normalized shape is under 1 MiB
# (including nine full chord tables), leaving this as a meaningful defence
# against malformed internal requests without contradicting the public schema.
MAX_REQUEST_BYTES = 1024 * 1024
MAX_BUILD_SECONDS = 12 * 60
MAX_SPEECH_JSON_BYTES = 64 * 1024
MAX_RECORDING_BYTES = 4 * 1024 * 1024
BUILD_KEY_PATTERN = re.compile(r"^[0-9a-f]{64}$")
# The contract is echoed into a response header, so it is held to a short
# printable token rather than trusted verbatim — no CRLF, no header injection.
MANUAL_CONTRACT_PATTERN = re.compile(r"^[0-9A-Za-z._-]{1,32}$")
FirmwareOutput = Literal["audio-wav", "intel-hex"]

# Route the ARM compiler through ccache when it is installed. Only the units in
# plaits/makefile's RECIPE_CONFIG_OBJS see the generated engine_config.h, so
# every other object hits the image-baked warm cache and a novel recipe
# recompiles that small set plus the link and WAV encode.
_CCACHE = shutil.which("ccache")


def _compiler(binary: str) -> str:
    path = f"{TOOLCHAIN_BIN}/{binary}"
    return f"{_CCACHE} {path}" if _CCACHE else path


def _arm_tool(binary: str) -> str:
    """Resolve a pinned container tool or the same binary from local PATH."""
    configured = Path(TOOLCHAIN_BIN) / binary
    if configured.is_file():
        return str(configured)
    return shutil.which(binary) or str(configured)


class BuildError(Exception):
    def __init__(self, code: str, message: str, detail: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.detail = detail


@dataclass
class BuildRecord:
    status: str = "building"
    artifact_path: Path | None = None
    output: FirmwareOutput = "audio-wav"
    metadata: dict[str, str] | None = None
    error: BuildError | None = None


BUILD_RECORDS: dict[str, BuildRecord] = {}
BUILD_RECORDS_LOCK = threading.Lock()
SPEECH_LOCK = threading.Lock()
SPEECH_ROOT = Path("/tmp/plaits-speech")
SPEECH_SCRIPTS = Path(__file__).resolve().parent
LPC_FRAME = struct.Struct("<BBhh8b")
VOICE_CATALOG = {
    # Kokoro voices have no hyphen in their id; Piper voices always do, which
    # is how preview_artifacts routes a request to an engine. Keep that true
    # when adding voices — the two id spaces must stay disjoint.
    #
    # The thirteen languages below Kokoro cannot speak at all. Every Piper
    # voice here was chosen by ear against a reference voice; see
    # plaits_piper_voice_expansion in project memory for what was rejected and
    # why the obvious automated check (ASR round-tripping) does not work.
    #
    # NOTE: the preview sentences for the thirteen new languages are
    # translations that have NOT been checked by a native speaker. They are
    # user-facing. Verify before launch.

    "en-US": ({"af_bella", "af_heart", "af_nicole", "am_fenrir", "am_michael", "en_US-bryce-medium", "en_US-joe-medium", "en_US-john-medium", "en_US-kristin-medium", "en_US-ljspeech-medium", "en_US-mike-medium", "en_US-sam-medium"}, "The singing voice approached rapidly."),
    "en-GB": ({"bf_emma", "bm_fable", "bm_george", "en_GB-cori-high", "en_GB-cori-medium"}, "The singing voice approached rapidly."),
    "es": ({"ef_dora", "em_alex", "es_ES-davefx-medium", "es_MX-claude-high"}, "La voz que cantaba se acercó rápidamente."),
    "fr-FR": ({"ff_siwis"}, "La voix chantante s'est approchée rapidement."),
    "hi": ({"hf_alpha", "hm_omega"}, "गाने वाली आवाज़ तेज़ी से पास आई।"),
    "it": ({"if_sara", "im_nicola"}, "La voce che cantava si avvicinò rapidamente."),
    "pt-BR": ({"pf_dora", "pm_alex", "pt_BR-cadu-medium", "pt_BR-faber-medium"}, "A voz que cantava se aproximou rapidamente."),
    "ja": ({"jf_alpha", "jf_gongitsune", "jf_nezumi", "jf_tebukuro", "jm_kumo"}, "歌声が急速に近づいてきました。"),
    "zh": ({"zf_xiaobei", "zf_xiaoni", "zf_xiaoxiao", "zf_xiaoyi", "zm_yunjian", "zm_yunxi", "zm_yunxia", "zm_yunyang"}, "歌声迅速地靠近了。"),

    # Piper only, from here down.
    "bg": ({"bg_BG-dimitar-medium"}, "Пеещият глас се приближи бързо."),
    "cs": ({"cs_CZ-jirka-medium"}, "Zpívající hlas se rychle přiblížil."),
    "da": ({"da_DK-talesyntese-medium"}, "Den syngende stemme nærmede sig hurtigt."),
    "de": ({"de_DE-thorsten-medium"}, "Die singende Stimme näherte sich schnell."),
    "hu": ({"hu_HU-anna-medium", "hu_HU-imre-medium"}, "Az éneklő hang gyorsan közeledett."),
    "nl": ({"nl_BE-nathalie-medium", "nl_NL-alex-medium", "nl_NL-pim-medium"}, "De zingende stem kwam snel dichterbij."),
    "no": ({"no_NO-nvcc-medium__KMN", "no_NO-nvcc-medium__KNV", "no_NO-nvcc-medium__MMN", "no_NO-nvcc-medium__MNV"}, "Den syngende stemmen nærmet seg raskt."),
    "pl": ({"pl_PL-gosia-medium", "pl_PL-mc_speech-medium"}, "Śpiewający głos szybko się zbliżał."),
    "ro": ({"ro_RO-mihai-medium"}, "Vocea care cânta s-a apropiat rapid."),
    "ru": ({"ru_RU-denis-medium", "ru_RU-dmitri-medium"}, "Поющий голос быстро приблизился."),
    "sk": ({"sk_SK-lili-medium"}, "Spievajúci hlas sa rýchlo priblížil."),
    "sv": ({"sv_SE-nst-medium"}, "Den sjungande rösten närmade sig snabbt."),
    "uk": ({"uk_UA-mykyta-high", "uk_UA-oleksa-high", "uk_UA-tetiana-high"}, "Співочий голос швидко наближався."),
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def redact_log(value: str) -> str:
    redacted = value.replace(str(WORKSPACE), "<workspace>").replace(str(BUILD_ROOT), "<build-root>")
    return redacted[-8000:]


def request_size_is_valid(content_length: int) -> bool:
    return 0 < content_length <= MAX_REQUEST_BYTES


def _speech_job_key(value: bytes, revision: str) -> str:
    return hashlib.sha256(revision.encode("ascii") + b"\0" + value).hexdigest()[:24]


def _run_speech_script(name: str, arguments: list[str]) -> None:
    result = subprocess.run(
        [sys.executable, str(SPEECH_SCRIPTS / name), *arguments],
        cwd=WORKSPACE,
        capture_output=True,
        text=True,
        timeout=4 * 60,
    )
    if result.returncode:
        raise BuildError("speech_encoding_failed", "The Speech bank could not be encoded.", redact_log(result.stderr))


def _data_audio(path: Path) -> str:
    return "data:audio/wav;base64," + base64.b64encode(path.read_bytes()).decode("ascii")


def _pack_plans(paths: list[Path]) -> tuple[list[int], str]:
    packed = bytearray()
    boundaries = [0]
    for path in paths:
        frames = []
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            values = [int(value) for value in line.split()]
            if len(values) != 12:
                raise BuildError("speech_encoding_failed", "The encoder produced an invalid LPC frame.")
            frames.append(values)
        if not frames:
            raise BuildError("speech_encoding_failed", "The encoder produced an empty word.")
        frames.append(frames[-1][:])
        for frame in frames:
            packed.extend(LPC_FRAME.pack(*frame))
        boundaries.append(boundaries[-1] + len(frames))
    if boundaries[-1] > MAX_FRAMES:
        raise BuildError(
            "speech_bank_too_large",
            f"This bank needs {boundaries[-1]} of {MAX_FRAMES} frames. Use fewer or shorter words.",
        )
    return boundaries, base64.b64encode(packed).decode("ascii")


LPC_WORD_BANK_FORMAT = "rubato.plaits-lpc-word-bank/v1"
NATURAL_SPEECH_WORD_BANK_FORMAT = "rubato.plaits-natural-speech-word-bank/v1"


def _validate_speech_request(
    value: Any,
    word_bank_format: str = LPC_WORD_BANK_FORMAT,
) -> dict[str, Any]:
    """Validate a word-bank request for either engine family.

    The two formats differ only in how frames are encoded downstream -- the
    words, the voice and the pronunciation hints are the same request, so this
    is parameterised rather than forked.
    """
    if not isinstance(value, dict) or value.get("format") != word_bank_format:
        raise BuildError("invalid_request", "The Speech bank request is invalid.")
    language = value.get("language")
    synthesis = value.get("synthesis")
    voice = synthesis.get("voice") if isinstance(synthesis, dict) else None
    if language not in VOICE_CATALOG or voice not in VOICE_CATALOG[language][0]:
        raise BuildError("invalid_request", "Choose a supported voice for the selected language.")
    entries = value.get("entries")
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_WORDS:
        raise BuildError(
            "invalid_request",
            f"A Speech bank must contain between 1 and {MAX_WORDS} words.",
        )
    normalized_entries = []
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise BuildError("invalid_request", f"Word {index + 1} is invalid.")
        word = entry.get("word")
        spoken_as = entry.get("spokenAs", "")
        if not isinstance(word, str) or not word.strip() or len(word) > 80:
            raise BuildError("invalid_request", f"Word {index + 1} is missing or too long.")
        if not isinstance(spoken_as, str) or len(spoken_as) > 80:
            raise BuildError("invalid_request", f"Pronunciation hint {index + 1} is invalid.")
        normalized_entries.append({"word": word.strip(), **({"spokenAs": spoken_as.strip()} if spoken_as.strip() else {})})
    return {
        "format": word_bank_format,
        "name": str(value.get("name", "Custom words"))[:80],
        "language": language,
        "entries": normalized_entries,
        "synthesis": {"voice": voice, "pitchContour": "flat-to-natural", "referencePitchHz": 100},
    }


def encode_speech_bank(value: Any) -> dict[str, Any]:
    request = _validate_speech_request(value)
    encoded = json.dumps(request, sort_keys=True, ensure_ascii=False).encode("utf-8")
    job_id = _speech_job_key(encoded, "continuous-kokoro-production-v1")
    output = SPEECH_ROOT / "banks" / job_id
    manifest_path = output / "manifest.json"
    cached = manifest_path.is_file()
    if not cached:
        with SPEECH_LOCK:
            cached = manifest_path.is_file()
            if not cached:
                output.mkdir(parents=True, exist_ok=True)
                request_path = output / "request.json"
                request_path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
                _run_speech_script("encode_word_bank.py", [
                    "--repo", str(WORKSPACE),
                    "--request", str(request_path),
                    "--output-dir", str(output),
                    "--artifact-cache", str(SPEECH_ROOT / "artifacts"),
                ])
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = []
    plans = []
    for entry in manifest["entries"]:
        index = int(entry["index"])
        prefix = f"entry-{index:02d}"
        plans.append(output / f"{prefix}.plan")
        entries.append({
            key: entry[key] for key in ("index", "word", "spokenAs", "frames", "durationSeconds", "frameBytes", "sourceF0Median")
        } | {"audio": {mode: _data_audio(output / f"{prefix}-{mode}.wav") for mode in ("source", "natural", "flat")}})
    boundaries, frame_data = _pack_plans(plans)
    return {
        "jobId": job_id,
        "cached": cached,
        "bankAudio": {mode: _data_audio(output / manifest["bankFiles"][mode]) for mode in ("natural", "flat")},
        "entries": entries,
        "totals": manifest["totals"],
        "synthesis": manifest["synthesis"],
        "cache": manifest.get("cache"),
        "wordBoundaries": boundaries,
        "frameData": frame_data,
    }


def encode_natural_speech_bank(value: Any) -> dict[str, Any]:
    """Encode a Natural Speech word bank and render its previews.

    The same cache-by-content-key shape as encode_speech_bank, with a distinct
    revision tag so the two engines' jobs can never collide on one key. The
    driver returns the recipe bank ready for validation -- it validates its own
    output against the recipe contract before writing the manifest, so a bank
    that would fail at build time fails here instead, while the user can still
    do something about it.
    """
    request = _validate_speech_request(value, NATURAL_SPEECH_WORD_BANK_FORMAT)
    encoded = json.dumps(request, sort_keys=True, ensure_ascii=False).encode("utf-8")
    job_id = _speech_job_key(encoded, "natural-speech-nsh1-v1")
    output = SPEECH_ROOT / "natural-banks" / job_id
    manifest_path = output / "manifest.json"
    cached = manifest_path.is_file()
    if not cached:
        with SPEECH_LOCK:
            cached = manifest_path.is_file()
            if not cached:
                output.mkdir(parents=True, exist_ok=True)
                request_path = output / "request.json"
                request_path.write_text(
                    json.dumps(request, ensure_ascii=False), encoding="utf-8")
                _run_speech_script("encode_natural_speech_bank.py", [
                    "--repo", str(WORKSPACE),
                    "--request", str(request_path),
                    "--output-dir", str(output),
                    "--artifact-cache", str(SPEECH_ROOT / "artifacts"),
                ])
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = []
    for entry in manifest["entries"]:
        files = entry["files"]
        entries.append({
            key: entry[key] for key in (
                "index", "word", "spokenAs", "frames", "frameBytes",
                "durationSeconds")
        } | {"audio": {mode: _data_audio(output / files[mode])
                       for mode in ("source", "natural", "flat")}})
    bank = manifest["bank"]
    return {
        "jobId": job_id,
        "cached": cached,
        "bankAudio": {mode: _data_audio(output / manifest["bankFiles"][mode])
                      for mode in ("natural", "flat")},
        "entries": entries,
        "totals": manifest["totals"],
        "synthesis": manifest["synthesis"],
        "cache": manifest.get("cache"),
        "words": bank["words"],
        "wordBoundaries": bank["wordBoundaries"],
        "frameData": bank["frameData"],
    }


def _validate_saved_bank_preview_request(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != {"format", "bank"} \
            or value.get("format") != "rubato.plaits-lpc-bank-preview/v1":
        raise BuildError("invalid_request", "The saved Speech preview request is invalid.")
    try:
        normalized = validate_speech_banks({"stockBankIds": [], "customBanks": [value["bank"]]})
    except (KeyError, ValueError, TypeError) as error:
        raise BuildError("invalid_request", "The saved Speech bank is invalid.", str(error)) from error
    return normalized["customBanks"][0]


NATURAL_SPEECH_BANK_PREVIEW_FORMAT = "rubato.plaits-natural-speech-bank-preview/v1"


def render_saved_natural_speech_bank(value: Any) -> dict[str, Any]:
    """Preview a SAVED Natural Speech bank, straight from its frames.

    The encode endpoint returns preview audio with the bank it just made, but a
    bank that arrives from a saved configuration -- or from the editor's seeded
    starter banks -- has no encode session behind it, so its previews have to be
    rendered from the stored frames alone. That path is LPC-only otherwise, and
    a 23-byte NSH1 bank fails its validator as malformed rather than
    unsupported ("The saved Speech bank is invalid").

    Cheaper than encoding: no TTS and no analysis, just the config header and
    one compile of the same preview harness the encoder uses.
    """
    if (not isinstance(value, dict) or set(value) != {"format", "bank"}
            or value.get("format") != NATURAL_SPEECH_BANK_PREVIEW_FORMAT):
        raise BuildError("invalid_request", "The saved Natural Speech preview request is invalid.")
    try:
        normalized = validate_natural_speech_banks({"customBanks": [value["bank"]]})
    except (KeyError, ValueError, TypeError) as error:
        raise BuildError("invalid_request", "The saved Natural Speech bank is invalid.", str(error)) from error

    encoded = json.dumps(value, sort_keys=True, ensure_ascii=False).encode("utf-8")
    job_id = _speech_job_key(encoded, "saved-natural-speech-bank-preview-v1")
    output = SPEECH_ROOT / "saved-natural-banks" / job_id
    targets = {mode: output / f"bank-{mode}.wav" for mode in ("natural", "flat")}
    cached = all(path.is_file() for path in targets.values())
    if not cached:
        with SPEECH_LOCK:
            cached = all(path.is_file() for path in targets.values())
            if not cached:
                output.mkdir(parents=True, exist_ok=True)
                request_path = output / "bank.json"
                request_path.write_text(
                    json.dumps(normalized, ensure_ascii=False), encoding="utf-8")
                _run_speech_script("render_natural_speech_bank.py", [
                    "--repo", str(WORKSPACE),
                    "--bank", str(request_path),
                    "--output-dir", str(output),
                ])
    return {
        "jobId": job_id,
        "cached": cached,
        "bankAudio": {mode: _data_audio(target) for mode, target in targets.items()},
    }


def _write_saved_plan(path: Path, frames: list[tuple[int, ...]]) -> None:
    # Banks produced by _pack_plans carry a duplicated final frame as the
    # firmware's interpolation guard. It was not part of the original listening
    # preview, so omit it when the duplicate is present.
    audible = frames[:-1] if len(frames) > 1 and frames[-1] == frames[-2] else frames
    path.write_text(
        "".join(" ".join(str(value) for value in frame) + "\n" for frame in audible),
        encoding="utf-8",
    )


def _concatenate_pcm_wavs(sources: list[Path], target: Path) -> None:
    parameters = None
    chunks = []
    for source_path in sources:
        with wave.open(str(source_path), "rb") as source:
            source_parameters = source.getparams()
            comparable = (
                source_parameters.nchannels,
                source_parameters.sampwidth,
                source_parameters.framerate,
                source_parameters.comptype,
            )
            if parameters is None:
                parameters = comparable
            elif comparable != parameters:
                raise BuildError("speech_encoding_failed", "The LPC renderer returned incompatible audio.")
            chunks.append(source.readframes(source.getnframes()))
    if parameters is None:
        raise BuildError("speech_encoding_failed", "The LPC renderer returned no audio.")
    channels, sample_width, sample_rate, _compression = parameters
    with wave.open(str(target), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(sample_width)
        output.setframerate(sample_rate)
        output.writeframes(b"".join(chunks))


def render_saved_speech_bank(value: Any) -> dict[str, Any]:
    bank = _validate_saved_bank_preview_request(value)
    encoded = json.dumps(value, sort_keys=True, ensure_ascii=False).encode("utf-8")
    job_id = _speech_job_key(encoded, "saved-lpc-bank-preview-v1")
    output = SPEECH_ROOT / "saved-banks" / job_id
    targets = {mode: output / f"bank-{mode}.wav" for mode in ("natural", "flat")}
    cached = all(path.is_file() for path in targets.values())
    if not cached:
        with SPEECH_LOCK:
            cached = all(path.is_file() for path in targets.values())
            if not cached:
                output.mkdir(parents=True, exist_ok=True)
                plans = []
                boundaries = bank["wordBoundaries"]
                for index in range(len(bank["words"])):
                    plan = output / f"word-{index:02d}.plan"
                    _write_saved_plan(plan, bank["frames"][boundaries[index]:boundaries[index + 1]])
                    plans.append(plan)
                for mode, prosody in (("natural", "1.0"), ("flat", "0.0")):
                    chunks = []
                    for index, plan in enumerate(plans):
                        chunk = output / f"word-{index:02d}-{mode}.wav"
                        subprocess.run([
                            "/usr/local/bin/plaits-render-lpc-bank", str(chunk), prosody, str(plan),
                        ], check=True, capture_output=True, text=True, timeout=60)
                        chunks.append(chunk)
                    _concatenate_pcm_wavs(chunks, targets[mode])
    return {
        "jobId": job_id,
        "cached": cached,
        "bankAudio": {mode: _data_audio(target) for mode, target in targets.items()},
    }


def encode_natural_speech_recording(audio: bytes) -> dict[str, Any]:
    """A recorded bank for Natural Speech, segmented the same way as the LPC one.

    Its own job-key revision so a recording encoded for one engine can never be
    served from the other's cache: the bodies are identical bytes, and only the
    endpoint says which format the caller wants back.
    """
    job_id = _speech_job_key(audio, "paused-recording-natural-speech-v1")
    with tempfile.TemporaryDirectory(prefix="natural-speech-recording-") as temporary:
        output = Path(temporary)
        source = output / "recording.wav"
        source.write_bytes(audio)
        _run_speech_script("encode_natural_speech_recording.py", [
            "--repo", str(WORKSPACE), "--source", str(source), "--output-dir", str(output),
        ])
        manifest = json.loads((output / "recording-manifest.json").read_text(encoding="utf-8"))
        entries = []
        for entry in manifest["entries"]:
            files = entry["files"]
            entries.append({
                key: entry[key] for key in (
                    "index", "frames", "guardFrames", "frameBytes", "durationSeconds")
            } | {"audio": {mode: _data_audio(output / files[mode])
                           for mode in ("source", "natural", "flat")}})
        bank = manifest["bank"]
        return {
            "jobId": job_id,
            "cached": False,
            "entries": entries,
            "bankAudio": {mode: _data_audio(output / manifest["bankFiles"][mode])
                          for mode in ("natural", "flat")},
            "totals": manifest["totals"],
            "segmentation": manifest["segmentation"],
            "normalization": manifest["normalization"],
            "sampleRate": manifest["sampleRate"],
            "wordBoundaries": bank["wordBoundaries"],
            "frameData": bank["frameData"],
        }


def encode_recording(audio: bytes) -> dict[str, Any]:
    job_id = _speech_job_key(audio, "paused-recording-production-v1")
    with tempfile.TemporaryDirectory(prefix="speech-recording-") as temporary:
        output = Path(temporary)
        source = output / "recording.wav"
        source.write_bytes(audio)
        _run_speech_script("encode_recording.py", [
            "--repo", str(WORKSPACE), "--source", str(source), "--output-dir", str(output),
        ])
        manifest = json.loads((output / "recording-manifest.json").read_text(encoding="utf-8"))
        entries = []
        plans = []
        for entry in manifest["entries"]:
            index = int(entry["index"])
            prefix = f"recording-word-{index:02d}"
            plans.append(output / f"{prefix}.plan")
            entries.append({
                key: entry[key] for key in ("index", "frames", "guardFrames", "frameBytes", "durationSeconds", "sourceF0Median")
            } | {"audio": {mode: _data_audio(output / f"{prefix}-{mode}.wav") for mode in ("source", "natural", "flat")}})
        boundaries, frame_data = _pack_plans(plans)
        return {
            "jobId": job_id,
            "cached": False,
            "entries": entries,
            "bankAudio": {mode: _data_audio(output / manifest["bankFiles"][mode]) for mode in ("source", "natural", "flat")},
            "totals": manifest["totals"],
            "segmentation": manifest["segmentation"],
            "normalization": manifest.get("normalization"),
            "sampleRate": manifest["sampleRate"],
            "wordBoundaries": boundaries,
            "frameData": frame_data,
        }


def voice_preview(language: str, voice: str) -> Path:
    if language not in VOICE_CATALOG or voice not in VOICE_CATALOG[language][0]:
        raise BuildError("invalid_request", "That Speech voice is not supported.")
    text = VOICE_CATALOG[language][1]
    job_id = _speech_job_key(f"{language}\0{voice}\0{text}".encode("utf-8"), "voice-preview-production-v1")
    output = SPEECH_ROOT / "voices" / job_id
    target = output / "voice-preview-source.wav"
    if not target.is_file():
        with SPEECH_LOCK:
            if not target.is_file():
                _run_speech_script("render_voice_preview.py", [
                    "--language", language, "--voice", voice, "--text", text,
                    "--output-dir", str(output), "--artifact-cache", str(SPEECH_ROOT / "artifacts"),
                ])
    return target


def stock_preview(bank: int, mode: str) -> Path:
    if bank < 0 or bank > 4 or mode not in {"natural", "flat"}:
        raise BuildError("invalid_request", "That stock Speech preview is invalid.")
    target = SPEECH_ROOT / "stock" / f"bank-{bank + 1}-{mode}.wav"
    if not target.is_file():
        with SPEECH_LOCK:
            if not target.is_file():
                target.parent.mkdir(parents=True, exist_ok=True)
                subprocess.run([
                    "/usr/local/bin/plaits-render-stock-speech-bank", str(bank), str(target),
                    "1.0" if mode == "natural" else "0.0",
                ], check=True, timeout=60)
    return target


def parse_size(elf_path: Path) -> tuple[int, int, int]:
    result = subprocess.run(
        [_arm_tool("arm-none-eabi-size"), str(elf_path)],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    fields = result.stdout.strip().splitlines()[-1].split()
    if len(fields) < 3:
        raise BuildError("invalid_artifact", "The compiler returned unreadable size metadata.")
    return int(fields[0]), int(fields[1]), int(fields[2])


# Where the bootloader starts writing, and so where the application image
# begins (stmlib/makefile.inc BASE_ADDRESS, plaits/bootloader kStartAddress).
APPLICATION_BASE_ADDRESS = 0x08008000


def _linked_flash_span(elf_path: Path, data_bytes: int) -> int:
    """Bytes of flash the linked image actually occupies, padding included.

    `size` reports the sum of SECTION sizes, which silently omits alignment
    padding between them — and page-aligning the swappable bank regions creates
    exactly that kind of gap. The gap is real: objcopy writes it into plaits.bin
    and the bootloader programs it.

    `data_bytes` is added because .data's INITIALIZERS live in flash at
    `_sidata = _etext` even though the section itself lives in RAM. GNU ld
    region-checks a section's VMA, not its LMA, so it will happily place that
    initializer image past the end of FLASH and link without complaint — which
    is precisely what a build whose bank regions reach the top of flash does.
    Measuring only up to _etext reports such a build as exactly at budget
    instead of over it, and the firmware would then copy .data from an address
    past the end of the chip at startup.
    """
    result = subprocess.run(
        [_arm_tool("arm-none-eabi-nm"), str(elf_path)],
        check=True, capture_output=True, text=True, timeout=30)
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] == "_etext":
            return int(fields[0], 16) - APPLICATION_BASE_ADDRESS + data_bytes
    return 0


# The application flash region (224 KB) and usable RAM budget (32 KB less a 1 KB
# stack reserve) the linked firmware must fit — kept in sync with the values the
# post-link size check and the stm32f373x_flash_application_large.ld linker
# script enforce.
FLASH_BUDGET_BYTES = 224 * 1024
RAM_BUDGET_BYTES = 32 * 1024
RAM_STACK_RESERVE_BYTES = 1024


def _validate_shared_buffer_layout(nm_output: str) -> int:
    """Reject a scratch arena that would fault on Cortex-M4 typed accesses."""
    symbols = re.findall(
        r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[Bb]\s+shared_buffer$",
        nm_output, re.MULTILINE,
    )
    if len(symbols) != 1:
        raise BuildError(
            "unsafe_ram_layout", "The linked firmware's audio scratch buffer "
            "could not be verified and must not be installed.",
        )
    address, size = (int(value, 16) for value in symbols[0])
    if (address % 8 != 0 or size != 16384
            or address < 0x20000000
            or address + size > 0x20000000 + RAM_BUDGET_BYTES):
        raise BuildError(
            "unsafe_ram_layout", "The linked firmware has an unsafe audio "
            "scratch-buffer layout and must not be installed.",
            f"shared_buffer at 0x{address:08x}, size {size}; expected an "
            "8-byte-aligned 16384-byte arena entirely within SRAM.",
        )
    return address


def _check_shared_buffer_alignment(elf_path: Path) -> int:
    result = subprocess.run(
        [_arm_tool("arm-none-eabi-nm"), "-S", "--defined-only", str(elf_path)],
        check=True, capture_output=True, text=True, timeout=30,
    )
    return _validate_shared_buffer_layout(result.stdout)


def validate_linked_firmware(elf_path: Path, config_text: str) -> dict[str, int]:
    """Apply the hosted builder's post-link memory and flash-layout gates."""
    if not elf_path.is_file():
        raise BuildError(
            "invalid_artifact",
            "The compiler did not produce the linked Plaits firmware.",
        )

    region_count = _declared_region_count(config_text)
    if region_count:
        try:
            check_elf(
                elf_path,
                region_count,
                objdump=_arm_tool("arm-none-eabi-objdump"),
                nm=_arm_tool("arm-none-eabi-nm"),
            )
        except ValueError as error:
            raise BuildError(
                "unsafe_flash_layout",
                "The linked firmware has an unsafe rewritable user-data layout "
                "and must not be installed.",
                str(error),
            ) from error

    text_bytes, data_bytes, bss_bytes = parse_size(elf_path)
    # `size` omits linker-inserted alignment padding. The linked span includes
    # that padding, while .data initializers consume flash as well as RAM.
    flash_bytes = max(
        text_bytes + data_bytes,
        _linked_flash_span(elf_path, data_bytes),
    )
    if flash_bytes > FLASH_BUDGET_BYTES:
        over = flash_bytes - FLASH_BUDGET_BYTES
        raise BuildError(
            "flash_budget_exceeded",
            f"This palette is too large for Plaits' flash memory by {over} bytes. "
            "Remove an engine, disable per-engine stereo, or drop a chord table, "
            "then build again.",
        )
    if bss_bytes + RAM_STACK_RESERVE_BYTES > RAM_BUDGET_BYTES:
        over = bss_bytes + RAM_STACK_RESERVE_BYTES - RAM_BUDGET_BYTES
        raise BuildError(
            "ram_budget_exceeded",
            f"This palette needs more RAM than Plaits has by {over} bytes. "
            "Remove a memory-heavy engine, then build again.",
        )

    shared_buffer_address = _check_shared_buffer_alignment(elf_path)
    return {
        "textBytes": text_bytes,
        "dataBytes": data_bytes,
        "bssBytes": bss_bytes,
        "flashBytes": flash_bytes,
        "userDataRegions": region_count,
        "sharedBufferAddress": shared_buffer_address,
        # Preserve the pre-schema-24 key for local tooling that has not yet
        # learned that these regions can also hold Terrain/Wavetable data.
        "replaceableFmBankRegions": region_count,
    }

# GNU ld reports an over-budget link as a region overflow. The overflow line
# ("region `FLASH' overflowed by N bytes") carries the exact overage; the
# section line ("... will not fit in region `FLASH'") is the fallback when the
# byte count is absent. Both name FLASH or RAM.
_REGION_OVERFLOW_RE = re.compile(r"region `(FLASH|RAM)' overflowed by (\d+) bytes")
_REGION_NOFIT_RE = re.compile(r"will not fit in region `(FLASH|RAM)'")


def classify_link_failure(log: str) -> tuple[str, str] | None:
    """Map a linker region overflow in `log` to a specific (code, message).

    A recipe that overruns the module's memory fails at the LINK step, not with
    a compiler error — so the user must be told they exceeded a size budget (and
    by how much) rather than handed the generic "did not produce a valid ARM
    build", which reads like an internal fault. FLASH is the common case (too
    many or too heavy engines, per-engine stereo, or extra chord tables); RAM
    can overflow the same way. Returns None when the failure is not a region
    overflow (a genuine compiler error), leaving it classified compiler_failed.
    """
    overflow = _REGION_OVERFLOW_RE.search(log)
    if overflow:
        region = overflow.group(1)
        by = f" by {int(overflow.group(2))} bytes"
    else:
        nofit = _REGION_NOFIT_RE.search(log)
        if not nofit:
            return None
        region, by = nofit.group(1), ""
    if region == "FLASH":
        return (
            "flash_budget_exceeded",
            f"This palette is too large for Plaits' flash memory{by}. Remove an "
            "engine, disable per-engine stereo, or drop a chord table, then "
            "build again.",
        )
    return (
        "ram_budget_exceeded",
        f"This palette needs more RAM than Plaits has{by}. Remove a "
        "memory-heavy engine, then build again.",
    )


# Catalog id -> the PLAITS_STEREO_<MACRO> that gates that engine's stereo render
# code (see plaits/dsp/engine/stereo_config.h and the makefile). The three DX7
# banks share the six-op engine, so they map to one macro. The remaining
# declare-only engines — the Braids ports whose out/aux is already a stereo pair
# at ~0 cost — are absent, and are never gated. Keep this in sync with
# PLAITS_STEREO_MODELS in plaits/makefile; test_container_server.py pins the two
# lists against each other.
STEREO_MACROS = {
    "toy": "TOY",
    "bowed": "BOWED",
    "csaw": "CSAW",
    "ring-mod": "RING_MOD",
    "vowel-fof": "VOWEL_FOF",
    "digital-modulation": "DIGITAL_MODULATION",
    "virtual-analog": "VIRTUAL_ANALOG",
    "virtual-analog-dual": "VIRTUAL_ANALOG_DUAL",
    "virtual-analog-crossfade": "VIRTUAL_ANALOG_CROSSFADE",
    "waveshaping": "WAVESHAPING",
    "two-op-fm": "TWO_OP_FM",
    "granular-formant": "GRANULAR_FORMANT",
    "harmonic": "HARMONIC",
    "wavetable": "WAVETABLE",
    "chords": "CHORDS",
    "speech": "SPEECH",
    "formant-speech": "FORMANT_SPEECH",
    "lpc-speech": "LPC_SPEECH",
    "swarm": "SWARM",
    "filtered-noise": "FILTERED_NOISE",
    "particle-noise": "PARTICLE_NOISE",
    "inharmonic-string": "INHARMONIC_STRING",
    "modal-resonator": "MODAL_RESONATOR",
    "analog-bass-drum": "ANALOG_BASS_DRUM",
    "analog-snare": "ANALOG_SNARE",
    "analog-hi-hat": "ANALOG_HI_HAT",
    "virtual-analog-vcf": "VIRTUAL_ANALOG_VCF",
    "wave-terrain": "WAVE_TERRAIN",
    "chiptune": "CHIPTUNE",
    "dx7-bank-a": "SIX_OP",
    "dx7-bank-b": "SIX_OP",
    "dx7-bank-c": "SIX_OP",
    "glisson": "GLISSON",
    "gendy": "GENDY",
    "scanned": "SCANNED",
    "loopback": "LOOPBACK",
    "lockstep": "LOCKSTEP",
    "tapfield": "TAPFIELD",
    "phase-weave": "PHASE_WEAVE",
    "sideband-bank": "SIDEBAND_BANK",
    "undertow": "UNDERTOW",
    "reed-pipe": "REED_PIPE",
    "phase-flock": "PHASE_FLOCK",
    "rulefield": "RULEFIELD",
    "clap": "CLAP",
    "analog-percussion": "ANALOG_PERCUSSION",
    "freshets-formant": "FRESHETS_FORMANT",
    "natural-speech": "NATURAL_SPEECH",
    "bubbletime": "BUBBLETIME",
    "zxphase48k": "ZXPHASE48K",
    "zxpulse48k": "ZXPULSE48K",
}
ALL_STEREO_MACROS = frozenset(STEREO_MACROS.values())


def _recipe_is_stereo(validated_recipe: Any) -> bool:
    """Whether a validated recipe asks for the stereo OUT/AUX render path.

    Value 1 is stereo in the firmware's aux-output numbering (plaits/dsp/voice.h).
    This used to be spelled `== 3` inline at the call site — stereo's value before
    the options reorder moved it to 1 — so stereo recipes compiled every engine
    mono and sine-subosc recipes paid ~26 KB of flash for a path they could not
    reach. Named and pinned by a test now, since nothing else notices the drift.
    """
    return validated_recipe.aux_output_option == 1


def _stereo_disable_flags(aux_stereo: bool, stereo_engines: Any) -> list[str]:
    """PLAITS_STEREO_<MACRO>=0 make vars for every engine left in mono."""
    if not aux_stereo:
        enabled = frozenset()
    elif stereo_engines is None:
        enabled = ALL_STEREO_MACROS  # schema <= 9: global-stereo back-compat
    else:
        enabled = frozenset(
            STEREO_MACROS[e] for e in stereo_engines if e in STEREO_MACROS
        )
    return [f"PLAITS_STEREO_{macro}=0" for macro in sorted(ALL_STEREO_MACROS - enabled)]


def _build_targets(output: FirmwareOutput) -> list[str]:
    # `hex` is the application-only objcopy target. Do not use a combo/upload
    # target here: the advanced download must never add or overwrite a
    # bootloader. `bin` is also needed for the parity digest both formats report.
    return ["wav"] if output == "audio-wav" else ["bin", "hex"]


def _natural_speech_object() -> str:
    """Object file the Natural Speech engine compiles to.

    The name follows the registered package's source filename, so it is read
    from the catalog rather than assumed -- and a recipe cannot carry banks
    for an engine that is not in the build.
    """
    from generate_engine_config import CATALOG

    entry = CATALOG.get("natural-speech")
    if entry is None:
        raise ValueError(
            "naturalSpeechBanks requires the natural-speech engine to be "
            "registered in the catalog")
    return entry.member.rstrip("_") + ".o"


def build_firmware(payload: Any) -> tuple[Path, FirmwareOutput, dict[str, str]]:
    if not isinstance(payload, dict):
        raise BuildError("invalid_request", "The build request must be a JSON object.")
    build_key = payload.get("buildKey")
    recipe = payload.get("recipe")
    if not isinstance(build_key, str) or not BUILD_KEY_PATTERN.fullmatch(build_key):
        raise BuildError("invalid_request", "The build key is invalid.")

    try:
        validated_recipe = validate_recipe(recipe)
    except ValueError as error:
        raise BuildError("invalid_recipe", str(error)) from error
    output: FirmwareOutput = recipe["output"]

    build_dir = BUILD_ROOT / build_key
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=False)
    config_path = build_dir / "engine_config.h"
    speech_config_path = build_dir / "speech_config.h"
    natural_speech_config_path = build_dir / "natural_speech_config.h"
    recipe_path = build_dir / "recipe.json"
    config_text = render_config(validated_recipe)
    config_path.write_text(config_text, encoding="utf-8")
    speech_config_path.write_text(
        render_speech_config(validated_recipe.speech_banks), encoding="utf-8")
    natural_speech_config_path.write_text(
        render_natural_speech_config(validated_recipe.natural_speech_banks),
        encoding="utf-8")
    recipe_path.write_text(json.dumps(recipe, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")

    # Rewritable FM banks and custom Terrain/Wavetable resources need their own
    # page-aligned output section, which means a linker script this build owns.
    # stmlib is an UPSTREAM submodule
    # (pichenettes/stmlib) and is never edited: the stock script is read, the
    # section spliced in, and the result passed with LINKER_SCRIPT. A build with
    # FM banks locked and no customized model slots passes nothing and links
    # against the stock script byte-for-byte, exactly as before.
    region_count = _declared_region_count(config_text)
    linker_script_path: Path | None = None
    if region_count:
        linker_script_path = build_dir / "plaits_user_data.ld"
        linker_script_path.write_text(
            render_linker_script(STOCK_LINKER_SCRIPT.read_text(encoding="utf-8")),
            encoding="utf-8")

    command = [
        "make",
        "-f",
        "plaits/makefile",
        f"BUILD_ROOT={build_dir}/",
        f"ENGINE_CONFIG={config_path}",
        f"SPEECH_CONFIG={speech_config_path}",
        f"SPEECH_BANKS_ENABLED={1 if validated_recipe.speech_banks is not None else 0}",
        *(
            [
                f"NATURAL_SPEECH_CONFIG={natural_speech_config_path}",
                f"NATURAL_SPEECH_OBJ={_natural_speech_object()}",
            ]
            if validated_recipe.natural_speech_banks is not None
            else []
        ),
        f"CC={_compiler('arm-none-eabi-gcc')}",
        f"CXX={_compiler('arm-none-eabi-g++')}",
        # Every build gets a fresh BUILD_ROOT, so the per-object .d files are
        # never consulted for an incremental rebuild — suppress them to skip a
        # full-tree preprocessor pass. Empty DEPS makes the depends.mk recipe a
        # bare `cat`, which reads the closed stdin below and writes nothing.
        "DEPS=",
        "-j4",
    ]
    if linker_script_path is not None:
        # A command-line variable beats makefile.inc's own assignment, so this
        # overrides the stock script without touching the stmlib submodule.
        command.append(f"LINKER_SCRIPT={linker_script_path}")
    # Both formats come from the same application ELF. WAV encodes its raw
    # binary for Plaits' audio bootloader; Intel HEX preserves the linked
    # application addresses for a direct programmer. The HEX deliberately does
    # not merge in the bootloader.
    command.extend(_build_targets(output))
    # Per-engine stereo. The stereo render path (OUT/AUX as an L/R pair) costs
    # flash per engine, so it is compiled only for the engines a recipe enables.
    # The makefile turns each PLAITS_STEREO_<MACRO>=0 make var into a -D on that
    # engine's object alone, so each engine caches as two variants and the
    # default (all-stereo) build's warm ccache is untouched. Enabled set:
    #   - aux != stereo: nothing is stereo -> disable every engine.
    #   - aux == stereo, stereo_engines is None (schema <= 9): the global-stereo
    #     back-compat case -> enable all.
    #   - aux == stereo, stereo_engines given (schema 10+): enable exactly those.
    command.extend(_stereo_disable_flags(
        _recipe_is_stereo(validated_recipe),
        validated_recipe.stereo_engines,
    ))
    # Pin the full locale, including LC_CTYPE: ccache folds these into its hash,
    # and Python's PEP 538 C-locale coercion otherwise injects LC_CTYPE=C.UTF-8
    # here, which would never match the image's shell-built warm cache (LC_CTYPE=C)
    # and would miss every cached object.
    environment = {
        **os.environ,
        "LC_ALL": "C",
        "LANG": "C",
        "LC_CTYPE": "C",
        "HOME": str(build_dir),
    }
    try:
        result = subprocess.run(
            command,
            cwd=WORKSPACE,
            env=environment,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=MAX_BUILD_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        raise BuildError("build_timeout", "The firmware build exceeded its time limit.") from error

    log = redact_log(result.stdout + "\n" + result.stderr)
    if result.returncode != 0:
        classified = classify_link_failure(log)
        if classified is not None:
            code, message = classified
        else:
            code, message = (
                "compiler_failed",
                "The firmware recipe did not compile. This is usually a problem "
                "on our end rather than your palette — please try again, and "
                "report it if it persists.",
            )
        raise BuildError(code, message, log)

    artifact_dir = build_dir / "plaits"
    bin_path = artifact_dir / "plaits.bin"
    elf_path = artifact_dir / "plaits.elf"
    artifact_path = artifact_dir / ("plaits.wav" if output == "audio-wav" else "plaits.hex")
    if not artifact_path.is_file() or not bin_path.is_file() or not elf_path.is_file():
        raise BuildError("invalid_artifact", "The compiler did not produce every required firmware artifact.", log)

    try:
        safety = validate_linked_firmware(elf_path, config_text)
    except BuildError as error:
        if error.code == "unsafe_flash_layout":
            raise BuildError(
                "internal_error",
                "The firmware build produced an unsafe flash layout. This is a "
                "fault on our end rather than your palette — please report it.",
                f"{log}\nuser-data region check: {error.detail}",
            ) from error
        error.detail = f"{log}\n{error.detail}".rstrip()
        raise

    text_bytes = safety["textBytes"]
    data_bytes = safety["dataBytes"]
    bss_bytes = safety["bssBytes"]

    metadata = {
        "X-Plaits-Binary-Sha256": sha256_file(bin_path),
        "X-Plaits-Text-Bytes": str(text_bytes),
        "X-Plaits-Data-Bytes": str(data_bytes),
        "X-Plaits-Bss-Bytes": str(bss_bytes),
        "X-Plaits-Source-Revision": SOURCE_REVISION,
        "X-Plaits-Toolchain": TOOLCHAIN_ID,
        "X-Plaits-Build-Contract": BUILD_CONTRACT_VERSION,
    }
    metadata[
        "X-Plaits-Wav-Sha256" if output == "audio-wav" else "X-Plaits-Hex-Sha256"
    ] = sha256_file(artifact_path)
    return artifact_path, output, metadata


def render_manual_bytes(manual_key: str, recipe: Any) -> bytes:
    """Render the recipe's field-guide PDF and return its bytes.

    Deterministic for a given (renderer, catalog, recipe) — the Worker caches
    the result in R2 under the manual key, so a re-render must be byte-stable.
    """
    manual_dir = BUILD_ROOT / "manuals"
    manual_dir.mkdir(parents=True, exist_ok=True)
    output = manual_dir / f"{manual_key}.pdf"
    document = manual_document(recipe, manual_key)
    render_pdf(document, output)
    return output.read_bytes()


def run_build(build_key: str, payload: Any) -> None:
    try:
        artifact_path, output, metadata = build_firmware(payload)
        result = BuildRecord(
            status="succeeded",
            artifact_path=artifact_path,
            output=output,
            metadata=metadata,
        )
    except BuildError as error:
        result = BuildRecord(status="failed", error=error)
    except Exception:
        result = BuildRecord(
            status="failed",
            error=BuildError("internal_error", "The isolated compiler failed unexpectedly."),
        )
    with BUILD_RECORDS_LOCK:
        BUILD_RECORDS[build_key] = result


class Handler(BaseHTTPRequestHandler):
    server_version = "PlaitsLabBuilder/1"

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/ping":
            # Reports WHICH IMAGE is answering, not merely that something is.
            # A container rollout leaves the Worker on the new release while an
            # old container keeps serving, and nothing else distinguishes those
            # states cheaply: the only revision signal used to be the stamp on a
            # finished firmware, which costs a queued multi-minute compile. The
            # revision is already baked in (PLAITS_SOURCE_REVISION, defaulting
            # to the deliberate "development" sentinel), so this just reads it
            # back.
            self.send_json({
                "ok": True,
                "sourceRevision": SOURCE_REVISION,
                "buildContract": BUILD_CONTRACT_VERSION,
                "toolchain": TOOLCHAIN_ID,
            }, HTTPStatus.OK)
            return
        voice_match = re.fullmatch(r"/speech/voice-preview/([^/]+)/([^/]+)\.wav", self.path)
        stock_match = re.fullmatch(r"/speech/stock/bank-([1-5])-(natural|flat)\.wav", self.path)
        try:
            if voice_match:
                self.send_wav(voice_preview(voice_match.group(1), voice_match.group(2)))
                return
            if stock_match:
                self.send_wav(stock_preview(int(stock_match.group(1)) - 1, stock_match.group(2)))
                return
        except BuildError as error:
            self.send_json_error(error, HTTPStatus.BAD_REQUEST)
            return
        except Exception as error:  # noqa: BLE001
            print(f"builder: speech preview failed: {redact_log(str(error))}", flush=True)
            self.send_json_error(BuildError("speech_encoding_failed", "The Speech preview could not be rendered."), HTTPStatus.INTERNAL_SERVER_ERROR)
            return
        match = re.fullmatch(r"/build/([0-9a-f]{64})", self.path)
        if not match:
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        with BUILD_RECORDS_LOCK:
            record = BUILD_RECORDS.get(match.group(1))
        if record is None:
            self.send_json_error(BuildError("build_not_found", "That build is not running."), HTTPStatus.NOT_FOUND)
            return
        if record.status == "building":
            self.send_json({"status": "building"}, HTTPStatus.ACCEPTED)
            return
        if record.error is not None:
            status = HTTPStatus.BAD_REQUEST if record.error.code in {"invalid_request", "invalid_recipe"} else HTTPStatus.UNPROCESSABLE_ENTITY
            self.send_json_error(record.error, status)
            return
        if record.artifact_path is None or record.metadata is None:
            self.send_json_error(BuildError("internal_error", "The compiler result is incomplete."), HTTPStatus.INTERNAL_SERVER_ERROR)
            return
        self.send_artifact(record.artifact_path, record.output, record.metadata)

    def do_POST(self) -> None:  # noqa: N802
        if self.path == "/speech/segment":
            self.handle_speech_segment()
            return
        if self.path == "/speech/encode":
            self.handle_speech_encode()
            return
        if self.path == "/speech/encode-natural":
            self.handle_natural_speech_encode()
            return
        if self.path == "/speech/render-bank-natural":
            self.handle_natural_speech_render_bank()
            return
        if self.path == "/speech/render-bank":
            self.handle_speech_render_bank()
            return
        if self.path == "/speech/encode-recording-natural":
            self.handle_natural_speech_encode_recording()
            return
        if self.path == "/speech/encode-recording":
            self.handle_speech_recording()
            return
        if self.path == "/manual":
            self.handle_manual()
            return
        if self.path != "/build":
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            if not request_size_is_valid(content_length):
                raise BuildError("invalid_request", "The build request size is invalid.")
            payload = json.loads(self.rfile.read(content_length))
            build_key = payload.get("buildKey") if isinstance(payload, dict) else None
            if not isinstance(build_key, str) or not BUILD_KEY_PATTERN.fullmatch(build_key):
                raise BuildError("invalid_request", "The build key is invalid.")
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_json_error(BuildError("invalid_request", "The build request is not valid JSON."), HTTPStatus.BAD_REQUEST)
            return
        except BuildError as error:
            self.send_json_error(error, HTTPStatus.BAD_REQUEST)
            return

        with BUILD_RECORDS_LOCK:
            record = BUILD_RECORDS.get(build_key)
            if record is None:
                BUILD_RECORDS[build_key] = BuildRecord()
                threading.Thread(target=run_build, args=(build_key, payload), daemon=True).start()
        self.send_json({"status": "building"}, HTTPStatus.ACCEPTED)

    def handle_speech_segment(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_SPEECH_JSON_BYTES:
                raise BuildError("invalid_request", "The text-splitting request size is invalid.")
            value = json.loads(self.rfile.read(length))
            language = value.get("language") if isinstance(value, dict) else None
            source = value.get("text") if isinstance(value, dict) else None
            if language not in {"ja", "zh"} or not isinstance(source, str) or not source.strip() or len(source) > 2000:
                raise BuildError("invalid_request", "The text-splitting request is invalid.")
            result = subprocess.run(
                [sys.executable, str(SPEECH_SCRIPTS / "segment_text.py"), "--language", language, "--text", source.strip()],
                check=True, capture_output=True, text=True, timeout=30,
            )
            words = json.loads(result.stdout)["words"]
            self.send_json({"words": words[:16], "totalWords": len(words)}, HTTPStatus.OK)
        except (json.JSONDecodeError, UnicodeDecodeError, subprocess.SubprocessError) as error:
            self.send_json_error(BuildError("invalid_request", "The text could not be split.", redact_log(str(error))), HTTPStatus.BAD_REQUEST)
        except BuildError as error:
            self.send_json_error(error, HTTPStatus.BAD_REQUEST)

    def handle_speech_encode(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_SPEECH_JSON_BYTES:
                raise BuildError("invalid_request", "The Speech bank request size is invalid.")
            value = json.loads(self.rfile.read(length))
            self.send_json(encode_speech_bank(value), HTTPStatus.OK)
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_json_error(BuildError("invalid_request", "The Speech bank request is not valid JSON."), HTTPStatus.BAD_REQUEST)
        except BuildError as error:
            status = HTTPStatus.BAD_REQUEST if error.code == "invalid_request" else HTTPStatus.UNPROCESSABLE_ENTITY
            self.send_json_error(error, status)
        except Exception as error:  # noqa: BLE001
            print(f"builder: speech encoding failed: {redact_log(str(error))}", flush=True)
            self.send_json_error(BuildError("speech_encoding_failed", "The Speech bank could not be encoded."), HTTPStatus.INTERNAL_SERVER_ERROR)

    def handle_natural_speech_encode(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_SPEECH_JSON_BYTES:
                raise BuildError("invalid_request", "The Speech bank request size is invalid.")
            value = json.loads(self.rfile.read(length))
            self.send_json(encode_natural_speech_bank(value), HTTPStatus.OK)
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_json_error(BuildError("invalid_request", "The Speech bank request is not valid JSON."), HTTPStatus.BAD_REQUEST)
        except BuildError as error:
            status = HTTPStatus.BAD_REQUEST if error.code == "invalid_request" else HTTPStatus.UNPROCESSABLE_ENTITY
            self.send_json_error(error, status)
        except Exception as error:  # noqa: BLE001
            print(f"builder: natural speech encoding failed: {redact_log(str(error))}", flush=True)
            self.send_json_error(BuildError("speech_encoding_failed", "The Speech bank could not be encoded."), HTTPStatus.INTERNAL_SERVER_ERROR)

    def handle_natural_speech_render_bank(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_SPEECH_JSON_BYTES:
                raise BuildError("invalid_request", "The saved Natural Speech preview request size is invalid.")
            value = json.loads(self.rfile.read(length))
            self.send_json(render_saved_natural_speech_bank(value), HTTPStatus.OK)
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_json_error(BuildError("invalid_request", "The saved Natural Speech preview request is not valid JSON."), HTTPStatus.BAD_REQUEST)
        except BuildError as error:
            status = HTTPStatus.BAD_REQUEST if error.code == "invalid_request" else HTTPStatus.UNPROCESSABLE_ENTITY
            self.send_json_error(error, status)
        except Exception as error:  # noqa: BLE001
            print(f"builder: natural speech bank preview failed: {redact_log(str(error))}", flush=True)
            self.send_json_error(BuildError("speech_encoding_failed", "The saved Natural Speech bank could not be previewed."), HTTPStatus.INTERNAL_SERVER_ERROR)

    def handle_speech_render_bank(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_SPEECH_JSON_BYTES:
                raise BuildError("invalid_request", "The saved Speech preview request size is invalid.")
            value = json.loads(self.rfile.read(length))
            self.send_json(render_saved_speech_bank(value), HTTPStatus.OK)
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_json_error(BuildError("invalid_request", "The saved Speech preview request is not valid JSON."), HTTPStatus.BAD_REQUEST)
        except BuildError as error:
            status = HTTPStatus.BAD_REQUEST if error.code == "invalid_request" else HTTPStatus.UNPROCESSABLE_ENTITY
            self.send_json_error(error, status)
        except Exception as error:  # noqa: BLE001
            print(f"builder: saved speech preview failed: {redact_log(str(error))}", flush=True)
            self.send_json_error(BuildError("speech_encoding_failed", "The saved Speech bank preview could not be rendered."), HTTPStatus.INTERNAL_SERVER_ERROR)

    def handle_natural_speech_encode_recording(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_RECORDING_BYTES:
                raise BuildError("invalid_request", "Keep the recording to 15 seconds or less.")
            self.send_json(encode_natural_speech_recording(self.rfile.read(length)), HTTPStatus.OK)
        except BuildError as error:
            status = HTTPStatus.BAD_REQUEST if error.code == "invalid_request" else HTTPStatus.UNPROCESSABLE_ENTITY
            self.send_json_error(error, status)
        except Exception as error:  # noqa: BLE001
            print(f"builder: natural speech recording failed: {redact_log(str(error))}", flush=True)
            self.send_json_error(BuildError("speech_encoding_failed", "The recording could not be encoded."), HTTPStatus.INTERNAL_SERVER_ERROR)

    def handle_speech_recording(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_RECORDING_BYTES:
                raise BuildError("invalid_request", "Keep the recording to 15 seconds or less.")
            self.send_json(encode_recording(self.rfile.read(length)), HTTPStatus.OK)
        except BuildError as error:
            status = HTTPStatus.BAD_REQUEST if error.code == "invalid_request" else HTTPStatus.UNPROCESSABLE_ENTITY
            self.send_json_error(error, status)
        except Exception as error:  # noqa: BLE001
            print(f"builder: recording encoding failed: {redact_log(str(error))}", flush=True)
            self.send_json_error(BuildError("speech_encoding_failed", "The recording could not be converted into a Speech bank."), HTTPStatus.INTERNAL_SERVER_ERROR)

    def handle_manual(self) -> None:
        # Rendering takes a couple of seconds, so unlike /build this endpoint
        # answers synchronously with the finished PDF.
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            if not request_size_is_valid(content_length):
                raise BuildError("invalid_request", "The manual request size is invalid.")
            payload = json.loads(self.rfile.read(content_length))
            manual_key = payload.get("manualKey") if isinstance(payload, dict) else None
            if not isinstance(manual_key, str) or not BUILD_KEY_PATTERN.fullmatch(manual_key):
                raise BuildError("invalid_request", "The manual key is invalid.")
            manual_contract = payload.get("manualContract")
            if manual_contract is None:
                manual_contract = MANUAL_CONTRACT_FALLBACK
            elif not isinstance(manual_contract, str) or not MANUAL_CONTRACT_PATTERN.fullmatch(manual_contract):
                raise BuildError("invalid_request", "The manual contract is invalid.")
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_json_error(BuildError("invalid_request", "The manual request is not valid JSON."), HTTPStatus.BAD_REQUEST)
            return
        except BuildError as error:
            self.send_json_error(error, HTTPStatus.BAD_REQUEST)
            return

        try:
            pdf = render_manual_bytes(manual_key, payload.get("recipe"))
        except ValueError as error:
            self.send_json_error(BuildError("invalid_recipe", str(error)), HTTPStatus.BAD_REQUEST)
            return
        except Exception as error:  # noqa: BLE001 — surface renderer faults as JSON, not a dropped socket
            print(f"builder: manual render failed: {redact_log(str(error))}", flush=True)
            self.send_json_error(
                BuildError("manual_render_failed", "The field guide could not be rendered."),
                HTTPStatus.INTERNAL_SERVER_ERROR,
            )
            return

        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/pdf")
        self.send_header("Content-Length", str(len(pdf)))
        self.send_header("Content-Disposition", 'attachment; filename="plaits-palette-field-guide.pdf"')
        self.send_header("X-Plaits-Manual-Sha256", hashlib.sha256(pdf).hexdigest())
        self.send_header("X-Plaits-Manual-Contract", manual_contract)
        self.end_headers()
        self.wfile.write(pdf)

    def send_artifact(
        self,
        artifact_path: Path,
        output: FirmwareOutput,
        metadata: dict[str, str],
    ) -> None:
        content_type = "audio/wav" if output == "audio-wav" else "application/octet-stream"
        filename = "rubato-plaits-firmware.wav" if output == "audio-wav" else "rubato-plaits-firmware.hex"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(artifact_path.stat().st_size))
        self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        for name, value in metadata.items():
            self.send_header(name, value)
        self.end_headers()
        with artifact_path.open("rb") as artifact:
            shutil.copyfileobj(artifact, self.wfile, length=1024 * 1024)

    def send_wav(self, path: Path) -> None:
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(path.stat().st_size))
        self.end_headers()
        with path.open("rb") as audio:
            shutil.copyfileobj(audio, self.wfile)

    def send_json(self, value: Any, status: HTTPStatus) -> None:
        body = (json.dumps(value, separators=(",", ":")) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_json_error(self, error: BuildError, status: HTTPStatus) -> None:
        body = json.dumps({"error": {"code": error.code, "message": error.message, "detail": error.detail}}).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: object) -> None:
        print(f"builder: {format % args}", flush=True)


def main() -> None:
    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("0.0.0.0", 8080), Handler)
    server.serve_forever()


if __name__ == "__main__":
    main()
