"""Regime map: which register a held note settles in, against mouth pressure and one other setting."""
import sys, wc, numpy as np
def regime(n, params, ramp=0.03):
    x = wc.render(wc.held(n, 1.0, ramp=ramp), cal=False, params=params)
    seg = x[int(.7*48000):int(1.15*48000)]
    r = np.sqrt(np.mean(seg**2))
    if r < 1e-4: return "."
    f0 = 440*2**((n-69)/12)
    h = wc.harmonics(seg, 48000, wc.f0_of(seg,48000,f0), 8)
    # sub-harmonic or noise: check the strongest harmonic number
    k = int(np.argmax(h)) + 1
    if h[0] > h.max() - 25: return "1"
    return str(k) if k < 10 else "x"
if __name__ == "__main__":
    notes = [38, 45, 50, 57, 62, 69, 74, 81, 88, 96]
    gammas = [0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.7, 0.8, 0.9]
    base = dict(a.split("=") for a in sys.argv[1:])
    ramp = float(base.pop("ramp", 0.03))
    print("gamma " + " ".join("%4.2f" % g for g in gammas), base)
    for n in notes:
        print("%-5s " % wc.note_name(n) + "    ".join(regime(n, dict(base, breathFloor=g, breathTop=g), ramp) for g in gammas))
