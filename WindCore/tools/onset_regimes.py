"""Does a note settle on the same pitch however it starts? Held notes at several breaths, started with and without
   the onset burst and with a fast and a slow breath response; flags notes whose pitch differs by more than 8 cents."""
import sys, wc, voice, numpy as np
from concurrent.futures import ThreadPoolExecutor
BASE = dict(voice.params({}), wobble=0, shimmer=0, drift=0, vibDepth=0, slurDip=0, air=0)
STARTS = [dict(chiff=0, breathAttack=0.004), dict(chiff=1, breathAttack=0.012), dict(chiff=1.5, breathAttack=0.004), dict(chiff=0, breathAttack=0.03)]
def pitch(job):
    n, b, o, p = job
    x = wc.render(wc.held(n, b), params=dict(p, **o)); seg = x[int(.6*48000):int(1.1*48000)]; f0 = 440*2**((n-69)/12)
    return np.nan if np.sqrt(np.mean(seg**2)) < 1e-5 else 1200*np.log2(wc.f0_of(seg, 48000, f0)/f0)
def scan(p=BASE, notes=range(30, 100), breaths=(0.3, 0.6, 0.8, 1.0)):
    jobs = [(n, b, o, p) for n in notes for b in breaths for o in STARTS]
    with ThreadPoolExecutor(8) as ex: r = list(ex.map(pitch, jobs))
    out = {}
    for i in range(0, len(r), len(STARTS)):
        n, b = jobs[i][0], jobs[i][1]; v = np.array(r[i:i + len(STARTS)])
        if np.nanmax(v) - np.nanmin(v) > 8: out.setdefault(n, []).append((b, round(float(np.nanmax(v) - np.nanmin(v)))))
    return out
if __name__ == "__main__":
    wc.build(); bad = scan()
    print("notes whose pitch depends on how they start (breath, spread in cents):", {wc.note_name(n): v for n, v in bad.items()})
