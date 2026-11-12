"""Range check for a setting: over the whole range and three breaths, does every note still speak at its
   pitch, and what changes? usage: robust.py name=v1,v2,... [more=v1,v2 ...] [fixed=value ...]"""
import sys, wc, numpy as np
from concurrent.futures import ThreadPoolExecutor
NOTES = list(range(36, 97, 4)); BREATHS = (0.1, 0.5, 1.0)
def meas(job):
    n, b, params = job
    x = wc.render(wc.held(n, b), params=params); seg = x[int(.6*48000):int(1.1*48000)]; f0 = 440*2**((n-69)/12)
    r = np.sqrt(np.mean(seg**2))
    if r < 3e-4: return None
    f = wc.f0_of(seg, 48000, f0); h = wc.harmonics(seg, 48000, f, 30); a = 10**(h/20); k = np.arange(1, 31)
    spec = abs(np.fft.rfft(seg*np.hanning(len(seg))))**2; fr = np.fft.rfftfreq(len(seg), 1/48000); kk = fr/f
    noise = 10*np.log10(spec[(abs(kk-np.round(kk)) > 0.2) & (fr > 300)].sum()/spec.sum() + 1e-12)
    return wc.db(r), 1200*np.log2(f/f0), (a*k).sum()/a.sum(), noise
def table(params):
    jobs = [(n, b, params) for n in NOTES for b in BREATHS]
    with ThreadPoolExecutor(8) as ex: return dict(zip([(n, b) for n, b, _ in jobs], ex.map(meas, jobs)))
def compare(ref, t):
    silent = wrong = 0; dc = []; dl = []; br = []; nz = []
    for k, m in t.items():
        r = ref[k]
        if r is None: continue
        if m is None: silent += 1; continue
        if abs(m[1] - r[1]) > 70: wrong += 1; continue
        dc.append(m[1]-r[1]); dl.append(m[0]-r[0]); br.append(m[2]/r[2]); nz.append(m[3]-r[3])
    dc = np.array(dc); return ("silent %2d wrong %2d of %d | pitch %+4.0f c median, %3.0f worst | level %+5.1f dB | brightness x%.2f | noise %+4.0f dB"
            % (silent, wrong, len(t), np.median(dc), np.abs(dc).max(), np.median(dl), np.median(br), np.median(nz)))
if __name__ == "__main__":
    wc.build(); axes = {}; fixed = {}
    for a in sys.argv[1:]:
        k, v = a.split("="); (axes if "," in v else fixed)[k] = v.split(",") if "," in v else v
    ref = table(fixed)
    print("default: %d of %d speak" % (sum(v is not None for v in ref.values()), len(ref)))
    for k, vals in axes.items():
        for v in vals: print("%-12s %-7s %s" % (k, v, compare(ref, table(dict(fixed, **{k: v})))))
