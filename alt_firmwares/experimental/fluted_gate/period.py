from fluted_probe import *
import numpy as np

def partials(x, f0, tail=1.5, nh=9):
    """Level of each harmonic of f0, and the true period by autocorrelation."""
    seg = x[-int(tail*SR):].astype(np.float64)
    seg -= seg.mean()
    win = np.hanning(len(seg))
    nfft = 1 << int(np.ceil(np.log2(len(seg)*4)))
    spec = np.abs(np.fft.rfft(seg*win, nfft))
    peak = spec.max()
    lv = []
    for h in range(1, nh+1):
        c = f0*h
        i0 = int(c*0.985*nfft/SR); i1 = int(c*1.015*nfft/SR)+1
        lv.append(20*np.log10(spec[i0:i1].max()/peak + 1e-12))
    # True period: normalised autocorrelation, searched over 0.5..4 periods of f0
    ac = np.fft.irfft(np.abs(np.fft.rfft(seg, 2*len(seg)))**2)[:len(seg)]
    ac /= ac[0] + 1e-20
    lo = int(SR/(f0*4.5)); hi = int(SR/(f0*0.45))
    hi = min(hi, len(ac)-1)
    k = lo + int(np.argmax(ac[lo:hi]))
    return lv, SR/k, ac[k]

print("Is the PERIOD 1/f0 (loud 3rd harmonic = correct tuning),")
print("or is the period genuinely 1/(3*f0) (mode hop = wrong note)?\n")
print("Levels of each harmonic of the PLAYED note, dB below the loudest partial.")
print("'ac period' is the true waveform period from autocorrelation, as a ratio")
print("to the played note. 1.00 = the engine is in tune.\n")
hdr = "%-5s %-6s %6s" % ("note","morph","peak/f0") + "".join("%7s" % ("h%d"%h) for h in range(1,10)) + "%10s %7s" % ("ac f/f0","ac r")
print(hdr); print("-"*len(hdr))
for n in [48, 55, 60, 67, 72]:
    f0 = note_to_hz(n)
    for m in [0.0, 0.1, 0.2, 0.3, 0.4, 0.49]:
        x = render(model="port", note=n, harmonics=0.5, timbre=0.5, morph=m,
                   macro=0.5, seconds=3.0)
        r = analyse(x, f0)
        lv, acf, acr = partials(x, f0)
        print("%-5d %-6.2f %6.2f " % (n, m, r["dom_ratio"])
              + "".join("%7.1f" % v for v in lv)
              + "%10.3f %7.2f" % (acf/f0, acr))
    print()
