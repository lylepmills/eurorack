from fluted_probe import *
import numpy as np, os, subprocess
from multiprocessing import Pool

def job(a):
    note, h, morph, seed = a
    path = os.path.join(SP, "_w_%s.f32" % "_".join(str(v) for v in a))
    subprocess.run([PROBE, "--model", "port", "--note", str(note), "--harmonics", str(h),
                    "--timbre", "0.5", "--morph", str(morph), "--macro", "0.75",
                    "--seconds", "2.5", "--loop-dc", "0.999", "--seed", str(seed),
                    "--out", path], check=True)
    x = np.fromfile(path, dtype=np.float32); os.unlink(path)
    return a, analyse(x, note_to_hz(note), tail=1.2)

NOTES = [36, 48, 60, 72, 79]
HS = [i/24.0 for i in range(25)]
GRID = [(n, h, 0.25, 0x12345678) for n in NOTES for h in HS]
GRID += [(60, h, 0.25, s) for h in HS for s in (0xBEEF, 0xC0FFEE)]

def main():
    with Pool(10) as p:
        res = dict((tuple(a), r) for a, r in p.map(job, GRID))
    print("Pitch error in SEMITONES vs the played note, at the best-case setting")
    print("found anywhere in the 2000-point sweep (MACRO 0.75, in-loop DC 0.999,")
    print("MORPH 0.25 = below noon). '.' = within a quartertone.\n")
    print("HARMONICS ->")
    print("note   " + "".join("%5.2f" % h for h in HS[::2]))
    print("-"*(7+5*len(HS[::2])))
    for n in NOTES:
        cells = []
        for h in HS[::2]:
            r = res[(n,h,0.25,0x12345678)]
            if r["silent"] or r["rms"] < 0.02: cells.append("%5s" % "--")
            else:
                st = 12*np.log2(r["dom_ratio"])
                cells.append("%5s" % ("." if abs(st) < 0.5 else "%+.0f" % st))
        print("%-6d " % n + "".join(cells))

    print("\nIn-tune fraction of the HARMONICS travel, per note:")
    for n in NOTES:
        good = sum(1 for h in HS
                   if not res[(n,h,0.25,0x12345678)]["silent"]
                   and res[(n,h,0.25,0x12345678)]["rms"] > 0.02
                   and abs(12*np.log2(max(res[(n,h,0.25,0x12345678)]["dom_ratio"],1e-9))) < 0.5)
        print("  MIDI %-3d  %2d/25 of the knob" % (n, good))

    print("\nSame HARMONICS sweep at MIDI 60 under three different noise seeds")
    print("(does the mode choice depend on the breath noise, i.e. is it bistable?)\n")
    print("%-10s %10s %10s %10s" % ("HARMONICS","seed A","seed B","seed C"))
    for h in HS:
        vals = [res[(60,h,0.25,s)]["dom_ratio"] for s in (0x12345678,0xBEEF,0xC0FFEE)]
        flag = "  <-- differs" if (max(vals)/max(min(vals),1e-9)) > 1.03 else ""
        print("%-10.3f %10.3f %10.3f %10.3f%s" % (h, vals[0], vals[1], vals[2], flag))

if __name__ == "__main__":
    main()
