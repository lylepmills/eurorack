#!/usr/bin/env python3
"""Network-isolated HTTP wrapper around the pinned Plaits ARM build."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import threading
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

from generate_engine_config import render_config, validate_recipe
from render_manual import manual_document, render_pdf


WORKSPACE = Path(os.environ.get("PLAITS_WORKSPACE", "/workspace")).resolve()
BUILD_ROOT = Path(os.environ.get("PLAITS_BUILD_ROOT", "/tmp/plaits-builds")).resolve()
SOURCE_REVISION = os.environ.get("PLAITS_SOURCE_REVISION", "development")
BUILD_CONTRACT_VERSION = os.environ.get("PLAITS_BUILD_CONTRACT", "2")
MANUAL_CONTRACT_VERSION = os.environ.get("PLAITS_MANUAL_CONTRACT", "1")
TOOLCHAIN_ID = "gcc-arm-none-eabi-4.8-2013q4"
TOOLCHAIN_BIN = os.environ.get("PLAITS_TOOLCHAIN_BIN", "/usr/local/arm-4.8.3/bin")
MAX_REQUEST_BYTES = 32 * 1024
MAX_BUILD_SECONDS = 12 * 60
BUILD_KEY_PATTERN = re.compile(r"^[0-9a-f]{64}$")

# Route the ARM compiler through ccache when it is installed. Only the three
# recipe-dependent translation units (voice/plaits/ui) see the generated
# engine_config.h, so every other object hits the image-baked warm cache and a
# novel recipe recompiles just those three plus the link and WAV encode.
_CCACHE = shutil.which("ccache")


def _compiler(binary: str) -> str:
    path = f"{TOOLCHAIN_BIN}/{binary}"
    return f"{_CCACHE} {path}" if _CCACHE else path


class BuildError(Exception):
    def __init__(self, code: str, message: str, detail: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.detail = detail


@dataclass
class BuildRecord:
    status: str = "building"
    wav_path: Path | None = None
    metadata: dict[str, str] | None = None
    error: BuildError | None = None


BUILD_RECORDS: dict[str, BuildRecord] = {}
BUILD_RECORDS_LOCK = threading.Lock()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def redact_log(value: str) -> str:
    redacted = value.replace(str(WORKSPACE), "<workspace>").replace(str(BUILD_ROOT), "<build-root>")
    return redacted[-8000:]


def parse_size(elf_path: Path) -> tuple[int, int, int]:
    result = subprocess.run(
        ["/usr/local/arm-4.8.3/bin/arm-none-eabi-size", str(elf_path)],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    fields = result.stdout.strip().splitlines()[-1].split()
    if len(fields) < 3:
        raise BuildError("invalid_artifact", "The compiler returned unreadable size metadata.")
    return int(fields[0]), int(fields[1]), int(fields[2])


# Catalog id -> the PLAITS_STEREO_<MACRO> that gates that engine's stereo render
# code (see plaits/dsp/engine/stereo_config.h and the makefile). The three DX7
# banks share the six-op engine, so they map to one macro. The five declare-only
# engines whose out/aux is already a stereo pair at ~0 cost are absent — they are
# never gated. Keep this in sync with PLAITS_STEREO_MODELS in plaits/makefile.
STEREO_MACROS = {
    "virtual-analog": "VIRTUAL_ANALOG",
    "waveshaping": "WAVESHAPING",
    "two-op-fm": "TWO_OP_FM",
    "granular-formant": "GRANULAR_FORMANT",
    "harmonic": "HARMONIC",
    "wavetable": "WAVETABLE",
    "chords": "CHORDS",
    "speech": "SPEECH",
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
}
ALL_STEREO_MACROS = frozenset(STEREO_MACROS.values())


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


def build_firmware(payload: Any) -> tuple[Path, dict[str, str]]:
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

    build_dir = BUILD_ROOT / build_key
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=False)
    config_path = build_dir / "engine_config.h"
    recipe_path = build_dir / "recipe.json"
    config_path.write_text(render_config(validated_recipe), encoding="utf-8")
    recipe_path.write_text(json.dumps(recipe, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")

    command = [
        "make",
        "-f",
        "plaits/makefile",
        f"BUILD_ROOT={build_dir}/",
        f"ENGINE_CONFIG={config_path}",
        f"CC={_compiler('arm-none-eabi-gcc')}",
        f"CXX={_compiler('arm-none-eabi-g++')}",
        # Every build gets a fresh BUILD_ROOT, so the per-object .d files are
        # never consulted for an incremental rebuild — suppress them to skip a
        # full-tree preprocessor pass. Empty DEPS makes the depends.mk recipe a
        # bare `cat`, which reads the closed stdin below and writes nothing.
        "DEPS=",
        "-j4",
        "wav",
    ]
    # Per-engine stereo. The stereo render path (OUT/AUX as an L/R pair) costs
    # flash per engine, so it is compiled only for the engines a recipe enables.
    # The makefile turns each PLAITS_STEREO_<MACRO>=0 make var into a -D on that
    # engine's object alone, so each engine caches as two variants and the
    # default (all-stereo) build's warm ccache is untouched. Enabled set:
    #   - aux != stereo: nothing is stereo -> disable every engine.
    #   - aux == stereo, stereo_engines is None (schema <= 9): the global-stereo
    #     back-compat case -> enable all.
    #   - aux == stereo, stereo_engines given (schema 10): enable exactly those.
    command.extend(_stereo_disable_flags(
        validated_recipe.aux_subosc_wave_option == 3,
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
        code = "flash_budget_exceeded" if "will not fit in region `FLASH'" in log else "compiler_failed"
        raise BuildError(code, "The firmware recipe did not produce a valid ARM build.", log)

    artifact_dir = build_dir / "plaits"
    wav_path = artifact_dir / "plaits.wav"
    bin_path = artifact_dir / "plaits.bin"
    elf_path = artifact_dir / "plaits.elf"
    if not wav_path.is_file() or not bin_path.is_file() or not elf_path.is_file():
        raise BuildError("invalid_artifact", "The compiler did not produce every required firmware artifact.", log)

    text_bytes, data_bytes, bss_bytes = parse_size(elf_path)
    if text_bytes + data_bytes > 224 * 1024:
        raise BuildError("flash_budget_exceeded", "The firmware exceeds Plaits' application flash budget.", log)
    if bss_bytes + 1024 > 32 * 1024:
        raise BuildError("ram_budget_exceeded", "The firmware exceeds Plaits' runtime memory budget.", log)

    metadata = {
        "X-Plaits-Binary-Sha256": sha256_file(bin_path),
        "X-Plaits-Wav-Sha256": sha256_file(wav_path),
        "X-Plaits-Text-Bytes": str(text_bytes),
        "X-Plaits-Data-Bytes": str(data_bytes),
        "X-Plaits-Bss-Bytes": str(bss_bytes),
        "X-Plaits-Source-Revision": SOURCE_REVISION,
        "X-Plaits-Toolchain": TOOLCHAIN_ID,
        "X-Plaits-Build-Contract": BUILD_CONTRACT_VERSION,
    }
    return wav_path, metadata


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
        wav_path, metadata = build_firmware(payload)
        result = BuildRecord(status="succeeded", wav_path=wav_path, metadata=metadata)
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
            self.send_json({"ok": True}, HTTPStatus.OK)
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
        if record.wav_path is None or record.metadata is None:
            self.send_json_error(BuildError("internal_error", "The compiler result is incomplete."), HTTPStatus.INTERNAL_SERVER_ERROR)
            return
        self.send_artifact(record.wav_path, record.metadata)

    def do_POST(self) -> None:  # noqa: N802
        if self.path == "/manual":
            self.handle_manual()
            return
        if self.path != "/build":
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            if content_length <= 0 or content_length > MAX_REQUEST_BYTES:
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

    def handle_manual(self) -> None:
        # Rendering takes a couple of seconds, so unlike /build this endpoint
        # answers synchronously with the finished PDF.
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            if content_length <= 0 or content_length > MAX_REQUEST_BYTES:
                raise BuildError("invalid_request", "The manual request size is invalid.")
            payload = json.loads(self.rfile.read(content_length))
            manual_key = payload.get("manualKey") if isinstance(payload, dict) else None
            if not isinstance(manual_key, str) or not BUILD_KEY_PATTERN.fullmatch(manual_key):
                raise BuildError("invalid_request", "The manual key is invalid.")
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
        self.send_header("X-Plaits-Manual-Contract", MANUAL_CONTRACT_VERSION)
        self.end_headers()
        self.wfile.write(pdf)

    def send_artifact(self, wav_path: Path, metadata: dict[str, str]) -> None:
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(wav_path.stat().st_size))
        self.send_header("Content-Disposition", 'attachment; filename="rubato-plaits-firmware.wav"')
        for name, value in metadata.items():
            self.send_header(name, value)
        self.end_headers()
        with wav_path.open("rb") as artifact:
            shutil.copyfileobj(artifact, self.wfile, length=1024 * 1024)

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
