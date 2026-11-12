"""Per-note checks on a rendered take: is each note at its pitch, and how long does the new note
   take to become periodic at its own period (cycle-to-cycle correlation above 0.9 and staying there)."""
import sys, os, wc, numpy as np
from audit import onsets, TAKES
def check(name, x, r=48000, bend_ok=True):
    ons = onsets(name); rows = []
    ev = sorted(wc.midi_events(os.path.join(TAKES, name + '.mid')), key=lambda e: e[0]); offs = {}
    for t0, n0, _ in ons:
        offs[(t0, n0)] = next((t for t, k, v in ev if k == 'off' and v == n0 and t > t0), t0 + 9)
    for i, (t, n, leg) in enumerate(ons):
        nxt = ons[i + 1][0] if i + 1 < len(ons) else t + 0.4
        nxt = min(nxt, offs.get((t, n), nxt))     # a note ends at its own note-off if that comes first
        dur = nxt - t; f0 = 440 * 2 ** ((n - 69) / 12); P = r / f0
        if dur < 0.07 or n > 98: continue
        seg = x[int((t + dur * 0.5) * r):int((t + dur * 0.95) * r)]
        if len(seg) < 3 * P or np.sqrt(np.mean(seg ** 2)) < 2e-3: continue
        cents = 1200 * np.log2(wc.f0_of(seg, r, f0) / f0)
        # settle: correlation between consecutive cycles, hop of a quarter cycle
        Pi = int(round(P)); hop = max(1, Pi // 4); a = int(t * r); settle = None; cs = []
        for j in range(a, int((t + 0.7 * dur) * r) - 2 * Pi, hop):   # the end of a note is its release, not its start
            u, v = x[j:j + Pi], x[j + Pi:j + 2 * Pi]
            cs.append(np.dot(u, v) / (np.sqrt(np.dot(u, u) * np.dot(v, v)) + 1e-12))
        cs = np.array(cs); bad = np.where(cs < 0.9)[0]
        settle = 0.0 if len(bad) == 0 else (bad[-1] + 1) * hop / r * 1000
        if len(bad) and bad[-1] > 0.8 * len(cs): settle = float("nan")
        rows.append((t, n, leg, dur, cents, settle, settle * f0 / 1000))
    return rows
def summary(rows):
    c = np.array([q[4] for q in rows]); s = np.array([q[5] for q in rows]); cyc = np.array([q[6] for q in rows])
    return dict(notes=len(rows), wrong=int((abs(c) > 60).sum()), cents_med=float(np.median(abs(c))),
                settle_med_ms=float(np.nanmedian(s)), settle_max_ms=float(np.nanmax(s)), never=int(np.isnan(s).sum()),
                settle_med_cycles=float(np.nanmedian(cyc)))
if __name__ == "__main__":
    for path in sys.argv[1:]:
        x, r = wc.read_wav(path); name = os.path.basename(path).replace("16bit-", "").split("--")[0]
        rows = check(name, x, r); print(os.path.basename(path), {k: round(v, 1) for k, v in summary(rows).items()})
        for q in rows:
            if abs(q[4]) > 60 or not q[5] < 60: print("    %.3f %-4s dur %.2f  %+5.0f cents  settle %.0f ms" % (q[0], wc.note_name(q[1]), q[3], q[4], q[5]))
