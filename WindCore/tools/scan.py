"""One held note against one or two settings: level, tuning, harmonics, centroid."""
import sys, itertools, wc, numpy as np
def measure(n, params, breath=1.0):
    x = wc.render(wc.held(n, breath), cal=False, params=params)
    seg = x[int(.6*48000):int(1.1*48000)]; f0 = 440*2**((n-69)/12)
    r = np.sqrt(np.mean(seg**2))
    if r < 1e-5: return None
    f = wc.f0_of(seg, 48000, f0); h = wc.harmonics(seg, 48000, f, 40)
    a = 10**(h/20); k = np.arange(1, 41)
    return wc.db(r), 1200*np.log2(f/f0), h - h[0], (a*k).sum()/a.sum()
if __name__ == "__main__":
    n = int(sys.argv[1]); axes = []; base = {}
    for a in sys.argv[2:]:
        k, v = a.split("=")
        if "," in v: axes.append((k, v.split(",")))
        else: base[k] = v
    print("note", wc.note_name(n), base)
    for combo in itertools.product(*[v for _, v in axes]):
        p = dict(base, **{k: c for (k, _), c in zip(axes, combo)})
        if "gamma" in p: g = p.pop("gamma"); p["breathFloor"] = g; p["breathTop"] = g
        m = measure(n, p)
        lab = " ".join("%s=%s" % (k, c) for (k, _), c in zip(axes, combo))
        if m is None: print("%-34s silent" % lab); continue
        print("%-34s %6.1f dB %+5.0f c  H2-8 %s  centroid %.2f" % (lab, m[0], m[1], " ".join("%+5.1f" % v for v in m[2][1:8]), m[3]))
