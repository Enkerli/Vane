"""Screening for clicks and rough note changes. Not validated against listening marks: use it to
   find places to listen to, not to declare a render smooth.
   For each note start: overshoot (largest 3 ms peak in the first 80 ms over the settled peak after it),
   dip (smallest over settled), edge (largest short burst of high-frequency energy over its median)."""
import sys, os, wc, numpy as np
TAKES = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "bench", "playing")
def onsets(name):
    ev = wc.midi_events(os.path.join(TAKES, name + ".mid")); held = 0; out = []
    for t, k, n in sorted(ev, key=lambda e: e[0]):
        if k == "on": out.append((t, n, held > 0)); held += 1
        elif k == "off": held = max(0, held - 1)
    return out
def audit(x, r, ons):
    rows = []
    for i, (t, n, leg) in enumerate(ons):
        nxt = ons[i + 1][0] if i + 1 < len(ons) else t + 0.3
        a, b = int((t - 0.004) * r), int(min(t + 0.08, nxt) * r); s0, s1 = b, int(min(nxt, t + 0.25) * r)
        seg = np.abs(x[a:b]); w = int(.003 * r)
        if len(seg) < 2 * w: continue
        env = np.array([seg[j:j + w].max() for j in range(0, len(seg) - w, w // 2)])
        settled = np.abs(x[s0:s1]).max() if s1 - s0 > w else np.median(env)
        settled = max(settled, np.abs(x[max(0, a - int(.03 * r)):a]).max())   # a spike stands above both sides
        d2 = np.diff(x[a:b], 2); ww = int(.0005 * r)
        e = np.sqrt(np.convolve(d2 ** 2, np.ones(ww) / ww, "same"))
        rows.append((t, n, leg, env.max() / (settled + 1e-9), env.min() / (settled + 1e-9), e.max() / (np.median(e) + 1e-9), nxt - t))
    return rows
def report(name, x, r=48000, top=6, quiet=False):
    rows = audit(x, r, onsets(name))
    leg = [q for q in rows if q[2] and q[6] > 0.05]
    worst = sorted(leg, key=lambda q: -q[3])[:top]
    if not quiet:
        for t, n, l, ov, dip, edge, dur in worst:
            print("   %7.3f s  %-4s overshoot %4.2f  dip %4.2f  edge %5.1f" % (t, wc.note_name(n), ov, dip, edge))
    ov = np.array([q[3] for q in leg]) if leg else np.array([1.0])
    return dict(peak_db=wc.db(np.abs(x).max()), crest=wc.db(np.abs(x).max() / np.sqrt(np.mean(x ** 2))),
                over_max=ov.max(), over_n=int((ov > 1.5).sum()), n=len(leg), edge_max=max([q[5] for q in rows] or [0]))
if __name__ == "__main__":
    for path in sys.argv[1:]:
        x, r = wc.read_wav(path); name = os.path.basename(path).replace("16bit-", "").split("--")[0]
        print(os.path.basename(path)); print("  ", report(name, x, r))
