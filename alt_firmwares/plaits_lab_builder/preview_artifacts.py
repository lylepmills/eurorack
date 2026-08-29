"""Reusable content-addressed TTS source artifacts for the speech lab.

Two engines sit behind one interface. Kokoro produces the nine languages the
builder has always offered; Piper adds thirteen more and broadens the roster
inside several existing ones. Callers do not choose — the voice id decides,
because a voice belongs to exactly one engine and nothing upstream should have
to know which.

The cache key is deliberately UNCHANGED. It is still
{revision, language, voice, text, trimTokenEdges} with the same revision
string, so every Kokoro artifact already warmed on the website stays valid.
Piper voice ids are disjoint from Kokoro's, so they simply key to new entries.
Adding an "engine" field here would look tidier and would silently invalidate
the entire warmed cache.

Engines are imported lazily and never share a process in production: the server
runs encode_word_bank.py via subprocess, one request and therefore one voice at
a time. That matters more than it sounds. On macOS, loading piper and kokoro
into one interpreter makes them fight over which espeak-ng binding wins, and
the loser aborts the process with no traceback.
"""

from __future__ import annotations

import hashlib
import json
import os
import random
import shutil
from pathlib import Path

import numpy as np
import soundfile as sf


KOKORO_RATE = 24000
KOKORO_REPOSITORY = "hexgrad/Kokoro-82M"
PUBLISHED_MODEL_SHA256 = "496dba118d1a58f5f3db2efc88dbdc216e0483fc89fe6e47ee1f2c53f18ad1e4"
TTS_ARTIFACT_REVISION = "kokoro-0.9.4-source-v2-token-edges"
TOKEN_EDGE_PADDING_SECONDS = 0.025
# Where the image bakes the Piper ONNX voices. Overridable so the same code
# runs against a local checkout outside the container.
PIPER_VOICE_ROOT = Path(os.environ.get("PIPER_VOICE_ROOT", "/opt/piper-voices"))
# A speaker inside a multi-speaker model, as "<voice>__<speaker>". Only
# no_NO-nvcc-medium uses this today; it holds ten speakers in one file.
PIPER_SPEAKER_SEPARATOR = "__"

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


def is_piper_voice(voice: str) -> bool:
    """Which engine owns this voice id?

    Kokoro ids are two letters, an underscore and a lowercase name — af_heart,
    bf_emma, zm_yunyang — and never contain a hyphen. Piper ids always do:
    en_US-joe-medium, no_NO-nvcc-medium__KMN. The sets cannot collide, which is
    why the id alone is enough to route without a registry to keep in sync.
    """
    return "-" in voice


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


class _KokoroEngine:
    """The original path, behaviour-for-behaviour. Nine languages."""

    rate = KOKORO_RATE

    def __init__(self, language: str, voice: str):
        if language not in LANGUAGE_CODES:
            raise ValueError(f"unsupported language for Kokoro: {language}")
        self.language = language
        self.language_code = LANGUAGE_CODES[language]
        self.voice = voice
        self.pipeline = None
        self.voice_sha256: str | None = None

    def provenance(self) -> dict[str, object]:
        return {
            "engine": "kokoro",
            "repository": KOKORO_REPOSITORY,
            "publishedModelSha256": PUBLISHED_MODEL_SHA256,
            "publishedVoiceSha256": self.voice_sha256,
        }

    def initialize(self) -> None:
        if self.pipeline is not None:
            return
        import torch
        from huggingface_hub import hf_hub_download
        from kokoro import KPipeline

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

