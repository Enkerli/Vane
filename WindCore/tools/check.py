"""Render the takes with some settings and print the screening results. usage: check.py [name=value ...] [out=dir label=x]"""
import sys, os, tempfile, wc, numpy as np, audit, notes
from takes import render_takes
def run(params=None, outdir=None, label="", which=None, quiet=False):
    outdir = outdir or tempfile.mkdtemp(); res = {}
    for name, (path, x) in render_takes(outdir, label, params, which).items():
        a = audit.report(name, x, quiet=True); s = notes.summary(notes.check(name, x))
        res[name] = dict(a, **s)
        if not quiet:
            print("%-26s peak %5.1f dB crest %4.1f | spikes>1.5x %d of %2d (max %.2f) | wrong pitch %d of %2d, median %.1f c | settle median %.1f ms, max %.0f ms"
                  % (name, a["peak_db"], a["crest"], a["over_n"], a["n"], a["over_max"], s["wrong"], s["notes"], s["cents_med"], s["settle_med_ms"], s["settle_max_ms"]))
    return res
if __name__ == "__main__":
    p = dict(a.split("=") for a in sys.argv[1:]); out = p.pop("out", None); label = p.pop("label", "")
    run(p, out, label)
