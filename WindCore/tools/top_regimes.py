"""Transitions into the top octave (C6 to Eb7): slurs and tongued re-attacks from several notes, gaps and breaths.
   Counts how many land in another regime than the same note started on its own. usage: top_regimes.py name=v1,v2 ..."""
import sys, wc, voice, numpy as np
from concurrent.futures import ThreadPoolExecutor
BASE = dict(voice.params({}), wobble=0, shimmer=0, drift=0, vibDepth=0, slurDip=0, air=0)
def cents(ev, lo, p, cal):
    x = wc.render(ev, params=p, cal=cal); f0 = 440 * 2 ** ((lo - 69) / 12); t0 = [t for t, k, v in ev if k == "on" and v == lo][-1]
    seg = x[int((t0 + 0.25) * 48000):int((t0 + 0.4) * 48000)]
    return np.nan if np.sqrt(np.mean(seg ** 2)) < 1e-5 else 1200 * np.log2(wc.f0_of(seg, 48000, f0) / f0)
def battery(p, cal=False, lo_hi=(84, 99)):
    jobs = []
    for lo in range(lo_hi[0], lo_hi[1] + 1):
        jobs.append(("ref", lo, [(0.05, "breath", 0.0), (0.05, "on", lo), (0.08, "breath", 0.7), (0.8, "breath", 0.7), (0.83, "breath", 0), (0.83, "off", lo), (1.1, "end", 0)]))
        for prev in (lo - 5, lo - 2, lo + 2, 99):
            if prev == lo or prev > 99: continue
            for gap in (0.0, 0.1, 0.3, 0.5):
                for b in (0.7, 1.0):
                    if gap == 0: ev = [(0.05, "breath", 0.0), (0.05, "on", prev), (0.08, "breath", b), (0.7, "on", lo), (0.701, "off", prev), (1.3, "breath", b), (1.33, "breath", 0), (1.33, "off", lo), (1.6, "end", 0)]
                    else: ev = [(0.05, "breath", 0.0), (0.05, "on", prev), (0.08, "breath", 0.73), (0.7, "breath", 0.73), (0.72, "breath", 0.0), (0.72, "off", prev), (0.72 + gap, "on", lo), (0.7201 + gap, "breath", b), (1.3 + gap, "breath", b), (1.33 + gap, "breath", 0), (1.33 + gap, "off", lo), (1.6 + gap, "end", 0)]
                    jobs.append((prev, lo, ev))
    with ThreadPoolExecutor(8) as ex: res = list(ex.map(lambda j: (j[0], j[1], cents(j[2], j[1], p, cal)), jobs))
    ref = {lo: c for k, lo, c in res if k == "ref"}
    bad = [(prev, lo) for prev, lo, c in res if prev != "ref" and (not np.isfinite(c) or abs(c - ref[lo]) > 30)]
    return len(bad), len(res) - len(ref), sorted(set(wc.note_name(lo) for _, lo in bad))
if __name__ == "__main__":
    wc.build(); k, vals = sys.argv[1].split("=")
    for v in vals.split(","):
        n, tot, where = battery(dict(BASE, **{k: float(v)})); print("%s=%s: %d of %d transitions land in another regime %s" % (k, v, n, tot, where), flush=True)