class _PiperEngine:
    """Piper ONNX voices, driven through piper-tts' own API.

    No espeak shim here. The wheel's bundled espeak-ng is broken only on macOS
    arm64, where it ignores the data directory it is handed; on the Linux image
    it initialises correctly and is also the exact version these voices were
    trained against, which matters — Homebrew's espeak renders the Swedish
    sj-sound as "sx" where the bundled one gives the "ɧ" the model expects.

    trim_token_edges is accepted and ignored. It is Kokoro's token-timestamp
    trim; Piper exposes no equivalent without an alignment-patched model, and
    the LPC encoder's own silence trim covers the ordinary case. The manifest
    records that it was not applied rather than implying it was.
    """

    def __init__(self, language: str, voice: str):
        self.language = language
        self.voice = voice
        model, _, speaker = voice.partition(PIPER_SPEAKER_SEPARATOR)
        self.model_name = model
        self.speaker = speaker or None
        self.model_path = PIPER_VOICE_ROOT / f"{model}.onnx"
        self.config_path = PIPER_VOICE_ROOT / f"{model}.onnx.json"
        self._voice = None
        self.rate = 22050
        self.model_sha256: str | None = None

    def provenance(self) -> dict[str, object]:
        return {
            "engine": "piper",
            "model": self.model_name,
            "speaker": self.speaker,
            "publishedModelSha256": self.model_sha256,
            "trimTokenEdgesApplied": False,
        }

    def initialize(self) -> None:
        if self._voice is not None:
            return
        from piper import PiperVoice

        if not self.model_path.is_file():
            raise ValueError(
                f"Piper voice {self.model_name!r} is not baked into this image "
                f"(looked in {PIPER_VOICE_ROOT})")
        self.model_sha256 = hashlib.sha256(self.model_path.read_bytes()).hexdigest()
        self._voice = PiperVoice.load(self.model_path, config_path=self.config_path)
        self.rate = int(self._voice.config.sample_rate)

    def phoneme_count(self, text: str) -> int:
        """Segments in this text, for the length guard.

        Piper phonemizes internally, so the count is available for free. Kokoro
        exposes no equivalent, which is why the guard runs on Piper voices only
        — see bake_guard.
        """
        self.initialize()
        from piper import espeakbridge
        from piper.phonemize_espeak import ESPEAK_DATA_DIR

        espeakbridge.initialize(str(ESPEAK_DATA_DIR))
        espeakbridge.set_voice(self._voice.config.espeak_voice)
        phonemes = "".join(p for p, _, _ in espeakbridge.get_phonemes(text + "\n"))
        from bake_guard import count_phonemes

        return count_phonemes(phonemes)

    def synthesize(self, text: str, trim_token_edges: bool) -> np.ndarray:
        del trim_token_edges  # Kokoro-only; see the class docstring.
        self.initialize()
        # Piper's voices are VITS models trained on sentences, and several of
        # them treat a bare word as an unfinished utterance and keep generating.
        # A terminator settles them without changing the word.
        if text.strip()[-1:] not in ".?!":
            text = text.rstrip() + "."
        config = None
        if self.speaker is not None:
            from piper import SynthesisConfig

            speaker_id = self._voice.config.speaker_id_map.get(self.speaker)
            if speaker_id is None:
                raise ValueError(
                    f"unknown speaker {self.speaker!r} in {self.model_name!r}")
            config = SynthesisConfig(speaker_id=int(speaker_id))
        chunks = [np.frombuffer(chunk.audio_int16_bytes, dtype="<i2")
                  for chunk in self._voice.synthesize(text, syn_config=config)]
        if not chunks:
            raise ValueError(f"Piper produced no audio for {text!r}")
        return (np.concatenate(chunks).astype(np.float32) / 32768.0)


def _engine_for(language: str, voice: str):
    return (_PiperEngine(language, voice) if is_piper_voice(voice)
            else _KokoroEngine(language, voice))


class TtsArtifactSession:
    """Content-addressed source audio, whichever engine owns the voice."""

    def __init__(self, cache_root: Path, language: str, voice: str):
        self.cache_root = cache_root
        self.language = language
        self.voice = voice
        self.engine = _engine_for(language, voice)

    def phoneme_count(self, text: str) -> int | None:
        counter = getattr(self.engine, "phoneme_count", None)
        return counter(text) if counter else None

    @property
    def voice_sha256(self) -> str | None:
        # Kept for callers that read it off the session after a render.
        return getattr(self.engine, "voice_sha256", None) or \
            getattr(self.engine, "model_sha256", None)

    def source_artifact(
        self,
        text: str,
        trim_token_edges: bool = False,
        refresh: bool = False,
    ) -> tuple[Path, dict[str, object], bool]:
        """Source audio for one utterance, cached by content.

        refresh=True draws again and overwrites, keeping the SAME key. These
        models are stochastic, so a word that came out badly is usually fine on
        another draw, and the caller re-rolls rather than failing the build. The
        key deliberately does not include the attempt: one artifact per
        utterance, holding the draw that was accepted.
        """
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
        if not refresh and source_path.is_file() and manifest_path.is_file():
            return source_path, json.loads(manifest_path.read_text(encoding="utf-8")), True

        artifact_dir.mkdir(parents=True, exist_ok=True)
        audio = self.engine.synthesize(text, trim_token_edges)
        sf.write(source_path, audio, self.engine.rate, subtype="PCM_16")
        manifest = {
            "key": key,
            "language": self.language,
            "voice": self.voice,
            "text": text,
            "trimTokenEdges": trim_token_edges,
            "sampleRate": self.engine.rate,
            "samples": len(audio),
            **self.engine.provenance(),
        }
        write_json_atomic(manifest_path, manifest)
        return source_path, manifest, False
