"""What a setting does, on held notes: level, tuning, and the change in each harmonic against the default.
   usage: effect.py name=v1,v2,... [other=value ...] [notes=47,59,71,83] [breath=0.6]"""
import sys, wc, numpy as np
def meas(n, params, b):
    x = wc.render(wc.held(n, b), params=params); seg = x[int(.6*48000):int(1.1*48000)]; f0 = 440*2**((n-69)/12)
    r = np.sqrt(np.mean(seg**2))
    if r < 1e-5: return None
    f = wc.f0_of(seg, 48000, f0); h = wc.harmonics(seg, 48000, f, 16)
    res = seg - 0  # noise: energy between harmonics
    spec = abs(np.fft.rfft(seg*np.hanning(len(seg))))**2; fr = np.fft.rfftfreq(len(seg), 1/48000)
    k = fr / f; near = abs(k - np.round(k)) < 0.15
    noise = 10*np.log10(spec[~near & (fr > 300)].sum() / spec.sum() + 1e-12)
    return wc.db(r), 1200*np.log2(f/f0), h, noise, f
if __name__ == "__main__":
    args = dict(a.split("=") for a in sys.argv[1:])
    notes = [int(v) for v in args.pop("notes", "47,59,71,83").split(",")]; b = float(args.pop("breath", 0.6))
    axis = [k for k, v in args.items() if "," in v][0]; vals = args.pop(axis).split(",")
    for n in notes:
        ref = meas(n, args, b)
        print("%s (%.0f Hz), breath %.2f: default %.1f dB, %+.0f cents, noise %.0f dB" % (wc.note_name(n), ref[4], b, ref[0], ref[1], ref[3]))
        print("   %-18s level  cents noise | harmonic changes, dB:  " % axis + " ".join("%4d" % k for k in range(1, 13)))
        for v in vals:
            m = meas(n, dict(args, **{axis: v}), b)
            if m is None: print("   %-18s silent" % v); continue
            wrong = "  WRONG REGISTER" if abs(m[1]) > 80 else ""
            d = m[2] - ref[2]
            print("   %-18s %+5.1f %+5.0f %+5.0f |                        " % (v, m[0]-ref[0], m[1]-ref[1], m[3]-ref[3]) + " ".join("%+4.0f" % x for x in d[:12]) + wrong)
