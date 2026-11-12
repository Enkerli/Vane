"""Per-note calibration: tuning at soft, medium and loud breath (a curve over the embouchure opening), level at mf.
   usage: calibrate.py [passes] [name=value ...]     writes ../cal.txt for the engine's default settings
   From code: calibrate(params, out_path, start=rows) tunes any settings, for example a saved voice.
   File columns: note, c0, c1, gain dB, c2   (cents = c0 + c1*e + c2*e*e, e = sqrt of the embouchure opening)"""
import sys, os, numpy as np, wc
from concurrent.futures import ThreadPoolExecutor
LO, HI, SOFT, LOUD, MF, TARGET = 30, 100, 0.10, 1.0, 0.45, -18.0

def calibrate(params, out_path, passes=4, start=None, quiet=False):
    wc.build()
    emb = float(params.get("embMin", 0.04)); crv = float(params.get("breathCurve", 1.0))
    e = lambda b: np.sqrt(emb + (1 - emb) * b ** crv)
    A = np.linalg.inv(np.array([[1, e(b), e(b) ** 2] for b in (SOFT, MF, LOUD)]))
    def row(n):
        if start is None: return [0.0, 0.0, 0.0, 0.0]
        r = list(start[n][1:]); return r + [0.0] * (4 - len(r))
    cal = {n: row(n) for n in range(128)}      # c0, c1, gain, c2
    def write():
        with open(out_path, "w") as f:
            for n in range(128): f.write("%d %.2f %.2f %.2f %.2f\n" % (n, *cal[n]))
    def meas(job):
        n, b = job
        x = wc.render(wc.held(n, b), params=params, cal=out_path); seg = x[int(.6*48000):int(1.1*48000)]
        f0 = 440*2**((n-69)/12); r = np.sqrt(np.mean(seg**2))
        if r < 1e-5: return None
        f = wc.f0_of(seg, 48000, f0)
        return (1200*np.log2(f/f0), wc.db(r)) if np.isfinite(f) and f > 0 else None
    write(); err = None
    for it in range(passes + 1):
        jobs = [(n, b) for n in range(LO, HI + 1) for b in (SOFT, MF, LOUD)]
        with ThreadPoolExecutor(8) as ex: res = dict(zip(jobs, ex.map(meas, jobs)))
        errs = []; skipped = []
        for n in range(LO, HI + 1):
            s, m, l = res[(n, SOFT)], res[(n, MF)], res[(n, LOUD)]
            if not (s and l and m) or max(abs(s[0]), abs(m[0]), abs(l[0])) > 300: skipped.append(wc.note_name(n)); continue
            errs.append((abs(s[0]), abs(m[0]), abs(l[0]), abs(TARGET - m[1])))
            if it == passes: continue     # the last round only measures
            d = A @ np.array([s[0], m[0], l[0]])     # the error as a curve over e; take it off
            cal[n][0] -= d[0]; cal[n][1] -= d[1]; cal[n][3] -= d[2]; cal[n][2] += TARGET - m[1]
        err = np.array(errs).max(axis=0)
        if not quiet: print("%s: worst tuning soft %.1f mf %.1f loud %.1f cents, worst level %.1f dB (%d notes%s)"
                            % ("result" if it == passes else "before pass %d" % (it + 1), *err, len(errs), ", skipped " + " ".join(skipped) if skipped else ""))
        if it < passes:
            for n in range(0, LO): cal[n] = list(cal[LO])
            for n in range(HI + 1, 128): cal[n] = list(cal[HI])
            write()
    return err

if __name__ == "__main__":
    args = sys.argv[1:]; passes = int(args.pop(0)) if args and args[0].isdigit() else 4
    calibrate(dict(a.split("=") for a in args), wc.CAL, passes)
