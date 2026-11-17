"""How close is Vane's played pitch to a reference rendering of the same performance (a singing-voice editor's
   output, in tune and in sync with the voice)? Pitch tracks of both, aligned, compared where both sound.
   usage: voicein_vs_render.py vane_output.wav reference_render.wav"""
import sys, numpy as np, wc

def track(x, r, hop=0.01, lo=40, hi=800):
    """Pitch (MIDI note with fraction) every `hop` seconds, NaN where not clearly pitched."""
    n = int(0.046 * r); h = int(hop * r); out = []
    for i in range(0, len(x) - n, h):
        s = x[i:i + n] - np.mean(x[i:i + n])
        if np.sqrt(np.mean(s ** 2)) < 2e-3: out.append(np.nan); continue
        S = np.fft.rfft(s * np.hanning(n), 4 * n); ac = np.fft.irfft(abs(S) ** 2)[:n]; ac /= ac[0] + 1e-12
        a, b = int(r / hi), int(r / lo); k = a + int(np.argmax(ac[a:b]))
        if ac[k] < 0.6: out.append(np.nan); continue
        # prefer the shortest lag nearly as strong (octave safety)
        for d in (2, 3):
            kk = int(round(k / d))
            if kk > a and ac[kk - 1:kk + 2].max() > 0.85 * ac[k]: k = kk - 1 + int(np.argmax(ac[kk - 1:kk + 2])); break
        y0, y1, y2 = ac[k - 1], ac[k], ac[k + 1]; k = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
        out.append(69 + 12 * np.log2(r / k / 440))
    return np.array(out)

def compare(a, b, hop=0.01, max_lag=1.0):
    """Best lag (in hops) of b against a, then |a - b| in cents where both are pitched, with octaves folded."""
    best, bl = None, 0
    for L in range(-int(max_lag / hop), int(max_lag / hop) + 1):
        aa = a[max(0, L):len(a) + min(0, L)]; bb = b[max(0, -L):len(b) - max(0, L)]; m = min(len(aa), len(bb))
        d = aa[:m] - bb[:m]; ok = ~np.isnan(d)
        if ok.sum() < 50: continue
        e = np.median(abs(d[ok]))
        if best is None or e < best: best, bl = e, L
    L = bl; aa = a[max(0, L):len(a) + min(0, L)]; bb = b[max(0, -L):len(b) - max(0, L)]; m = min(len(aa), len(bb)); d = aa[:m] - bb[:m]
    ok = ~np.isnan(d); c = 100 * d[ok]
    return dict(lag=L * hop, median=float(np.median(abs(c))), within50=float(np.mean(abs(c) < 50)), within100=float(np.mean(abs(c) < 100)),
                both=float(ok.mean()), steady=float(np.median(abs(np.diff(aa[:m][ok])))) * 100)

if __name__ == "__main__":
    x, r = wc.read_wav(sys.argv[1]); y, ry = wc.read_wav(sys.argv[2])
    res = compare(track(x, r), track(y, ry))
    print("against the reference render (lag %+.2f s): pitch differs by %.0f cents median; within 50 cents %.0f%%, within 100 %.0f%% of the time both sound"
          % (res["lag"], res["median"], 100 * res["within50"], 100 * res["within100"]))
