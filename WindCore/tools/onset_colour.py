"""Tongued notes across the range: the first 30 ms against the settled note (brightness ratio, noise share,
   energy above 3 kHz), and how much the start overshoots. usage: onset_colour.py "name=v ... / name=v ..." """
import sys, wc, voice, numpy as np
R = 48000
def phrase():
    ev = []; t = 0.1
    for n in list(range(45, 86, 4)) * 2:
        ev += [(t, "breath", 0.0), (t, "on", n), (t + 0.02, "breath", 0.7), (t + 0.32, "breath", 0.7), (t + 0.34, "breath", 0.0), (t + 0.34, "off", n)]; t += 0.5
    return ev + [(t + 0.3, "end", 0)], [(0.1 + 0.5 * i, n) for i, n in enumerate(list(range(45, 86, 4)) * 2)]
def stats(x, starts):
    out = []
    for t, n in starts:
        a = int(t * R); f0 = 440 * 2 ** ((n - 69) / 12); env = np.sqrt(np.convolve(x[a:a + int(.15 * R)] ** 2, np.ones(96) / 96, "same"))
        i0 = int(np.argmax(env > 0.1 * env.max())); early = x[a + i0:a + i0 + int(.03 * R)]; steady = x[a + int(.15 * R):a + int(.3 * R)]
        sp = lambda s: (abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2, np.fft.rfftfreq(len(s), 1 / R))
        e, fe = sp(early); s, fs = sp(steady[:len(early) * 4]); cen = lambda p, f: (p * f).sum() / p.sum(); k = fe / f0
        out.append((cen(e, fe) / cen(s, fs), 10 * np.log10(e[(abs(k - np.round(k)) > 0.3) & (fe > 300)].sum() / e.sum() + 1e-12),
                    10 * np.log10(e[fe > 3000].sum() / e.sum() + 1e-12) - 10 * np.log10(s[fs > 3000].sum() / s.sum() + 1e-12),
                    env.max() / (np.sqrt(np.mean(steady ** 2)) * np.sqrt(2) + 1e-9)))
    return np.median(np.array(out), axis=0)
if __name__ == "__main__":
    wc.build(); ev, starts = phrase()
    print("%-44s %8s %10s %12s %10s" % ("", "bright x", "noise dB", ">3 kHz dB", "overshoot"))
    print("%-44s %8s %10s %12s %10s" % ("saxophones in the collection (few notes)", "0.7-1.1", "-28..-8", "-2..+2", ""))
    for chunk in " ".join(sys.argv[1:]).split("/"):
        p = {kv.split("=")[0]: float(kv.split("=")[1]) for kv in chunk.split()}
        x = wc.render(ev, params=dict(voice.params({}), wobble=0, shimmer=0, drift=0, vibDepth=0, slurDip=0, **p))
        print("%-44s %8.2f %10.1f %12.1f %10.2f" % ((chunk.strip() or "defaults")[:44], *stats(x, starts)))
