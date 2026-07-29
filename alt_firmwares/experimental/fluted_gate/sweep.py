from fluted_probe import *
import itertools, os, subprocess
import numpy as np
from multiprocessing import Pool

NOTES  = [36, 43, 48, 55, 60, 67, 72, 79]
MORPHS = [0.0, 0.1, 0.2, 0.3, 0.4]          # strictly below noon
HARMS  = [0.0, 0.25, 0.5, 0.75, 1.0]
MACROS = [0.0, 0.25, 0.5, 0.75, 1.0]
LOOPDC = [0.98, 0.999]                      # 153 Hz (spec) vs 7.6 Hz

GRID = list(itertools.product(NOTES, MORPHS, HARMS, MACROS, LOOPDC))


def job(a):
    note, morph, harm, macro, dc = a
    path = os.path.join(SP, "_s_%s.f32" % "_".join(str(v) for v in a))
    subprocess.run(
        [PROBE, "--model", "port", "--note", str(note), "--harmonics", str(harm),
         "--timbre", "0.5", "--morph", str(morph), "--macro", str(macro),
         "--seconds", "2.5", "--loop-dc", str(dc), "--out", path], check=True)
    x = np.fromfile(path, dtype=np.float32)
    os.unlink(path)
    f0 = note_to_hz(note)
    r = analyse(x, f0, tail=1.2)
    # Level of the PLAYED note relative to the loudest partial: the check that
    # separates "bright, but in tune" from "sounding a different note".
    seg = x[-int(1.2 * SR):].astype(np.float64)
    seg -= seg.mean()
    nfft = 1 << int(np.ceil(np.log2(len(seg) * 4)))
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft))
    i0, i1 = int(f0 * 0.985 * nfft / SR), int(f0 * 1.015 * nfft / SR) + 1
    r["f0_level_db"] = 20 * np.log10(spec[i0:i1].max() / (spec.max() + 1e-20) + 1e-12)
    return a, r


def in_tune(r):
    """Audible, and the played note is the loudest thing in the spectrum."""
    return (not r["silent"] and r["rms"] > 0.02
            and abs(cents(r["dom_ratio"])) < 50.0
            and r["f0_level_db"] > -6.0)


def main():
    with Pool(10) as p:
        res = dict((tuple(a), r) for a, r in p.map(job, GRID, chunksize=8))

    print("Renders: %d\n" % len(GRID))
    print("=== Is the PLAYED NOTE the dominant partial? (+-50 cents, and f0")
    print("    within 6 dB of the loudest partial). Count over the 25")
    print("    HARMONICS x MACRO combinations, per note x MORPH.\n")
    for dc in LOOPDC:
        corner = (1 - dc) / 6.2832 * 48000
        print("  in-loop DC blocker %.3f  (corner %.0f Hz)" % (dc, corner))
        hdr = "  note  " + "".join("%9s" % ("m=%.1f" % m) for m in MORPHS)
        print(hdr + "\n  " + "-" * (len(hdr) - 2))
        for n in NOTES:
            print("  %-6d" % n + "".join(
                "%9s" % ("%d/25" % sum(in_tune(res[(n, m, h, mc, dc)])
                                       for h in HARMS for mc in MACROS))
                for m in MORPHS))
        print()

    print("=== Best single (HARMONICS, MACRO, loop-DC) across the keyboard")
    print("    at every MORPH below noon (max 40 = 8 notes x 5 morphs)\n")
    best = sorted(
        ((sum(in_tune(res[(n, m, h, mc, dc)]) for n in NOTES for m in MORPHS),
          h, mc, dc) for h, mc, dc in itertools.product(HARMS, MACROS, LOOPDC)),
        reverse=True)
    print("  %-10s %-8s %-8s %-8s" % ("in tune", "HARM", "MACRO", "loopDC"))
    for c, h, mc, dc in best[:6]:
        print("  %-10s %-8.2f %-8.2f %-8.3f" % ("%d/40" % c, h, mc, dc))

    print("\n=== Which multiple of f0 does it choose? (audible takes)\n")
    hist = {}
    for r in res.values():
        if not r["silent"] and r["rms"] > 0.02:
            hist[round(r["dom_ratio"], 1)] = hist.get(round(r["dom_ratio"], 1), 0) + 1
    audible = sum(hist.values())
    for k in sorted(hist):
        if hist[k] >= 10:
            print("  %5.2f x f0 : %4d takes (%4.1f%%)  %s"
                  % (k, hist[k], 100.0 * hist[k] / audible, "#" * (hist[k] // 20)))
    tuned = sum(in_tune(r) for r in res.values())
    print("\n  audible: %d of %d takes; in tune: %d (%.1f%%)"
          % (audible, len(GRID), tuned, 100.0 * tuned / len(GRID)))

    np.save(os.path.join(SP, "sweep.npy"), res, allow_pickle=True)


if __name__ == "__main__":
    main()
