"""Turn the per-note fits (../build/fit/*.json) into the horn's curves (../curves.txt), and score them.
   usage: curves.py [sigma in anchors, default 1.0]     then: curves.py score"""
import sys, os, json, glob, numpy as np, wc, voice, gap, fit
ORDER = ["cutoffK", "lossRatio", "coneBeta", "chamber", "baffle", "aperture", "lay", "embTrack", "boreLoss"]   # Curve enum order
LOG = {n: lg for n, lo, hi, lg in fit.FREE}
def load():
    F = sorted((json.load(open(f)) for f in glob.glob(os.path.join(fit.OUT, "*.json"))), key=lambda d: d["note"])
    return np.array([d["note"] for d in F]), {k: np.array([d["params"][k] for d in F]) for k in ORDER + ["bell", "embFloor"]}, F
def smooth(v, sigma, log):
    x = np.log(v) if log else np.array(v, float); n = len(x); out = np.zeros(n)
    for i in range(n):
        w = np.exp(-0.5 * ((np.arange(n) - i) / sigma) ** 2); out[i] = (w * x).sum() / w.sum()
    return np.exp(out) if log else out
def build(sigma=1.0, path=wc.CURVES):
    notes, P, _ = load(); allp = {}
    for k in ORDER + ["bell", "embFloor"]:
        s = smooth(P[k], sigma, LOG.get(k, False)) if sigma > 0 else P[k]
        allp[k] = np.interp(np.arange(128), notes, s)
    with open(path, "w") as f:
        for n in range(128):
            f.write("%d " % n + " ".join("%.5f" % (allp[k][n] / fit.BASE[k]) for k in ORDER) + " %.3f %.4f\n" % (allp["bell"][n] - fit.BASE["bell"], allp["embFloor"][n] - fit.BASE["embFloor"]))
    return allp
def score(label="", curves=True, params=None):
    F = fit.target(); base = dict(fit.BASE, **(params or {})); res = []
    from concurrent.futures import ThreadPoolExecutor
    def one(n):
        f0 = 440*2**((n-69)/12); nh = int(min(16, 9000 // f0)); per = []; bad = 0; cen = []
        for bi, b in enumerate(gap.BREATHS):
            x = wc.render(wc.held(n, b / 127.0, dur=0.6), params=base, cal=False, curves=curves); seg = x[int(.35*48000):int(.62*48000)]
            if np.sqrt(np.mean(seg**2)) < 2e-4: bad += 1; per.append(np.nan); cen.append(np.nan); continue
            f = wc.f0_of(seg, 48000, f0)
            if not np.isfinite(f) or abs(1200*np.log2(f/f0)) > 120: bad += 1; per.append(np.nan); cen.append(np.nan); continue
            h = wc.harmonics(seg, 48000, f, gap.NH); t = fit.reln(F["H"][bi, gap.NOTES.index(n)], nh); c = fit.reln(h, nh)
            per.append(np.minimum(np.abs(np.maximum(t, -55) - np.maximum(c, -55)), 25).mean()); cen.append(gap.centroid(h))
        return per, bad, cen
    with ThreadPoolExecutor(8) as ex: R = list(ex.map(one, gap.NOTES))
    D = np.array([r[0] for r in R]); bad = sum(r[1] for r in R); C = np.array([r[2] for r in R]); FC = np.array([gap.centroid(F["H"][b]) for b in range(3)]).T
    print("%-22s distance to the target, dB: p %.1f  mf %.1f  ff %.1f | by octave (mf) %s | notes that fail to speak at pitch: %d of %d"
          % (label, *np.nanmean(D, axis=0), " ".join("%.1f" % np.nanmean(D[o*12:o*12+12, 1]) for o in range(5)), bad, D.size))
    print("%-22s brightness growth p to ff, by octave: %s   (target %s)" % ("", " ".join("x%.2f" % np.nanmedian(C[o*12:o*12+12, 2] / C[o*12:o*12+12, 0]) for o in range(5)),
          " ".join("x%.2f" % np.nanmedian(FC[o*12:o*12+12, 2] / FC[o*12:o*12+12, 0]) for o in range(5))))
    return D, bad
if __name__ == "__main__":
    wc.build()
    if len(sys.argv) > 1 and sys.argv[1] == "score": score("current curves")
    else:
        score("flat (before)", curves=False)
        for sg in ([float(sys.argv[1])] if len(sys.argv) > 1 else [0.0, 0.7, 1.2]):
            build(sg); score("sigma %.1f anchors" % sg)
