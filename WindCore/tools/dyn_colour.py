"""How the tone follows breath on held notes: brightness ratio and level change from breath 0.25 to 1.0
   (two doublings), per range, plus notes in the wrong register. usage: dyn_colour.py "name=v ... / ..." """
import sys, wc, voice, numpy as np
from concurrent.futures import ThreadPoolExecutor
BASE = dict(voice.params({}), wobble=0, shimmer=0, drift=0, vibDepth=0, slurDip=0, air=0, chiff=0, keyNoise=0, bloom=0)
RANGES = [("low", range(38, 50, 2)), ("tenor core", range(50, 71, 2)), ("upper", range(71, 83, 2)), ("top", range(83, 97, 3))]
def meas(job):
    n, b, p = job
    x = wc.render(wc.held(n, b, dur=0.7), params=p); seg = x[int(.35*48000):int(.7*48000)]; f0 = 440*2**((n-69)/12)
    r = np.sqrt(np.mean(seg**2))
    if r < 3e-4: return None
    f = wc.f0_of(seg, 48000, f0); h = wc.harmonics(seg, 48000, f, 30); a = 10**(h/20); k = np.arange(1, 31)
    return (a*k).sum()/a.sum(), wc.db(r), 1200*np.log2(f/f0)
def run(p):
    out = []
    for name, notes in RANGES:
        jobs = [(n, b, p) for n in notes for b in (0.25, 1.0)]
        with ThreadPoolExecutor(8) as ex: r = list(ex.map(meas, jobs))
        br = []; lv = []; wrong = 0
        for i in range(0, len(r), 2):
            s, l = r[i], r[i + 1]
            if s is None or l is None or abs(s[2]) > 60 or abs(l[2]) > 60: wrong += 1; continue
            br.append(l[0] / s[0]); lv.append(l[1] - s[1])
        out.append("%s x%.2f %+.0f dB%s" % (name, np.sqrt(np.median(br)), np.median(lv) / 2, " (%d wrong)" % wrong if wrong else ""))
    return " | ".join(out)
if __name__ == "__main__":
    wc.build(); print("per doubling of breath: brightness, level   (sax models in the tenor core: x1.3-1.5, +9..+16 dB)")
    for chunk in " ".join(sys.argv[1:]).split("/"):
        p = {kv.split("=")[0]: float(kv.split("=")[1]) for kv in chunk.split()}
        print("%-40s %s" % ((chunk.strip() or "defaults")[:40], run(dict(BASE, **p))), flush=True)
