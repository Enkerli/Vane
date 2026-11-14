"""Does the voice tracker find what the test voices were made with? Formants and vowel position against the known
   ones, consonant onsets against the known times, and false onsets on voices with none.
   usage: voicein_check.py [name=value ...]   (v.rise=, v.smooth= and so on are passed to the renderer)"""
import sys, os, tempfile, subprocess, numpy as np, wc, voicein_synth as vs

def pos(f1, f2):
    """Place on the oo..ah..ee line, as VoiceTracker::vowelPosition."""
    A = np.log2([[320, 900], [700, 1200], [300, 2300]]); p = np.log2([f1, f2]); best = (9, .5)
    for i in range(2):
        a, b = A[i], A[i + 1] - A[i]; t = float(np.clip(np.dot(p - a, b) / np.dot(b, b), 0, 1)); d = np.linalg.norm(p - a - t * b)
        if d < best[0]: best = (d, 0.5 * (i + t))
    return best[1]

def track(x, opts=()):
    wc.build(); d = tempfile.mkdtemp(); wav = os.path.join(d, "v.wav"); wc.write_wav16(wav, x, vs.SR)
    ev = os.path.join(d, "e.txt"); open(ev, "w").write("0 breath 0\n%.3f end 0\n" % (len(x) / vs.SR))
    log = os.path.join(d, "log.txt")
    subprocess.check_call([wc.BIN, ev, os.path.join(d, "o.wav"), "--voice", wav, "--voice-log", log] + list(opts))
    fr = np.array([[float(v) for v in l.split()[1:]] for l in open(log) if l[0] == "f"])
    on = np.array([[float(v) for v in l.split()[1:]] for l in open(log) if l[0] == "o"]).reshape(-1, 3)
    return fr, on

def report(name, opts=(), quiet=False, **kw):
    x, truth = vs.make(name, **kw); fr, on = track(x, opts)
    tt = np.arange(len(truth["f1"])) * truth["hop"]; lag = 0.03                       # the tracker's answer is about 30 ms behind
    f1 = np.interp(fr[:, 0] - lag, tt, truth["f1"]); f2 = np.interp(fr[:, 0] - lag, tt, truth["f2"]); lv = np.interp(fr[:, 0] - lag, tt, truth["level"])
    lv2 = np.interp(fr[:, 0] - lag - 0.05, tt, truth["level"])
    m = (lv > 0.3) & (lv2 > 0.3) & (fr[:, 2] > 0)                                       # settled, sounding frames
    e1 = 1200 * np.log2(fr[m, 6] / f1[m]); e2 = 1200 * np.log2(fr[m, 7] / f2[m])
    pt = np.array([pos(a, b) for a, b in zip(f1[m], f2[m])]); ep = fr[m, 8] - pt
    marks = truth["marks"]; used = set(); late = []; missed = []; bright = {}
    for mk in marks:
        c = [i for i in range(len(on)) if i not in used and -0.005 <= on[i, 0] - mk["t"] <= 0.04]
        if not c: missed.append(mk); continue
        i = c[0]; used.add(i); late.append((on[i, 0] - mk["t"]) * 1000); bright.setdefault(mk["what"], []).append(on[i, 1])
    false = len(on) - len(used)
    res = dict(f1_cents_med=float(np.median(abs(e1))), f1_cents_95=float(np.percentile(abs(e1), 95)), f2_cents_med=float(np.median(abs(e2))), f2_cents_95=float(np.percentile(abs(e2), 95)),
               vowel_err_med=float(np.median(abs(ep))), vowel_err_95=float(np.percentile(abs(ep), 95)), frames=int(m.sum()),
               onsets=len(marks), found=len(late), missed=len(missed), false=false, late_ms_med=float(np.median(late)) if late else float("nan"), late_ms_max=float(max(late)) if late else float("nan"),
               bright={k: float(np.mean(v)) for k, v in bright.items()})
    if not quiet:
        print("%-22s formants: F1 off by %3.0f cents (95%%: %4.0f), F2 %3.0f (%4.0f) | vowel position off by %.3f (95%%: %.3f) of the 0..1 line, %d frames"
              % (name + ("/whisper" if kw.get("whisper") else "") + ("/f0=%d" % kw["f0"] if "f0" in kw else ""), res["f1_cents_med"], res["f1_cents_95"], res["f2_cents_med"], res["f2_cents_95"], res["vowel_err_med"], res["vowel_err_95"], res["frames"]))
        print("%-22s onsets: %d of %d found, %d false | late by %.1f ms median, %.1f ms at most | brightness %s"
              % ("", res["found"], res["onsets"], res["false"], res["late_ms_med"], res["late_ms_max"], " ".join("%s %.2f" % kv for kv in sorted(res["bright"].items()))))
    return res

if __name__ == "__main__":
    opts = sys.argv[1:]
    for name in vs.SETS: report(name, opts)
    for name in ("oo-ah-ee", "ta-ka-pa"): report(name, opts, whisper=True)
    for f0 in (90, 160, 220, 300): report("phrase", opts, f0=f0)
    for seed in (2, 3): report("ta-ka-pa", opts, seed=seed, floor_db=-45.0)
