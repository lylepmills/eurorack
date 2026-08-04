"""Reusable content-addressed Kokoro source artifacts for the local speech lab."""

from __future__ import annotations

import hashlib
import json
import os
import random
import shutil
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
from huggingface_hub import hf_hub_download
from kokoro import KPipeline


KOKORO_RATE = 24000
KOKORO_REPOSITORY = "hexgrad/Kokoro-82M"
PUBLISHED_MODEL_SHA256 = "496dba118d1a58f5f3db2efc88dbdc216e0483fc89fe6e47ee1f2c53f18ad1e4"
TTS_ARTIFACT_REVISION = "kokoro-0.9.4-source-v2-token-edges"
TOKEN_EDGE_PADDING_SECONDS = 0.025
LANGUAGE_CODES = {
    "en-US": "a",
    "en-GB": "b",
    "es": "e",
    "fr-FR": "f",
    "hi": "h",
    "it": "i",
    "pt-BR": "p",
    "ja": "j",
    "zh": "z",
}


def content_key(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def link_or_copy(source: Path, destination: Path) -> None:
    destination.unlink(missing_ok=True)
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


def write_json_atomic(path: Path, value: object) -> None:
    temporary = path.with_suffix(f"{path.suffix}.tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(temporary, path)


class TtsArtifactSession:
    def __init__(self, cache_root: Path, language: str, voice: str):
        if language not in LANGUAGE_CODES:
            raise ValueError(f"unsupported language: {language}")
        self.cache_root = cache_root
        self.language = language
        self.language_code = LANGUAGE_CODES[language]
        self.voice = voice
        self.pipeline: KPipeline | None = None
        self.voice_sha256: str | None = None

    def initialize(self) -> None:
        if self.pipeline is not None:
            return
        random.seed(0)
        np.random.seed(0)
        torch.manual_seed(0)
        torch.set_num_threads(1)
        torch.use_deterministic_algorithms(True)
        voice_path = Path(hf_hub_download(
            repo_id=KOKORO_REPOSITORY,
            filename=f"voices/{self.voice}.pt",
        ))
        self.voice_sha256 = hashlib.sha256(voice_path.read_bytes()).hexdigest()
        self.pipeline = KPipeline(lang_code=self.language_code, repo_id=KOKORO_REPOSITORY)

    @staticmethod
    def trim_token_edges(audio: np.ndarray, result: object) -> np.ndarray:
        tokens = getattr(result, "tokens", None)
        if not tokens:
            return audio
        timed_tokens = [
            token for token in tokens
            if getattr(token, "start_ts", None) is not None
            and getattr(token, "end_ts", None) is not None
            and any(character.isalnum() for character in getattr(token, "text", ""))
        ]
        if not timed_tokens:
            return audio
        first_seconds = min(float(token.start_ts) for token in timed_tokens)
        last_seconds = max(float(token.end_ts) for token in timed_tokens)
        first = max(0, round((first_seconds - TOKEN_EDGE_PADDING_SECONDS) * KOKORO_RATE))
        last = min(len(audio), round((last_seconds + TOKEN_EDGE_PADDING_SECONDS) * KOKORO_RATE))
        return audio[first:last] if last > first else audio

    def synthesize(self, text: str, trim_token_edges: bool) -> np.ndarray:
        self.initialize()
        chunks = []
        for result in self.pipeline(text, voice=self.voice, speed=1.0):
            audio = np.asarray(result.audio, dtype=np.float32)
            if trim_token_edges:
                audio = self.trim_token_edges(audio, result)
            chunks.append(audio)
        if not chunks:
            raise ValueError(f"Kokoro produced no audio for {text!r}")
        separator = np.zeros(round(0.1 * KOKORO_RATE), dtype=np.float32)
        joined: list[np.ndarray] = []
        for index, chunk in enumerate(chunks):
            if index:
                joined.append(separator)
            joined.append(chunk)
        return np.concatenate(joined)

    def source_artifact(
        self,
        text: str,
        trim_token_edges: bool = False,
    ) -> tuple[Path, dict[str, object], bool]:
        key = content_key({
            "revision": TTS_ARTIFACT_REVISION,
            "language": self.language,
            "voice": self.voice,
            "text": text,
            "trimTokenEdges": trim_token_edges,
        })
        artifact_dir = self.cache_root / "tts" / key
        source_path = artifact_dir / "source.wav"
        manifest_path = artifact_dir / "manifest.json"
        if source_path.is_file() and manifest_path.is_file():
            return source_path, json.loads(manifest_path.read_text(encoding="utf-8")), True

        artifact_dir.mkdir(parents=True, exist_ok=True)
        audio = self.synthesize(text, trim_token_edges)
        sf.write(source_path, audio, KOKORO_RATE, subtype="PCM_16")
        manifest = {
            "key": key,
            "language": self.language,
            "voice": self.voice,
            "text": text,
            "trimTokenEdges": trim_token_edges,
            "sampleRate": KOKORO_RATE,
            "samples": len(audio),
            "repository": KOKORO_REPOSITORY,
            "publishedModelSha256": PUBLISHED_MODEL_SHA256,
            "publishedVoiceSha256": self.voice_sha256,
        }
        write_json_atomic(manifest_path, manifest)
        return source_path, manifest, False
