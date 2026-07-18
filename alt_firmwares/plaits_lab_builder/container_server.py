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
MAX_REQUEST_BYTES = 32 * 1024
MAX_BUILD_SECONDS = 12 * 60
BUILD_KEY_PATTERN = re.compile(r"^[0-9a-f]{64}$")


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

    cppflags = f"-fno-exceptions -fno-rtti -include {config_path}"
    command = [
        "make",
        "-f",
        "plaits/makefile",
        f"BUILD_ROOT={build_dir}/",
        f"CPPFLAGS={cppflags}",
        "-j2",
        "wav",
    ]
    environment = {**os.environ, "LC_ALL": "C", "LANG": "C", "HOME": str(build_dir)}
    try:
        result = subprocess.run(
            command,
            cwd=WORKSPACE,
            env=environment,
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
        self.send_header("Content-Disposition", 'attachment; filename="plaits-lab-field-guide.pdf"')
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
