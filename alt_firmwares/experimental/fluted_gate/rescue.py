from fluted_probe import *
import numpy as np, os, subprocess, itertools
from multiprocessing import Pool

# Most generous reading available: in-loop DC blocker moved to 8 Hz (beyond the
# spec's eight fixes), and HARMONICS restricted to the window that held pitch
# at MIDI 60 -- i.e. give the design every fix and see if MORPH then works.

def job(a):
    note, h, morph, macro = a
    path = os.path.join(SP, "_r_%s.f32" % "_".join(str(v) for v in a))
    subprocess.run([PROBE, "--model", "port", "--note", str(note), "--harmonics", str(h),
                    "--timbre", "0.5", "--morph", str(morph), "--macro", str(macro),
                    "--seconds", "2.5", "--loop-dc", "0.999", "--out", path], check=True)
    x = np.fromfile(path, dtype=np.float32); os.unlink(path)
    return a, analyse(x, note_to_hz(note), tail=1.2)

NOTES  = [36, 43, 48, 55, 60, 67, 72, 79]
HS     = [0.20, 0.35, 0.50, 0.65, 0.78]      # the in-tune window only
MORPHS = [0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.85, 1.0]
MACROS = [0.5, 0.75]
GRID = list(itertools.product(NOTES, HS, MORPHS, MACROS))

def main():
    with Pool(10) as p:
        res = dict((tuple(a), r) for a, r in p.map(job, GRID, chunksize=8))
    for macro in MACROS:
        print("\nMACRO %.2f%s -- pitch error in semitones, '.' = within a quartertone"
              % (macro, "  (detent)" if macro == 0.5 else ""))
        print("HARMONICS restricted to its in-tune window. MORPH ->")
        hdr = "note  H     " + "".join("%6.2f" % m for m in MORPHS)
        print(hdr); print("-"*len(hdr))
        for n in NOTES:
            for h in HS:
                cells = []
                for m in MORPHS:
                    r = res[(n,h,m,macro)]
                    if r["silent"] or r["rms"] < 0.02: cells.append("%6s" % "--")
                    else:
                        st = 12*np.log2(max(r["dom_ratio"],1e-9))
                        cells.append("%6s" % ("." if abs(st)<0.5 else "%+.0f" % st))
                print("%-5d %-5.2f " % (n,h) + "".join(cells))
            print()
        good = sum(1 for n in NOTES for h in HS for m in MORPHS
                   if not res[(n,h,m,macro)]["silent"]
                   and res[(n,h,m,macro)]["rms"] > 0.02
                   and abs(12*np.log2(max(res[(n,h,m,macro)]["dom_ratio"],1e-9))) < 0.5)
        print("  in tune: %d / %d  (%.0f%%)" % (good, len(NOTES)*len(HS)*len(MORPHS),
              100.0*good/(len(NOTES)*len(HS)*len(MORPHS))))

if __name__ == "__main__":
    main()
