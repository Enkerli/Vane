"""Our notes from a real recording against a reference transcription of the same recording (a MIDI file, e.g.
   from a singing-voice editor). Finds the time offset between the two, then scores: notes found, pitch right,
   onset lateness, notes invented, and how far the played pitch strays from each reference note while it lasts.
   usage: voicein_vs_reference.py voice.wav reference.mid [v.name=value ...]"""
import sys, os, tempfile, subprocess, numpy as np, mido, wc

def reference(path):
    t, on, out = 0.0, {}, []
    for m in mido.MidiFile(path):
        t += m.time
        if m.type == "note_on" and m.velocity > 0: on[m.note] = t
        elif m.type in ("note_off", "note_on") and m.note in on: t0 = on.pop(m.note); out.append((t0, t - t0, m.note))
    return sorted(out)

def ours(wav, opts=()):
    x, r = wc.read_wav(wav); d = tempfile.mkdtemp(); ev = os.path.join(d, "e.txt"); open(ev, "w").write("0 breath 0.7\n%.3f end 0\n" % (len(x) / r))
    log = os.path.join(d, "l.txt"); wc.build()
    subprocess.check_call([wc.BIN, ev, os.path.join(d, "o.wav"), "--voice", wav, "--voice-log", log, "v.notes=1"] + list(opts))
    fr = np.array([[float(v) for v in l.split()[1:]] for l in open(log) if l[0] == "f"])
    on = [(float(l.split()[1]), int(l.split()[3])) for l in open(log) if l.startswith("n ") and " on " in l]
    off = [(float(l.split()[1]), int(l.split()[3])) for l in open(log) if l.startswith("n ") and " off " in l]
    return fr, on, off

def score(ref, on, off, fr, shift, quiet=False):
    used = set(); late = []; wrong = 0; found = 0
    for t0, dur, n in ref:
        t = t0 + shift
        c = [i for i, (to, no) in enumerate(on) if i not in used and -0.08 <= to - t <= 0.35]
        if not c: continue
        i = min(c, key=lambda i: abs(on[i][0] - t)); used.add(i)
        if on[i][1] == n: found += 1; late.append((on[i][0] - t) * 1000)
        elif abs(on[i][1] - n) == 12: wrong += 1
        else: wrong += 1
    # the pitch the voice tracker heard (what the bend follows) against each reference note, in its middle 80%
    stray = []
    for t0, dur, n in ref:
        a, b = t0 + shift + 0.1 * dur, t0 + shift + 0.9 * dur
        m = (fr[:, 0] >= a) & (fr[:, 0] <= b) & (fr[:, 12] > 0)
        if m.sum(): stray.append(np.median(abs(fr[m, 12] - n)) * 100)
    res = dict(ref=len(ref), found=found, wrong=wrong, extra=len(on) - len(used), late=float(np.median(late)) if late else float("nan"),
               late_max=float(max(late)) if late else float("nan"), stray=float(np.median(stray)) if stray else float("nan"))
    if not quiet:
        print("notes: %d of %d right, %d wrong pitch, %d extra | onset late by %.0f ms median (%.0f at most) | sung pitch strays %.0f cents (median) from the reference note"
              % (found, len(ref), wrong, res["extra"], res["late"], res["late_max"], res["stray"]))
    return res

if __name__ == "__main__":
    wav, mid = sys.argv[1], sys.argv[2]; opts = sys.argv[3:]
    ref = reference(mid); fr, on, off = ours(wav, opts)
    best = max(np.arange(-1.5, 1.5, 0.01), key=lambda s: (lambda r: r["found"] * 10 - r["extra"] - r["wrong"])(score(ref, on, off, fr, s, quiet=True)))
    print("offset of the reference: %+.2f s; our notes: %d" % (best, len(on)))
    score(ref, on, off, fr, best)
