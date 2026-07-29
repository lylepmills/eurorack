#!/usr/bin/env python3
"""Decode the AUX beacon readout from a hardware capture.

Capture (ES-8 as the capture device; Plaits AUX on input 2 = channel index 1):

    ffmpeg -f avfoundation -audio_device_index 1 -i "" -t 200 cap.wav
    python3 decode_beacons.py cap.wav 1

Written for the 1-section (13-report) SECTION_TOTAL validation build; the
NAMES table is the only thing to change for other section counts. Robust to
the two artifacts an at/over-budget build puts on AUX: engine-aux bleed in
the readout silences (partial overwrites under overrun drift -- defeated by
per-slot MEDIAN envelope, since readout silence is written zeros) and
uniform time-stretch (solved jointly with the envelope's edge bias from the
fixed reference tone plus the known-zero beacons, then every value decodes
through durations, with tone frequency as cross-check).
"""
import sys, wave
import numpy as np

SR = 48000
HOP = 480  # 10 ms

NAMES = {
    1: ("total usage", "usage"), 2: ("section 0", "usage"),
    3: ("raw ratio", "usage"), 4: ("violations", "exact"),
    5: ("canary", "exact"), 6: ("cadence", "exact"),
    7: ("last-block abs", "usage"), 8: ("calls x100", "exact"),
    9: ("snap s0 share", "exact"), 10: ("snap voices", "exact"),
    11: ("checksum lo", "exact"), 12: ("checksum mid", "exact"),
    13: ("checksum hi", "exact"),
}


def load(path, ch):
    w = wave.open(path)
    n = w.getnchannels()
    v = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    return v.reshape(-1, n)[:, ch].astype(np.float64) / 32768.0


def clean_active(x):
    # Per-slot MEDIAN |x|: readout silence is written zeros, so sparse engine
    # bleed (partial overwrites under overrun drift) cannot lift it, while
    # tone slots sit at the tone's median amplitude. The result is cleanly
    # bimodal where the max-envelope was hopeless.
    n = len(x) // HOP
    env = np.median(np.abs(x[: n * HOP]).reshape(n, HOP), axis=1)
    thr = 0.4 * np.percentile(env, 98)
    out = (env > thr).copy()
    # close 1-2 slot gaps, drop 1-2 slot blips (real features are >= 7 slots)
    for fill, limit in ((True, 2), (False, 2)):
        i = 0
        while i < n:
            if out[i] != fill:
                j = i
                while j < n and out[j] != fill:
                    j += 1
                if 0 < i and j < n and (j - i) <= limit:
                    out[i:j] = fill
                i = j
            else:
                i += 1
    return out, env, thr


def segments(active):
    segs = []
    i, n = 0, len(active)
    while i < n:
        if active[i]:
            j = i
            while j < n and active[j]:
                j += 1
            segs.append((i, j - i))  # slot units
            i = j
        else:
            i += 1
    return segs


def tone_hz(x, start_slot, len_slots):
    # FFT peak restricted to the readout band -- immune to engine bleed.
    s = x[start_slot * HOP:(start_slot + len_slots) * HOP]
    a, b = int(0.15 * len(s)), int(0.85 * len(s))
    s = s[a:b]
    if len(s) < 400:
        return float("nan")
    S = np.abs(np.fft.rfft(s * np.hanning(len(s))))
    f = np.fft.rfftfreq(len(s), 1.0 / SR)
    band = (f >= 2300) & (f <= 6900)
    return float(f[band][np.argmax(S[band])])


def main():
    path, ch = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 1
    x = load(path, ch)
    active, env, thr = clean_active(x)
    segs = segments(active)
    print(f"{len(segs)} segments after cleanup, thr {thr:.3f}, "
          f"{len(x)/SR:.1f} s")

    # Split into report groups at inter-report silences (> 32 slots quiet).
    # Within a group: short segs (< 15 slots) = bursts, long = ref then value.
    reports = []
    cur = {"bursts": 0, "tones": []}
    prev_end = None
    for start, length in segs:
        if prev_end is not None and (start - prev_end) > 32:
            if cur["tones"]:
                reports.append(cur)
            cur = {"bursts": 0, "tones": []}
        if length < 15:
            cur["bursts"] += 1
        else:
            cur["tones"].append((start, length))
        prev_end = start + length
    if cur["tones"]:
        reports.append(cur)

    good = []
    for r in reports:
        if len(r["tones"]) == 2 and 1 <= r["bursts"] <= 13:
            (rs, rl), (vs, vl) = r["tones"]
            good.append({"n": r["bursts"], "ref": rl, "val": vl,
                         "hz": tone_hz(x, vs, vl), "t": rs * HOP / SR})
        else:
            print(f"  reject group: {r['bursts']} bursts, "
                  f"{len(r['tones'])} long tones "
                  f"{[(l*10) for _, l in r['tones']]} ms")
    print(f"{len(good)} well-formed reports of {len(reports)} groups")

    # Solve stretch & bias in 10 ms slots: ref = 120*st + b; a known-zero
    # value tone (beacons 4, 5, 10) = 30*st + b.
    refs = [g["ref"] for g in good]
    ref_med = float(np.median(refs))
    zeros = [g["val"] for g in good if g["n"] in (4, 5, 10)]
    if zeros:
        z_med = float(np.median(zeros))
        stretch = (ref_med - z_med) / 90.0
        bias = ref_med - 120.0 * stretch
    else:
        stretch, bias = ref_med / 120.0, 0.0
    print(f"stretch x{stretch:.4f}  envelope bias {bias*10:.0f} ms  "
          f"(ref median {ref_med*10:.0f} ms, {len(zeros)} zero-anchors)")

    print(f"\n{'#':>2} {'report':15} {'t':>6} {'dur val':>8} {'freq val':>9} "
          f"{'val ms':>7}")
    by = {}
    for g in good:
        # value tone: (30 + 90*v/4096) slots, stretched, plus bias
        v_dur = (g["val"] - bias) / stretch
        val = (v_dur - 30.0) * 4096.0 / 90.0
        fval = g["hz"] - 2500.0
        name = NAMES.get(g["n"], ("?", ""))[0]
        print(f"{g['n']:>2} {name:15} {g['t']:6.1f} {val:8.0f} {fval:9.0f} "
              f"{g['val']*10:7.0f}")
        by.setdefault(g["n"], []).append((val, fval))

    print(f"\n=== medians across cycles ===")
    print(f"{'#':>2} {'report':15} {'dur val':>8} {'freq val':>9} {'n':>2}")
    for n in sorted(by):
        dv = float(np.median([a for a, _ in by[n]]))
        fv = float(np.median([b for _, b in by[n]]))
        print(f"{n:>2} {NAMES.get(n,('?',''))[0]:15} {dv:8.0f} {fv:9.0f} "
              f"{len(by[n]):>2}")


if __name__ == "__main__":
    main()
