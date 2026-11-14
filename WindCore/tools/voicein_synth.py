"""Test voices with known answers, for the voice input (VoiceTracker.h): a pulse or noise source through four
   formant resonators, with closures and bursts for "t", "k", "p". Each returns the sound, the formants it was
   made with (every 5 ms) and the times its consonants and vowels start.
   usage: voicein_synth.py outdir   (writes the set as WAV files with a .json of the answers beside each)"""
import sys, os, json, numpy as np, wc
from scipy.signal import lfilter, butter

SR = 48000
VOWELS = {"oo": (320, 900, 2300), "ah": (700, 1200, 2500), "ee": (300, 2300, 3000), "eh": (550, 1800, 2500), "oh": (480, 900, 2400)}

def _resonate(src, f, bw):
    """A resonator whose frequency moves (f per sample)."""
    y = np.zeros(len(src)); r = np.exp(-np.pi * bw / SR); c = 2 * r * np.cos(2 * np.pi * f / SR); g = 1 - c + r * r
    y1 = y2 = 0.0
    for n in range(len(src)):
        v = g[n] * src[n] + c[n] * y1 - r * r * y2; y2 = y1; y1 = v; y[n] = v
    return y

def tract(src, F1, F2, F3):
    y = _resonate(src, F1, 60.0); y = _resonate(y, F2, 90.0); y = _resonate(y, F3, 150.0)
    return _resonate(y, np.full(len(src), 3500.0), 200.0)

def source(n, f0, rng, whisper=False, vib=0.0):
    if whisper:
        return rng.standard_normal(n) * 0.05
    t = np.arange(n) / SR
    f = f0 * (1 + 0.004 * np.sin(2 * np.pi * 5.1 * t) * (1 if vib == 0 else vib / 0.004)) * (1 + 0.003 * np.cumsum(rng.standard_normal(n)) / np.sqrt(SR))
    ph = np.cumsum(f) / SR; pulses = np.diff(np.floor(ph), prepend=0.0)
    g = lfilter([1.0], [1, -1.94, 0.9409], pulses)             # -12 dB per octave, as a voice's source
    g = np.diff(g, prepend=0.0)                                 # radiation at the lips
    return g / (abs(g).max() + 1e-9) + rng.standard_normal(n) * 0.003

BURST = {"t": ("high", 4500, 1.0, 0.010), "k": ("band", (1400, 2600), 0.9, 0.014), "p": ("low", 900, 0.6, 0.007)}
def burst(kind, rng):
    mode, fc, amp, dur = BURST[kind]; n = int(dur * SR); x = rng.standard_normal(n + 400)
    if mode == "high": b, a = butter(2, fc / (SR / 2), "high")
    elif mode == "low": b, a = butter(2, fc / (SR / 2), "low")
    else: b, a = butter(2, [fc[0] / (SR / 2), fc[1] / (SR / 2)], "band")
    y = lfilter(b, a, x)[400:]; y /= np.sqrt(np.mean(y ** 2))
    env = np.minimum(1, np.arange(n) / (0.0008 * SR)) * np.exp(-np.arange(n) / (0.45 * n))
    return 0.12 * amp * y * env

