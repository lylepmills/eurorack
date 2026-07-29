# Copyright 2026 Lyle Mills.
# SPDX-License-Identifier: MIT
#
# Analysis half of the section 3.11 `fluted` gate measurement.
#
# For each rendered take: the dominant partial, the perceived pitch (harmonic
# product spectrum, which is what a listener calls "the note"), and both as a
# ratio to the played note.

import os
import subprocess
import sys
import numpy as np

SP = os.path.dirname(os.path.abspath(__file__))
PROBE = os.path.join(SP, "fluted_probe")
SR = 48000.0
CORRECTED = 47872.34


def note_to_hz(note, rate=SR):
    """The frequency the engine's own delay is built from, in Hz at `rate`.

    NoteToFrequency is normalised (cycles/sample) against kCorrectedSampleRate,
    so a take played back at 48000 lands 4.6 cents high. Measuring against this
    number rather than 440*2**((n-69)/12) keeps the port's known, uniform pitch
    offset out of the mode-tracking result.
    """
    return (440.0 / 8.0 / CORRECTED) * 0.25 * 2.0 ** ((note - 9.0) / 12.0) * rate


def render(model="port", note=60.0, harmonics=0.5, timbre=0.5, morph=0.25,
           macro=0.5, seconds=3.0, seed=0x12345678, **kw):
    path = os.path.join(SP, "_take.f32")
    cmd = [PROBE, "--model", model, "--note", str(note),
           "--harmonics", str(harmonics), "--timbre", str(timbre),
           "--morph", str(morph), "--macro", str(macro),
           "--seconds", str(seconds), "--seed", str(seed), "--out", path]
    for k, v in kw.items():
        cmd += ["--" + k.replace("_", "-"), str(v)]
    subprocess.run(cmd, check=True)
    return np.fromfile(path, dtype=np.float32)


def analyse(x, f0, tail=1.5):
    """Dominant partial and perceived pitch of the settled part of a take."""
    n = int(tail * SR)
    seg = x[-n:].astype(np.float64)
    rms = float(np.sqrt(np.mean(seg ** 2)))
    if rms < 1e-5:
        return dict(rms=rms, silent=True, dominant=0.0, dom_ratio=0.0,
                    perceived=0.0, perc_ratio=0.0, f0_energy_db=-999.0)

    win = np.hanning(len(seg))
    nfft = 1 << int(np.ceil(np.log2(len(seg) * 4)))
    spec = np.abs(np.fft.rfft(seg * win, nfft))
    freqs = np.fft.rfftfreq(nfft, 1.0 / SR)

    # Ignore anything under 20 Hz (DC-blocker residue) and over 12 kHz.
    lo = np.searchsorted(freqs, 20.0)
    hi = np.searchsorted(freqs, 12000.0)
    band = spec[lo:hi]

    # Dominant partial, with parabolic interpolation on the log magnitude.
    k = int(np.argmax(band)) + lo
    a, b, c = (np.log(spec[k - 1] + 1e-20), np.log(spec[k] + 1e-20),
               np.log(spec[k + 1] + 1e-20))
    delta = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    dominant = (k + delta) * SR / nfft

    # Perceived pitch: harmonic product spectrum over a coarse grid. This is
    # the question "what note does it sound like", which can differ from the
    # loudest partial when the loop puts energy on a sub-fundamental.
    cand = np.geomspace(f0 / 6.0, f0 * 6.0, 4000)
    hps = np.zeros_like(cand)
    for h in range(1, 9):
        idx = np.clip((cand * h * nfft / SR).astype(int), 0, len(spec) - 1)
        hps += np.log(spec[idx] + 1e-20)
    perceived = float(cand[int(np.argmax(hps))])

    # How much energy actually sits on the played note, +-3%.
    def band_db(centre):
        i0 = np.searchsorted(freqs, centre * 0.97)
        i1 = np.searchsorted(freqs, centre * 1.03)
        if i1 <= i0:
            return -999.0
        return 20 * np.log10(np.max(spec[i0:i1]) / (np.max(band) + 1e-20) + 1e-20)

    return dict(rms=rms, silent=False, dominant=dominant,
                dom_ratio=dominant / f0, perceived=perceived,
                perc_ratio=perceived / f0, f0_energy_db=band_db(f0))


def cents(ratio):
    return 1200.0 * np.log2(ratio) if ratio > 0 else float("nan")
