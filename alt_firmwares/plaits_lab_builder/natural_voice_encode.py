"""WORLD-vocoder encoder for Natural Voice word banks.

The production counterpart of `render_lpc_continuous.py`: that one turns
source audio into the classic 14-byte LPC10 frames, this one turns it into
the 23-byte NVH1 frames the Natural Voice engine plays. Source audio comes
from the same `TtsArtifactSession` the Speech pipeline uses, so text banks,
every Kokoro and Piper voice, and uploaded recordings all arrive here the
same way.

The lab original is `research/natural_speech/analyze_world.py` in the
rubato-audio repo. This is a deliberate second copy rather than a shared
import -- the same split the LPC path already has -- so the builder image
carries no dependency on the research tree. The numbers below are load
bearing and must not drift from it:

  * 16 kHz analysis, 25 ms hop (40 Hz frames), order-18 all-pole fit
  * five voicing bands at 0/500/1k/2k/4k/8k
  * banks normalized to a 100 Hz register, F0 stored as a semitone offset
  * log-area-ratios clamped to +-7.0 and stored as int8

Two findings from the R&D are baked in here rather than in the firmware,
because they are analysis decisions:

  * Voicing is gated on whether the WAVEFORM actually repeats. Harvest
    reports an F0 straight through a fricative and D4C then measures low
    aperiodicity in bands carrying almost no energy, so without the gate an
    /s/ is stored as a voiced frame and the engine says /z/.
  * The hop must be a whole number of WORLD frames. Rounding silently
    desynchronises the frames from the rate the bank advertises and the word
    plays stretched.
"""

from __future__ import annotations

import base64
import struct
from typing import Any

import numpy as np

ANALYSIS_FS = 16000
HOP_MS = 25.0
WORLD_HOP_MS = 5.0
LPC_ORDER = 18
VOICING_BANDS = 5
VOICING_EDGES_HZ = [0.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0]
F0_BASE = 100.0
LAR_MAX = 7.0
SILENCE_DB = -70.0

# Waveform-periodicity gate: below LO fully unvoiced, above HI fully trusted.
PERIODICITY_LO = 0.30
PERIODICITY_HI = 0.60

FRAME_STRUCT = struct.Struct("<5B18b")


def _require_world():
    try:
        import pyworld
    except ModuleNotFoundError as error:  # pragma: no cover - image config
        raise RuntimeError(
            "Natural Voice bank encoding needs the pyworld package in the "
            "builder image") from error
    return pyworld


