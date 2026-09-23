"""Is a MORPH error near noon louder than an engine's own strike-to-strike
variation?  Reads the strikes written by render_noon_strikes.cc.

Ratio = (distance from offset strikes to noon strikes) / (noon-vs-noon
distance), using 1/6-octave band energies. ~1.0 means the knob error is buried
in the engine's own randomness; well above 1 means it stands out. This is a
necessary condition for audibility, not a listening test.
"""
import numpy as np
SR, STRIKES, N = 48000, 8, 1600 * 12
edges = 50 * 2 ** (np.arange(0, 48) / 6.0); edges = edges[edges < 16000]
def bands(x):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return np.array([10 * np.log10(X[(f >= a) & (f < b)].sum() + 1e-12)
                     for a, b in zip(edges[:-1], edges[1:])])
def strikes(name, d):
    a = np.fromfile(f'/tmp/twistbuild/perc_{name}_{d}.raw', dtype=np.float32)
    return [bands(a[s * N:s * N + N]) for s in range(1, STRIKES)]
dist = lambda a, b: float(np.sqrt(np.mean((a - b) ** 2)))
print("  %-15s %8s %8s %8s %8s %8s   noise floor" % ("", "+0.5%", "+1%", "+2%", "+5%", "+10%"))
for name in ("plucked", "struck-drum", "cymbal", "particle-burst"):
    noon = strikes(name, 0)
    base = np.mean([dist(noon[i], noon[j]) for i in range(len(noon))
                    for j in range(len(noon)) if i < j])
    row = [np.mean([dist(o, n) for o in strikes(name, d) for n in noon]) / base
           for d in range(1, 6)]
    print("  %-15s" % name + "".join("%8.2f" % r for r in row) + "   %5.2f dB" % base)
