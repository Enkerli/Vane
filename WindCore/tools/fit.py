import warnings; warnings.filterwarnings("ignore")
"""Fit the horn's settings, note by note, toward a measured target (harmonics at three dynamics).
   usage: fit.py 40,52,64 [maxiter]      prints the fitted settings and the distance left; saves ../build/fit/<note>.json"""
import sys, os, json, glob, numpy as np, wc, voice, gap
from scipy.optimize import differential_evolution
BASE = dict(dict(lossRatio=8.0, embTrack=0.25, embFloor=0.0, boreLoss=0.99), **dict(voice.params({}), air=0.0, lossHz=16000.0))
# name, low, high, log scale
FREE = [("cutoffK", 60, 1500, True), ("lossRatio", 2, 80, True), ("coneBeta", 0.12, 0.42, False), ("chamber", 1.0, 2.2, False), ("baffle", 1.0, 12.0, True),
        ("aperture", 0.6, 2.6, True), ("lay", 0.004, 0.35, True), ("embTrack", 0.0, 1.5, False), ("embFloor", 0.0, 0.6, False), ("bell", -6.0, 22.0, False), ("boreLoss", 0.90, 0.995, False)]
TARGET = os.environ.get("WIND_TARGET", "")     # path to a target: an .npz with H[3 breaths, notes, harmonics] in dB (see gap.py)
OUT = os.path.join(wc.ROOT, "build", "fit")
def target():
    if not TARGET: sys.exit("set WIND_TARGET to a target file (.npz with H shaped [3, notes, harmonics], notes and breaths as in gap.py)")
    return dict(np.load(TARGET))
def reln(h, nh):
    h = np.maximum(np.array(h[:nh], float), -200.0); return h - 10 * np.log10(np.sum(10 ** (h / 10)))
def decode(u): return {n: float(lo * (hi / lo) ** x if lg else lo + (hi - lo) * x) for (n, lo, hi, lg), x in zip(FREE, u)}
def encode(p): return [float(np.clip(np.log(p.get(n, BASE[n]) / lo) / np.log(hi / lo) if lg else (p.get(n, BASE[n]) - lo) / (hi - lo), 0, 1)) for n, lo, hi, lg in FREE]
SOFT = 0.06 * 127      # the softest breath that must still speak, at pitch
def model(n, params):
    out = []
    for b in tuple(gap.BREATHS) + (SOFT, 127.0, 0.76 * 127):      # three for the sound, then soft, full and one between for playability
        x = wc.render(wc.held(n, b / 127.0, dur=0.6), params=dict(BASE, **params), cal=False, curves=False); seg = x[int(.35*48000):int(.62*48000)]; f0 = 440*2**((n-69)/12)
        r = np.sqrt(np.mean(seg**2))
        if r < 2e-4: out.append(None); continue
        f = wc.f0_of(seg, 48000, f0)
        if not np.isfinite(f) or f <= 0 or not np.all(np.isfinite(seg)): out.append(None); continue
        out.append((1200*np.log2(f/f0), wc.harmonics(seg, 48000, f, gap.NH), wc.db(r)))
    return out