def load_source(path, fs: int = ANALYSIS_FS) -> np.ndarray:
    import soundfile as sf
    from scipy.signal import resample_poly

    x, source_fs = sf.read(str(path), always_2d=False)
    if x.ndim > 1:
        x = x.mean(axis=1)
    x = x.astype(np.float64)
    if source_fs != fs:
        from math import gcd
        divisor = gcd(int(source_fs), fs)
        x = resample_poly(x, fs // divisor, int(source_fs) // divisor)
    peak = np.abs(x).max() + 1e-12
    return x / peak if peak > 1.0 else x


def waveform_periodicity(x: np.ndarray, fs: int,
                         hop_ms: float = HOP_MS) -> np.ndarray:
    from scipy.signal import butter, lfilter

    b, a = butter(4, 1200.0 / (fs / 2.0), "low")
    lp = lfilter(b, a, x)
    hop = int(fs * hop_ms / 1000.0)
    win = int(fs * 0.040)
    lo, hi = int(fs / 400.0), int(fs / 60.0)
    out = []
    for index in range(int(np.ceil(len(x) / hop))):
        chunk = lp[index * hop:index * hop + win]
        if len(chunk) < win or np.sqrt((chunk ** 2).mean()) < 1e-6:
            out.append(0.0)
            continue
        chunk = chunk - chunk.mean()
        ac = np.correlate(chunk, chunk, "full")[len(chunk) - 1:]
        ac /= (ac[0] + 1e-12)
        out.append(float(max(0.0, np.max(ac[lo:hi]))))
    return np.asarray(out)


def voicing_gate(periodicity: np.ndarray) -> np.ndarray:
    return np.clip((periodicity - PERIODICITY_LO) /
                   (PERIODICITY_HI - PERIODICITY_LO), 0.0, 1.0)


def analyze(x: np.ndarray, fs: int = ANALYSIS_FS,
            hop_ms: float = HOP_MS) -> dict[str, Any]:
    pw = _require_world()
    ratio = hop_ms / WORLD_HOP_MS
    step = int(round(ratio))
    if abs(ratio - step) > 1e-9 or step < 1:
        raise ValueError(
            f"hop_ms={hop_ms} is not a whole number of {WORLD_HOP_MS} ms "
            "WORLD frames")

    f0, t = pw.harvest(x, fs, f0_floor=60.0, f0_ceil=400.0,
                       frame_period=WORLD_HOP_MS)
    f0 = pw.stonemask(x, f0, t, fs)
    sp = pw.cheaptrick(x, f0, t, fs)
    ap = pw.d4c(x, f0, t, fs)

    count = max(1, len(f0) // step)
    sp_d = np.empty((count, sp.shape[1]))
    ap_d = np.empty((count, ap.shape[1]))
    f0_d = np.zeros(count)
    voiced_frac = np.zeros(count)
    for i in range(count):
        block = slice(i * step, min((i + 1) * step, len(f0)))
        sp_d[i] = sp[block].mean(axis=0)
        weights = sp[block].sum(axis=1) + 1e-12
        ap_d[i] = np.sqrt(
            (ap[block] ** 2 * weights[:, None]).sum(axis=0) / weights.sum())
        voiced = f0[block] > 0
        voiced_frac[i] = voiced.mean()
        if voiced.any():
            f0_d[i] = np.median(f0[block][voiced])

    periodicity = waveform_periodicity(x, fs, hop_ms)
    if len(periodicity) < count:
        periodicity = np.pad(periodicity, (0, count - len(periodicity)))
    return {"fs": fs, "sp": sp_d, "ap": ap_d, "f0": f0_d,
            "voiced_frac": voiced_frac,
            "gate": voicing_gate(periodicity[:count])}


def band_voicing(frames: dict[str, Any]) -> np.ndarray:
    fs = frames["fs"]
    freqs = np.linspace(0.0, fs / 2.0, frames["sp"].shape[1])
    v = np.zeros((len(frames["f0"]), VOICING_BANDS))
    for band in range(VOICING_BANDS):
        lo = VOICING_EDGES_HZ[band]
        hi = min(VOICING_EDGES_HZ[band + 1], fs / 2.0 + 1.0)
        mask = (freqs >= lo) & (freqs < hi)
        if not mask.any():
            continue
        weights = frames["sp"][:, mask] + 1e-12
        harmonic = 1.0 - frames["ap"][:, mask] ** 2
        v[:, band] = np.clip(
            (harmonic * weights).sum(axis=1) / weights.sum(axis=1), 0.0, 1.0)
    v *= np.clip(frames["voiced_frac"], 0.0, 1.0)[:, None] ** 0.5
    v *= frames["gate"][:, None]
    return v


def _levinson(r: np.ndarray, order: int):
    a = np.zeros(order + 1)
    a[0] = 1.0
    error = r[0] + 1e-12
    ks = np.zeros(order)
    for m in range(1, order + 1):
        acc = r[m] + np.dot(a[1:m], r[m - 1:0:-1])
        k = -acc / error
        ks[m - 1] = k
        updated = a.copy()
        updated[1:m] += k * a[m - 1:0:-1]
        updated[m] = k
        a = updated
        error *= max(1.0 - k * k, 1e-9)
    return ks, error


def all_pole_fit(frames: dict[str, Any], order: int = LPC_ORDER):
    sp = frames["sp"]
    ks = np.zeros((len(sp), order))
    excitation_db = np.full(len(sp), -120.0)
    power_db = np.full(len(sp), -120.0)
    for i in range(len(sp)):
        r = np.fft.irfft(sp[i])[:order + 1]
        if r[0] <= 1e-14:
            continue
        k, residual = _levinson(r, order)
        ks[i] = np.clip(k, -0.9995, 0.9995)
        excitation_db[i] = 10.0 * np.log10(residual + 1e-14)
        power_db[i] = 10.0 * np.log10(r[0] + 1e-14)
    return ks, excitation_db, power_db


def f0_contour(frames: dict[str, Any]):
    """Semitone offsets from the word's own register, plus that register.

    The pitch tracker reports an F0 through fricatives, so the register is
    taken from confidently voiced frames only and the contour is clamped and
    median-filtered before the unvoiced stretches are bridged.
    """
    f0 = frames["f0"]
    gate = frames["gate"]
    has_pitch = f0 > 0
    voiced = has_pitch & (gate > 0.0)
    if not has_pitch.any():
        return np.zeros(len(f0)), F0_BASE, voiced
    confident = has_pitch & (gate > 0.5)
    median = float(np.median(f0[confident if confident.any() else has_pitch]))
    st = np.zeros(len(f0))
    st[has_pitch] = 12.0 * np.log2(f0[has_pitch] / median)
    st = np.clip(st, -6.0, 6.0)
    if len(st) >= 3:
        padded = np.pad(st, 1, mode="edge")
        st = np.median(
            np.stack([padded[:-2], padded[1:-1], padded[2:]]), axis=0)
    index = np.where(has_pitch)[0]
    st = np.interp(np.arange(len(f0)), index, st[index])
    return st, median, voiced


def encode_word(path, hop_ms: float = HOP_MS) -> bytes:
    """One utterance's audio as packed NVH1 frames."""
    x = load_source(path)
    frames = analyze(x, hop_ms=hop_ms)
    ks, excitation_db, power_db = all_pole_fit(frames)
    lar = np.log((1.0 + np.clip(ks, -0.9995, 0.9995)) /
                 (1.0 - np.clip(ks, -0.9995, 0.9995)))
    v = np.round(np.clip(band_voicing(frames), 0.0, 1.0) * 15.0) / 15.0
    st, _, voiced = f0_contour(frames)

    active = np.where(power_db > SILENCE_DB)[0]
    lo = max(0, active[0] - 1) if len(active) else 0
    hi = min(len(power_db), active[-1] + 2) if len(active) else len(power_db)

    packed = bytearray()
    for i in range(lo, hi):
        if power_db[i] <= SILENCE_DB:
            gain = 0
        else:
            gain = int(np.clip(round((excitation_db[i] + 96.0) / 0.5), 1, 255))
        nibbles = [int(round(value * 15.0)) for value in v[i]]
        lars = np.clip(np.round(lar[i] / LAR_MAX * 127.0), -127, 127)
        packed += FRAME_STRUCT.pack(
            gain,
            int(np.clip(round(st[i] / 0.25), -128, 127)) & 0xFF,
            nibbles[0] | (nibbles[1] << 4),
            nibbles[2] | (nibbles[3] << 4),
            nibbles[4] | ((1 if voiced[i] else 0) << 4),
            *[int(value) for value in lars])
    return bytes(packed)


def encode_bank(sources) -> dict[str, Any]:
    """A recipe bank from (word, source path) pairs.

    Returns the shape `natural_voice_banks.validate_natural_voice_banks`
    accepts, so the encoder's output and the recipe contract cannot drift.
    """
    words: list[str] = []
    boundaries = [0]
    payload = bytearray()
    for word, path in sources:
        frames = encode_word(path)
        if not frames:
            raise ValueError(f"{word!r} produced no frames")
        words.append(word)
        payload += frames
        boundaries.append(len(payload) // FRAME_STRUCT.size)
    return {
        "words": words,
        "wordBoundaries": boundaries,
        "frameData": base64.b64encode(bytes(payload)).decode("ascii"),
    }
