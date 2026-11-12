"""Does a setting act as a formant, fixed in Hz? Held notes over three octaves; the change in each
   third-octave band, averaged over the notes that have a harmonic in it. usage: formant.py label:name=value,name=value ..."""
import sys, numpy as np, wc
from concurrent.futures import ThreadPoolExecutor
wc.build()
NOTES = list(range(45, 82)); BANDS = [250 * 2 ** (i / 3) for i in range(0, 16)]
def harm(job):
    n, p = job
    x = wc.render(wc.held(n, 0.5, dur=0.7), params=p); seg = x[int(.35*48000):int(.7*48000)]; f0 = 440*2**((n-69)/12)
    f = wc.f0_of(seg, 48000, f0); h = wc.harmonics(seg, 48000, f, 24)
    return f, h
def env(p):
    with ThreadPoolExecutor(8) as ex: return list(ex.map(harm, [(n, p) for n in NOTES]))
def bands(a, b):
    out = []
    for c in BANDS:
        d = []
        for (f, h), (f2, h2) in zip(a, b):
            for k in range(1, 25):
                if c / 2 ** (1 / 6) <= k * f < c * 2 ** (1 / 6) and h[k - 1] > -120: d.append(h2[k - 1] - h[k - 1])
        out.append(np.mean(d) if d else np.nan)
    return np.array(out)
if __name__ == "__main__":
    ref = env({})
    print(" " * 26 + " ".join("%5.0f" % b if b < 1000 else "%4.1fk" % (b / 1000) for b in BANDS))
    for a in sys.argv[1:]:
        lab, ps = a.split(":"); p = dict(q.split("=") for q in ps.split(","))
        print("%-26s" % lab + " ".join("%+5.1f" % v for v in bands(ref, env(p))))
