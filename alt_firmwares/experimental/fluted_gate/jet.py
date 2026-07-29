from fluted_probe import *
import numpy as np, os, subprocess
from multiprocessing import Pool

# HYPOTHESIS. The bore-only loop cannot self-oscillate: the bore write takes
# `reflection >> 1`, so its round-trip gain is capped at |H|/2 <= 0.5. The
# oscillation therefore has to come through the jet path, whose magnitude
# condition |1 - g*exp(-jwJ)| is maximised at w*J = pi. That predicts a mode
# family at
#       f = (2m+1) * f0 / (4 * jet_fraction)
# with jet_fraction = (48 + (HARMONICS*32767 >> 10)) / 256, i.e. 0.1875..0.3086.
# So the LOWEST available mode slides from 1.333*f0 (HARMONICS 0) through
# 1.016*f0 (HARMONICS 0.5) to 0.810*f0 (HARMONICS 1) -- HARMONICS is a PITCH
# control, and f0 is only reachable near the middle of its travel.

def jet_fraction(h):
    return (48 + (int(h * 32767.0) >> 10)) / 256.0

def job(a):
    note, h, morph, macro = a
    path = os.path.join(SP, "_j_%s.f32" % "_".join(str(v) for v in a))
    subprocess.run([PROBE, "--model", "port", "--note", str(note),
                    "--harmonics", str(h), "--timbre", "0.5", "--morph", str(morph),
                    "--macro", str(macro), "--seconds", "2.5",
                    "--loop-dc", "0.999", "--out", path], check=True)
    x = np.fromfile(path, dtype=np.float32); os.unlink(path)
    return a, analyse(x, note_to_hz(note), tail=1.2)

HS = [i/24.0 for i in range(25)]
GRID = [(60, h, 0.25, 0.75) for h in HS]

def main():
    with Pool(10) as p:
        res = dict((tuple(a), r) for a, r in p.map(job, GRID))
    print("MIDI 60, MORPH 0.25 (below noon), MACRO 0.75, loop-DC 0.999")
    print("HARMONICS is labelled 'Embouchure' -- a TIMBRE control.\n")
    print("%-10s %-8s %10s %12s %10s %8s" % (
        "HARMONICS","jet frac","dom/f0","predicted","semitones","rms"))
    print("-"*62)
    for h in HS:
        r = res[(60,h,0.25,0.75)]
        j = jet_fraction(h)
        fam = [(2*m+1)/(4*j) for m in range(4)]
        pred = min(fam, key=lambda v: abs(np.log(v/max(r["dom_ratio"],1e-9))))
        st = 12*np.log2(r["dom_ratio"]) if r["dom_ratio"]>0 else float("nan")
        print("%-10.3f %-8.4f %10.3f %12.3f %+10.1f %8.4f" % (
            h, j, r["dom_ratio"], pred, st, r["rms"]))

    print("\nLowest predicted mode 1/(4*jet_fraction) at the ends of HARMONICS:")
    for h in (0.0, 0.5, 1.0):
        j = jet_fraction(h)
        print("  HARMONICS %.1f -> jet %.4f -> lowest mode %.3f x f0  (%+.1f semitones)"
              % (h, j, 1/(4*j), 12*np.log2(1/(4*j))))

if __name__ == "__main__":
    main()
