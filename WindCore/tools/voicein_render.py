"""A listening folder for the voice input: the same notes played plain and with a voice driving the instrument in
   each way the renderer offers, level-matched.
   usage: voicein_render.py outdir [voice.wav ...]
   With no recordings it uses the test voices of voicein_synth.py. Recordings get every variant over a slow line."""
import sys, os, numpy as np, wc, voicein_synth as vs

CONTROLS = dict(air=0.1)           # little breath noise, so what the voice adds is easy to hear
VARIANTS = [                       # tag, description, renderer settings
    ("01-plain-notes-no-voice", None),
    ("02-vowel-as-filter-after-the-horn", {"v.vowel": 10}),
    ("03-formants-as-filter-after-the-horn", {"v.formants": 10}),
    ("04-voice-sung-into-the-mouth", {"voiceSing": 1.0}),
    ("05-voice-as-the-air", {"voiceAir": 2.0}),
    ("06-consonants-tongue-the-reed", {"v.tongue": 1, "chiff": 0}),
    ("07-consonants-tongue-with-their-noise", {"v.tongue": 1, "v.chiff": 1.5}),
    ("08-tongue-noise-and-vowel-filter", {"v.tongue": 1, "v.chiff": 1.5, "v.vowel": 10}),
    ("09-tongue-noise-and-voice-in-the-mouth", {"v.tongue": 1, "v.chiff": 1.5, "voiceSing": 1.0}),
    ("10-all-of-it-with-breath-from-the-voice", {"v.tongue": 1, "v.chiff": 1.5, "v.vowel": 10, "v.breath": 1, "voiceSing": 0.6}),
]
SETS = [("oo-ah-ee", {}, "held", (1, 2, 3, 4, 5)), ("oo-ah-ee", {"whisper": True}, "held", (1, 2, 5)),
        ("ta-ka-pa", {}, "line", (1, 6, 7)), ("phrase", {}, "line", (1, 2, 4, 6, 7, 8, 9, 10))]

def notes(kind, dur, breath=0.7):
    seq = [53] if kind == "held" else [53, 56, 58, 60, 58, 56]; step = 0.62
    ev = [(0.02, "breath", 0), (0.06, "breath", breath), (dur - 0.12, "breath", breath), (dur - 0.06, "breath", 0), (dur, "end", 0)]
    t, i, prev = 0.02, 0, None
    while t < dur - 0.2 and (kind != "held" or i == 0):
        n = seq[i % len(seq)]; ev.append((t, "on", n))
        if prev is not None: ev.append((t + 0.001, "off", prev))
        prev = n; t += step; i += 1
    ev.append((dur - 0.06, "off", prev))
    return ev

def loud(x):
    """RMS of the sounding parts (50 ms frames within 30 dB of the loudest)."""
    n = len(x) // 2400; f = np.sqrt(np.mean(x[:n * 2400].reshape(n, 2400) ** 2, axis=1) + 1e-12); return np.sqrt(np.mean(f[f > f.max() * 10 ** -1.5] ** 2))

def render_set(out, name, voice_wav, kind, which):
    v, rate = wc.read_wav(voice_wav); dur = len(v) / rate + 0.3; made = [("00-the-voice", v if rate == 48000 else np.interp(np.arange(int(len(v) * 48000 / rate)) * rate / 48000, np.arange(len(v)), v))]
    for k in which:
        tag, opts = VARIANTS[k - 1]
        made.append((tag, wc.render(notes(kind, dur), params=opts, controls=CONTROLS, voice=voice_wav if opts else None)))
    g = min(10 ** (-20 / 20) / loud(x) * 1.0 for _, x in made); peak = max(abs(x).max() * 10 ** (-20 / 20) / loud(x) for _, x in made)
    scale = min(1.0, 0.98 / peak)                       # one more gain for the whole set if anything would clip
    for tag, x in made:
        wc.write_wav16(os.path.join(out, "%s--%s--%s.wav" % (name, kind, tag)), x * 10 ** (-20 / 20) / loud(x) * scale)
        print("%-34s %-44s %6.1f dB before matching" % (name, tag, wc.db(loud(x))))

if __name__ == "__main__":
    out = sys.argv[1]; os.makedirs(out, exist_ok=True); wc.build()
    if len(sys.argv) > 2:
        for path in sys.argv[2:]:
            render_set(out, os.path.splitext(os.path.basename(path))[0], path, "line", range(1, len(VARIANTS) + 1))
    else:
        import tempfile
        for name, kw, kind, which in SETS:
            x, _ = vs.make(name, **kw); f = tempfile.mktemp(suffix=".wav"); wc.write_wav16(f, x, vs.SR)
            render_set(out, "synthetic-" + name + ("-whisper" if kw.get("whisper") else ""), f, kind, which); os.unlink(f)
