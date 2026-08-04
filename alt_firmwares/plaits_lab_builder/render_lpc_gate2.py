"""Small compatibility surface used by the production continuous LPC analyzer."""

from __future__ import annotations

import os
import wave
from pathlib import Path

import numpy as np

SOURCE_RATE = 16000
DEFAULT_RENDERER = Path(os.environ.get("PLAITS_LPC_RENDERER", "/usr/local/bin/plaits-render-lpc-bank"))


def lowpass_taps() -> np.ndarray:
    size = 63
    offset = np.arange(size) - size // 2
    cutoff = 0.225
    taps = 2 * cutoff * np.sinc(2 * cutoff * offset) * np.hamming(size)
    return taps / np.sum(taps)


def read_wave(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, SOURCE_RATE):
            raise ValueError("expected mono 16-bit 16 kHz PCM WAV")
        return np.frombuffer(source.readframes(source.getnframes()), dtype="<i2").astype(np.float64) / 32768.0


def build_renderer(path: Path, force: bool) -> None:
    del force
    if not path.is_file():
        raise FileNotFoundError(f"missing LPC renderer: {path}")
