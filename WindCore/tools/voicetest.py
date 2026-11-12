"""Random character settings: does every note still speak, in tune and at an even level?
   usage: voicetest.py [count] [spread 0..1]"""
import sys, numpy as np, wc, voice
from concurrent.futures import ThreadPoolExecutor
wc.build()
N = int(sys.argv[1]) if len(sys.argv) > 1 else 12; SPREAD = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
NOTES = list(range(36, 97, 3)); BREATHS = (0.1, 0.5, 1.0)
def meas(job):
    n, b, values, use = job
    x = voice.render(wc.held(n, b), values, use_trim=use); seg = x[int(.6*48000):int(1.1*48000)]; f0 = 440*2**((n-69)/12)
    r = np.sqrt(np.mean(seg**2))
    if r < 3e-4: return None
    return 1200*np.log2(wc.f0_of(seg, 48000, f0)/f0), wc.db(r)
def score(values, use=True):
    jobs = [(n, b, values, use) for n in NOTES for b in BREATHS]
    with ThreadPoolExecutor(8) as ex: res = list(ex.map(meas, jobs))
    silent = sum(m is None for m in res); c = np.array([m[0] for m in res if m]); wrong = int((abs(c) > 70).sum()); c = c[abs(c) <= 70]
    lev = np.array([m[1] for (j, m) in zip(jobs, res) if m and j[1] == 0.5])
    return silent, wrong, np.median(abs(c)), np.percentile(abs(c), 95), abs(c).max(), np.ptp(lev)
if __name__ == "__main__":
    rng = np.random.default_rng(7); T = []
    print("default        : silent %d wrong %d | tuning median %.1f, 95%% %.1f, worst %.1f cents | level spread %.1f dB" % score({}))
    for i in range(N):
        v = {k: float(np.clip(voice.DEFAULT[k] + SPREAD * (rng.random() - voice.DEFAULT[k]), 0, 1)) for k in voice.CONTROLS}
        a = score(v, True); b = score(v, False); T.append(a)
        print("voice %2d       : silent %d wrong %d | tuning median %.1f, 95%% %.1f, worst %.1f cents | level spread %.1f dB   (without trims: 95%% %.1f, worst %.1f, spread %.1f)" % (i + 1, *a, b[3], b[4], b[5]))
    T = np.array(T); print("all voices     : silent %d wrong %d | tuning 95%% typically %.1f, worst seen %.1f cents | level spread typically %.1f dB" % (T[:, 0].sum(), T[:, 1].sum(), np.median(T[:, 3]), T[:, 4].max(), np.median(T[:, 5])))
