"""Does the voice's pitch give the right notes? Sung test melodies with known notes (voicein_synth.py) through
   VoiceTracker and VoiceNotes: pitch error, notes found, notes invented, and how late each note starts.
   usage: voicein_notes_check.py [name=value ...]   (v.* options are passed to the renderer)"""
import sys, os, tempfile, subprocess, numpy as np, wc, voicein_synth as vs

def melody(kind, base):
    N = lambda *k: [base + i for i in k]
    if kind == "legato scale":   return [("v", "ah", 0.3)] + sum([[("p", n), ("v", "ah", 0.32)] for n in N(2, 4, 5, 7, 5, 4, 2, 0)], [])
    if kind == "tongued scale":  return sum([[("c", "tkp"[i % 3], 0.06), ("p", n, 0.001), ("v", "ah", 0.24)] for i, n in enumerate(N(0, 2, 4, 5, 7, 5, 4, 2, 0))], [])
    if kind == "vowels on a line": return sum([[("p", n), ("g", v, 0.1), ("v", v, 0.26)] for n, v in zip(N(0, 4, 7, 4, 0, 5, 9, 5), ["ah", "ee", "oo", "eh", "ah", "oh", "ee", "ah"])], [("v", "ah", 0.2)])
    if kind == "leaps":          return [("v", "ah", 0.3)] + sum([[("p", n), ("v", "ah", 0.4)] for n in N(12, 0, 7, 12, 0)], [])
    if kind == "slow slide":     return [("v", "ah", 0.4), ("p", base + 5, 1.0), ("v", "ah", 1.3), ("p", base, 0.8), ("v", "ah", 1.2)]
    if kind == "held with vibrato": return [("v", "ah", 1.2), ("p", base + 3), ("v", "ah", 1.2), ("r", 0.2), ("v", "ee", 1.0)]
    raise KeyError(kind)

def run(x, opts=()):
    wc.build(); d = tempfile.mkdtemp(); wav = os.path.join(d, "v.wav"); wc.write_wav16(wav, x, vs.SR)
    ev = os.path.join(d, "e.txt"); open(ev, "w").write("0 breath 0.7\n%.3f end 0\n" % (len(x) / vs.SR)); log = os.path.join(d, "log.txt")
    subprocess.check_call([wc.BIN, ev, os.path.join(d, "o.wav"), "--voice", wav, "--voice-log", log, "v.notes=1"] + list(opts))
    fr = np.array([[float(v) for v in l.split()[1:]] for l in open(log) if l[0] == "f"])
    on = [(float(l.split()[1]), int(l.split()[3])) for l in open(log) if l.startswith("n ") and " on " in l]
    return fr, on, wc.read_wav(os.path.join(d, "o.wav"))[0]

def report(kind, base, opts=(), quiet=False, vib=0.0, seed=1):
    x, truth = vs.build(melody(kind, base), f0=440 * 2 ** ((base - 69) / 12), vib=vib, seed=seed)
    fr, on, out = run(x, opts); tt = np.arange(len(truth["note"])) * truth["hop"]
    # pitch error on settled, sounding frames
    lag = 0.03; tn = np.interp(fr[:, 0] - lag, tt, truth["note"]); tn2 = np.interp(fr[:, 0] - lag - 0.06, tt, truth["note"]); lv = np.interp(fr[:, 0] - lag, tt, truth["level"])
    m = (lv > 0.3) & (abs(tn - tn2) < 0.05) & (fr[:, 12] > 0); cents = 100 * (fr[m, 12] - tn[m])
    octave = int((abs(cents) > 600).sum()); cents = cents[abs(cents) <= 600]
    voiced_ok = float(((lv > 0.5) & (fr[:, 12] > 0)).sum() / max(1, (lv > 0.5).sum()))
    # the notes meant: each new pitch target once it sounds; plus re-starts after rests
    want = []
    for tm, nt, sl in truth["pitch_marks"]:
        ts = tm
        while ts < tt[-1] and np.interp(ts, tt, truth["level"]) < 0.2: ts += 0.005          # it starts when the voice does
        n = int(round(nt))
        if not want or want[-1][1] != n: want.append((ts, n))
    used = set(); late = []
    for ts, n in want:
        c = [i for i, (to, no) in enumerate(on) if i not in used and no == n and -0.03 <= to - ts <= 0.4]
        if c: used.add(c[0]); late.append((on[c[0]][0] - ts) * 1000)
    extra = len(on) - len(used); slide = kind == "slow slide"
    res = dict(kind=kind, base=base, cents_med=float(np.median(abs(cents))) if len(cents) else float("nan"), cents_95=float(np.percentile(abs(cents), 95)) if len(cents) else float("nan"),
               octave_frames=octave, voiced=voiced_ok, want=len(want), found=len(late), extra=extra, late_med=float(np.median(late)) if late else float("nan"), late_max=float(max(late)) if late else float("nan"), notes=[n for _, n in on])
    if not quiet:
        print("%-18s from %s%s: pitch off by %4.1f cents (95%%: %4.1f), %d octave-error frames, voiced %3.0f%% | notes %d of %d found, %d extra%s | late %3.0f ms median, %3.0f at most"
              % (kind, wc_note(base), " vib" if vib else "", res["cents_med"], res["cents_95"], octave, 100 * voiced_ok, res["found"], res["want"], extra, " (a slide: steps on the way are expected)" if slide else "", res["late_med"], res["late_max"]))
    return res

def wc_note(n): return ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"][n % 12] + str(n // 12 - 1)

if __name__ == "__main__":
    opts = sys.argv[1:]
    for base in (36, 43, 48, 55, 60):
        for kind in ("legato scale", "tongued scale", "vowels on a line", "leaps"): report(kind, base, opts)
    for base in (40, 52): report("held with vibrato", base, opts, vib=0.03); report("slow slide", base, opts)