def cost(u, n, target, detail=False, near=None, pull=0.0):
    m = model(n, decode(u)); f0 = 440*2**((n-69)/12); nh = int(min(16, 9000 // f0)); tot = 0; per = []
    for bi in range(3):
        if m[bi] is None: tot += 40; per.append(40.0); continue
        t = reln(target[bi], nh); c = reln(m[bi][1], nh)
        e = np.minimum(np.abs(np.maximum(t, -55) - np.maximum(c, -55)), 25)
        w = np.where(np.arange(nh) < 4, 3.0, np.where(np.arange(nh) < 6, 1.5, 1.0))    # the weight of a sound is in its lowest harmonics
        e = float(np.sum(w * e) / np.sum(w))
        if abs(m[bi][0]) > 120: e += 30
        if not np.isfinite(e): e = 40.0
        per.append(float(e)); tot += e
    # playability: the softest breath speaks at pitch, not more than 40 dB under ff; pitch holds from soft to loud
    if m[3] is None or abs(m[3][0]) > 120 or (m[2] is not None and m[3][2] < m[2][2] - 40): tot += 60
    elif m[2] is not None: tot += 0.15 * max(0.0, abs(m[3][0] - m[2][0]) - 25)
    # pitch must move smoothly with breath: no jump between neighbouring breaths (a sign of a second regime nearby)
    cs = [m[i][0] if m[i] is not None else None for i in (3, 0, 1, 5, 2, 4)]     # soft to full
    if any(c is None for c in cs): tot += 60
    else: tot += 0.6 * sum(max(0.0, abs(a - b) - 15) for a, b in zip(cs, cs[1:]))
    if near is not None and not detail: tot += 3 * pull * float(np.mean(np.abs(np.array(u) - near)))   # stay close to the neighbours
    return (tot / 3, per, m) if detail else tot / 3
def fit(n, target, maxiter=18, seed=1, x0=None, near=None, pull=0.0):
    init = "sobol" if x0 is None else np.clip(np.array(x0)[None, :] + np.random.default_rng(seed).normal(0, 0.12, (40, len(FREE))), 0, 1)
    r = differential_evolution(cost, [(0, 1)] * len(FREE), args=(n, target, False, near, pull), maxiter=maxiter, popsize=4, tol=1e-3, seed=seed, workers=8, updating="deferred", polish=False, init=init)
    return r.x, r.fun
if __name__ == "__main__":
    wc.build(); os.makedirs(OUT, exist_ok=True); F = target()
    if sys.argv[1] == "smooth":
        # refit every anchor starting from the average of its neighbours, with a pull toward it: fewer bumps along the range
        pull = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0
        files = sorted(glob.glob(os.path.join(OUT, "*.json")), key=lambda f: int(os.path.basename(f)[:-5])); D = [json.load(open(f)) for f in files]
        U = np.array([encode(d["params"]) for d in D]); new = []
        for i, d in enumerate(D):
            nb = [U[j] for j in (i - 1, i + 1) if 0 <= j < len(D)]; near = np.mean(nb, axis=0); n = d["note"]; T = F["H"][:, gap.NOTES.index(n)]
            u, f = fit(n, T, 8, x0=near, near=near, pull=pull); u0 = U[i]
            c_new, per, m = cost(u, n, T, True); c_old = cost(u0, n, T, True)[0]
            dev_new, dev_old = np.mean(np.abs(u - near)), np.mean(np.abs(u0 - near))
            keep_old = c_old + pull * dev_old <= c_new + pull * dev_new
            if keep_old: u, c_new = u0, c_old
            new.append(dict(note=n, params=decode(u), u=[float(v) for v in u], distance=c_new, before=d["before"]))
            print("%-4s cost %.1f -> %.1f, distance from neighbours %.2f -> %.2f%s" % (wc.note_name(n), c_old, c_new, dev_old, min(dev_new, dev_old) if not keep_old else dev_old, "  (kept)" if keep_old else ""), flush=True)
        for f, d in zip(files, new): json.dump(d, open(f, "w"))
        sys.exit(0)
    notes = [int(v) for v in sys.argv[1].split(",")]; it = int(sys.argv[2]) if len(sys.argv) > 2 else 18; prev = None
    for n in notes:
        T = F["H"][:, gap.NOTES.index(n)]; d0, per0, _ = cost(encode(BASE), n, T, True); old = os.path.join(OUT, "%d.json" % n)
        starts = ([encode(dict(json.load(open(old))["params"], embFloor=0.25))] if os.path.exists(old) else []) + ([prev] if prev is not None else [])
        u, f = fit(n, T, it)                                  # a fresh search
        for x0 in starts:                                     # and from the earlier fit and the neighbour's answer; keep the best
            u2, f2 = fit(n, T, max(6, it // 2), x0=x0)
            if f2 < f: u, f = u2, f2
        prev = u; d, per, m = cost(u, n, T, True); p = decode(u)
        json.dump(dict(note=n, params=p, u=[float(v) for v in u], distance=d, before=d0, per=per), open(old, "w"))
        soft = "silent" if m[3] is None else "%.0f dB, %+.0f c" % (m[3][2] - (m[2][2] if m[2] else 0), m[3][0])
        print("%-4s cost %4.1f -> %4.1f (p %.1f mf %.1f ff %.1f; soft breath: %s)  " % (wc.note_name(n), d0, d, *per, soft) + " ".join("%s=%.3g" % kv for kv in p.items()), flush=True)
