"""Render the playing excerpts. usage: takes.py outdir [label] [name=value ...] [takes=A,C,F]"""
import sys, os, glob, wc, numpy as np
from audit import TAKES
def render_takes(outdir, label="", params=None, which=None, gain=0.25):
    os.makedirs(outdir, exist_ok=True); out = {}
    for f in sorted(glob.glob(os.path.join(TAKES, "*.mid"))):
        name = os.path.basename(f)[:-4]
        if which and name[0] not in which: continue
        x = wc.render(wc.midi_events(f), params=params) * gain
        path = os.path.join(outdir, name + ("--" + label if label else "") + ".wav")
        wc.write_wav16(path, x); out[name] = (path, x)
    return out
if __name__ == "__main__":
    a = sys.argv[1:]; outdir = a.pop(0); label = a.pop(0) if a and "=" not in a[0] else ""
    p = dict(v.split("=") for v in a); which = p.pop("takes", None)
    for name, (path, x) in render_takes(outdir, label, p, which.split(",") if which else None).items():
        print("%-28s peak %5.1f dB  rms %5.1f dB" % (name, wc.db(abs(x).max()), wc.db(np.sqrt(np.mean(x**2)))))
