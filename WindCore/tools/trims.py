"""Measure, for each character control at its two ends (and the mouth at several positions), how far each
   note goes out of tune and changes level, and store the corrections in ../trims.json."""
import json, sys, numpy as np, wc, voice
from concurrent.futures import ThreadPoolExecutor
LO, HI, B = 30, 100, 0.5
STILL = dict(wobble=0, shimmer=0, drift=0, vibDepth=0, slurDip=0, air=0)     # measure tuning on a still player
wc.build()
def meas(job):
    n, values, T = job
    x = voice.render(wc.held(n, B), values, T=T, extra=STILL); seg = x[int(.6*48000):int(1.1*48000)]; f0 = 440*2**((n-69)/12)
    r = np.sqrt(np.mean(seg**2))
    if r < 1e-5: return None
    return 1200*np.log2(wc.f0_of(seg, 48000, f0)/f0), wc.db(r)
def table(values, T):
    with ThreadPoolExecutor(8) as ex: return list(ex.map(meas, [(n, values, T) for n in range(LO, HI + 1)]))
anchors = {}
for k, c in voice.CONTROLS.items():
    if c.get("trim") is False or k in ("mouth", "mouthDepth"): continue
    anchors[k + ":0"] = {k: 0.0}; anchors[k + ":1"] = {k: 1.0}
for f in voice.MOUTH_ANCHORS:
    anchors["mouth@%d" % f] = {"mouth": float(voice.to_pos(voice.CONTROLS["mouth"], f)), "mouthDepth": 1.0}
# the mouth tables are deltas from a neutral mouth, so measure the reference with it
ref = table({"mouthDepth": 0.0}, {})
T = json.load(open(voice.TRIMS)) if '--resume' in sys.argv else {}
for key, values in anchors.items():
    if key in T: continue
    vals = dict({"mouthDepth": 0.0}, **values)
    cents = np.zeros(128); gain = np.zeros(128)
    for it in range(2):
        Tt = dict(T); Tt[key] = {"cents": cents.tolist(), "gain": gain.tolist()}
        # apply this anchor's own table at full weight during the measurement
        res = table(vals, {key: Tt[key]} if key.startswith("mouth@") else {key: Tt[key]})
        errs = []
        for i, n in enumerate(range(LO, HI + 1)):
            m, r = res[i], ref[i]
            if m is None or r is None or abs(m[0] - r[0]) > 150: continue
            cents[n] -= m[0] - r[0]; gain[n] -= m[1] - r[1]; errs.append(abs(m[0] - r[0]))
        cents[:LO] = cents[LO]; cents[HI + 1:] = cents[HI]; gain[:LO] = gain[LO]; gain[HI + 1:] = gain[HI]
    T[key] = {"cents": np.round(cents, 2).tolist(), "gain": np.round(gain, 2).tolist()}
    print("%-12s correction up to %5.1f cents, %4.1f dB; left after one pass: %4.1f cents worst (%d notes)" % (key, np.abs(cents).max(), np.abs(gain).max(), max(errs), len(errs)))
    json.dump(T, open(voice.TRIMS, "w"))