def build(score, f0=120.0, whisper=False, seed=1, floor_db=-66.0):
    """score: list of items, each ('v', vowel, seconds [, level]) a vowel held, ('g', vowel, seconds) a glide to that vowel,
       ('c', 't'|'k'|'p', closure seconds) a stop consonant before the next vowel, or ('r', seconds) a rest."""
    rng = np.random.default_rng(seed)
    F = [[], [], []]; amp = []; marks = []; bursts = []; t = 0.0; cur = VOWELS[score[0][1]] if score[0][0] in "vg" else VOWELS["ah"]
    def seg(dur, target, a0, a1, glide=False):
        nonlocal cur, t
        n = int(dur * SR); u = np.linspace(0, 1, n, endpoint=False); s = 0.5 - 0.5 * np.cos(np.pi * u) if glide else np.ones(n)
        for k in range(3): F[k].append(np.exp(np.log(cur[k]) + (np.log(target[k]) - np.log(cur[k])) * s))
        amp.append(a0 + (a1 - a0) * u); cur = target; t += dur
    lvl = 0.0; i = 0; pending = None
    seg(0.1, cur, 0, 0)                                         # a moment of quiet first
    while i < len(score):
        it = score[i]
        if it[0] == "r":
            if lvl > 0: seg(0.03, cur, lvl, 0); lvl = 0
            seg(it[1], cur, 0, 0)
        elif it[0] == "c":
            if lvl > 0: seg(0.02, cur, lvl, 0); lvl = 0     # the mouth closes
            seg(it[2], cur, 0, 0); pending = it[1]
        else:
            target = VOWELS[it[1]]; want = it[3] if len(it) > 3 else 1.0
            if pending:
                b = burst(pending, rng); bursts.append((t, b)); marks.append({"t": t, "what": pending})
                cur = target; seg(len(b) / SR + 0.012, target, 0, 0)           # burst, then a little aspiration before the voice
                seg(0.02, target, 0, want); lvl = want; pending = None
            elif lvl == 0:
                marks.append({"t": t, "what": "vowel"}); cur = target if it[0] == "v" or i == 0 else cur; seg(0.03, cur, 0, want); lvl = want
            seg(it[2], target, lvl, want, glide=(it[0] == "g")); lvl = want
        i += 1
    if lvl > 0: seg(0.04, cur, lvl, 0)
    seg(0.25, cur, 0, 0)
    F = [np.concatenate(f) for f in F]; amp = np.concatenate(amp); n = len(amp)
    x = tract(source(n, f0, rng, whisper), *F) * amp
    x /= abs(x).max() + 1e-9; x *= 0.5
    for tb, b in bursts:
        k = int(tb * SR); x[k:k + len(b)] += b
    x += rng.standard_normal(n) * 10 ** (floor_db / 20)
    hop = SR // 200; idx = np.arange(0, n, hop)
    truth = {"hop": hop / SR, "f1": F[0][idx].round(1).tolist(), "f2": F[1][idx].round(1).tolist(), "level": amp[idx].round(3).tolist(), "marks": marks}
    return x, truth

SETS = {
    "oo-ah-ee": dict(score=[("v", "oo", 0.5), ("g", "ah", 0.35), ("v", "ah", 0.45), ("g", "ee", 0.35), ("v", "ee", 0.5), ("g", "ah", 0.3), ("g", "oo", 0.3), ("v", "oo", 0.3),
                            ("r", 0.25), ("v", "ee", 0.4), ("g", "oo", 0.6), ("g", "ee", 0.6), ("v", "ee", 0.2)]),
    "ta-ka-pa": dict(score=[("c", "t", 0.08), ("v", "ah", 0.22), ("c", "k", 0.08), ("v", "ah", 0.22), ("c", "p", 0.08), ("v", "ah", 0.3),
                            ("c", "t", 0.06), ("v", "ah", 0.12), ("c", "k", 0.06), ("v", "ah", 0.12), ("c", "p", 0.06), ("v", "ah", 0.12),
                            ("c", "t", 0.05), ("v", "ah", 0.09), ("c", "k", 0.05), ("v", "ah", 0.09), ("c", "t", 0.05), ("v", "ah", 0.09), ("c", "k", 0.05), ("v", "ah", 0.35)]),
    "phrase": dict(score=[("c", "t", 0.07), ("v", "oo", 0.2), ("g", "ah", 0.2), ("c", "k", 0.07), ("v", "ee", 0.25), ("c", "p", 0.07), ("v", "ah", 0.2), ("g", "oo", 0.25),
                          ("r", 0.15), ("v", "ee", 0.2), ("g", "ah", 0.2), ("c", "t", 0.06), ("v", "ah", 0.15), ("c", "t", 0.06), ("v", "ee", 0.15), ("c", "k", 0.06), ("v", "oo", 0.4), ("g", "ee", 0.5)]),
    "steady": dict(score=[("v", "ah", 0.6), ("v", "ah", 0.5, 0.5), ("v", "ah", 0.5, 1.0), ("g", "ee", 0.4), ("v", "ee", 0.6, 0.6), ("g", "oo", 0.5), ("v", "oo", 0.6)]),   # no consonants: swells and glides must not trigger
}
def make(name, **kw):
    return build(**dict(SETS[name], **kw))

if __name__ == "__main__":
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    for name in SETS:
        for tag, kw in (("", {}), ("-whisper", {"whisper": True})):
            x, truth = make(name, **kw); wc.write_wav16(os.path.join(out, "synthetic-%s%s.wav" % (name, tag)), x, SR)
            json.dump(truth, open(os.path.join(out, "synthetic-%s%s.json" % (name, tag)), "w"))
    print("wrote", out)
