"""Do the settings do different things? For each change, the shift of every harmonic (dB, against the default,
   level-matched) on several notes; then the correlation between changes. High correlation = same effect."""
import wc, numpy as np
from concurrent.futures import ThreadPoolExecutor
wc.build()
NOTES = [40, 46, 52, 58, 64, 70, 76, 82]
CH = [("breath harder", dict(_b=0.85)), ("aperture 1.9", dict(aperture=1.9)), ("lay 0.3", dict(lay=0.3)), ("cone 0.22", dict(coneBeta=0.22)),
      ("cone 0.37", dict(coneBeta=0.37)), ("chamber 1.8", dict(chamber=1.8)), ("baffle 1.5", dict(baffle=1.5)), ("cutoff 320", dict(cutoffK=320)),
      ("bell +14", dict(bell=14)), ("mouth 600", dict(mouthDepth=1.2, mouthFreq=600)), ("mouth 1100", dict(mouthDepth=1.2, mouthFreq=1100)),
      ("mouth 2000", dict(mouthDepth=1.2, mouthFreq=2000)), ("reed 1700 Hz", dict(reedFreq=1700)), ("reed damp 0.4", dict(reedDamp=0.4))]
def prof(job):
    n, p = job; p = dict(p); b = p.pop("_b", 0.5)
    x = wc.render(wc.held(n, b), params=p); seg = x[int(.6*48000):int(1.1*48000)]; f0 = 440*2**((n-69)/12)
    f = wc.f0_of(seg, 48000, f0); h = wc.harmonics(seg, 48000, f, 14); h = h[h > -150]
    return h[:10] - 10*np.log10(np.sum(10**(h/10)))
def vec(p):
    with ThreadPoolExecutor(8) as ex: return list(ex.map(prof, [(n, p) for n in NOTES]))
ref = vec({}); V = {}
for name, p in CH:
    v = vec(p); d = np.concatenate([a[:min(len(a), len(b))] - b[:min(len(a), len(b))] for a, b in zip(v, ref)])
    V[name] = d
    print("%-14s typical harmonic shift %4.1f dB, largest %4.1f dB" % (name, np.sqrt(np.mean(d**2)), np.abs(d).max()))
names = list(V); L = min(len(v) for v in V.values())
C = np.corrcoef(np.array([V[n][:L] for n in names]))
print("\ncorrelation between changes (1 = same effect):")
print(" " * 15 + " ".join("%5d" % (i + 1) for i in range(len(names))))
for i, n in enumerate(names): print("%2d %-12s" % (i + 1, n) + " ".join("%5.2f" % c for c in C[i]))
