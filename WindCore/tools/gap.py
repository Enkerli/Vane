"""Shared constants for fitting, and held notes measured through Vane (any Waveguide model), note by note.
   A target for fit.py is an .npz with H shaped [3 breaths, notes, harmonics] in dB, for NOTES and BREATHS below."""
import os, subprocess, numpy as np, wc
VANE = os.path.dirname(wc.ROOT)
PR = os.path.join(VANE, "build/VanePerfRender_artefacts/Release/VanePerfRender")
NOTES = list(range(36, 97)); BREATHS = (48, 80, 112); DUR, GAP, NH = 0.8, 0.25, 20
CACHE = os.path.join(wc.ROOT, "build", "gap")
def perf(path, vane_set=""):
    t = 0.2; marks = []
    with open(path, "w") as f:
        for kv in vane_set.split(";"):
            if "=" in kv: f.write("0 set %s %s\n" % tuple(kv.split("=")))
        for b in BREATHS:
            for n in NOTES:
                f.write("%.4f cc 2 0\n%.4f on %d 100\n%.4f cc 2 %d\n" % (t, t + 0.01, n, t + 0.03, b))
                f.write("%.4f cc 2 %d\n%.4f cc 2 0\n%.4f off %d\n" % (t + DUR, b, t + DUR + 0.03, t + DUR + 0.04, n))
                marks.append((n, b, t)); t += DUR + GAP
    return marks, t + 0.5
def measure(x, r, marks):
    H = np.full((len(BREATHS), len(NOTES), NH), -200.0); LV = np.zeros((len(BREATHS), len(NOTES)))
    for n, b, t in marks:
        seg = x[int((t + 0.35) * r):int((t + DUR - 0.05) * r)]; f0 = 440 * 2 ** ((n - 69) / 12)
        bi, ni = BREATHS.index(b), NOTES.index(n); rms = np.sqrt(np.mean(seg ** 2)); LV[bi, ni] = wc.db(rms)
        if rms < 1e-5: continue
        f = wc.f0_of(seg, r, f0)
        H[bi, ni] = wc.harmonics(seg, r, f if abs(1200 * np.log2(f / f0)) < 80 else f0, NH)
    return dict(H=H, level=LV)
def profile(name, vane_set, force=False):
    """Held notes through Vane with these settings ("id=value;id=value"), cached in build/gap/<name>.npz."""
    os.makedirs(CACHE, exist_ok=True); c = os.path.join(CACHE, name + ".npz")
    if os.path.exists(c) and not force: return dict(np.load(c))
    pf = os.path.join(CACHE, name + ".txt"); wav = os.path.join(CACHE, name + ".wav"); marks, total = perf(pf, vane_set)
    subprocess.check_call([PR, pf, wav, "48000", "%.1f" % total], stdout=subprocess.DEVNULL)
    x, r = wc.read_wav(wav); res = measure(x, r, marks); np.savez(c, **res); os.unlink(wav); return res
def rel(H): return H - 10 * np.log10(np.sum(10 ** (H / 10), axis=-1, keepdims=True))
def centroid(H):
    k = np.arange(1, NH + 1); a = np.where(H > -150, 10 ** (H / 20), 0); return (a * k).sum(-1) / a.sum(-1)
