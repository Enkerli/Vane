"""The character layer: controls (0..1) to model settings, plus the tuning and level trims that keep
   the instrument in tune and even when a control moves."""
import json, os, tempfile, numpy as np, wc
SPEC = json.load(open(os.path.join(wc.ROOT, "character.json")))
CONTROLS = {c["id"]: c for c in SPEC["controls"]}
TRIMS = os.path.join(wc.ROOT, "trims.json")
MOUTH_ANCHORS = [400, 500, 630, 800, 1000, 1260, 1590, 2000, 2400]

def to_phys(c, v):
    lo, hi = c["lo"], c["hi"]
    if c["scale"] == "log": return lo * (hi / lo) ** v
    if c["scale"] == "square": return lo + (hi - lo) * v * v
    return lo + (hi - lo) * v
def to_pos(c, x):
    lo, hi = c["lo"], c["hi"]
    if c["scale"] == "log": return np.log(x / lo) / np.log(hi / lo)
    if c["scale"] == "square": return np.sqrt(max(0.0, (x - lo) / (hi - lo)))
    return (x - lo) / (hi - lo)
DEFAULT = {k: float(to_pos(c, c["default"])) for k, c in CONTROLS.items()}

def params(values=None):
    v = dict(DEFAULT, **(values or {}))
    return {c["param"]: to_phys(c, v[k]) for k, c in CONTROLS.items()}

def _load():
    return json.load(open(TRIMS)) if os.path.exists(TRIMS) else {}
def trim(values=None, T=None):
    """cents[128], dB[128] to add to the base calibration for these control positions."""
    T = _load() if T is None else T; v = dict(DEFAULT, **(values or {})); cents = np.zeros(128); gain = np.zeros(128)
    for k, c in CONTROLS.items():
        if c.get("trim") is False or k in ("mouth", "mouthDepth"): continue
        d = DEFAULT[k]; x = v[k]
        if abs(x - d) < 1e-9: continue
        key, w = (k + ":1", (x - d) / (1 - d)) if x > d else (k + ":0", (d - x) / d)
        if key in T: cents += w * np.array(T[key]["cents"]); gain += w * np.array(T[key]["gain"])
    # mouth: tables at full strength for several positions; scale with strength, interpolate between positions
    depth = to_phys(CONTROLS["mouthDepth"], v["mouthDepth"]); f = to_phys(CONTROLS["mouth"], v["mouth"])
    if depth > 0 and CONTROLS["mouth"]["param"] == "mouthFreq":      # only the physical tract detunes; the vowel filter does not
        la = np.log(MOUTH_ANCHORS); i = int(np.clip(np.searchsorted(la, np.log(f)) - 1, 0, len(la) - 2))
        t = float(np.clip((np.log(f) - la[i]) / (la[i + 1] - la[i]), 0, 1)); w = depth / CONTROLS["mouthDepth"]["hi"]
        for a, ww in ((MOUTH_ANCHORS[i], 1 - t), (MOUTH_ANCHORS[i + 1], t)):
            e = T.get("mouth@%d" % a)
            if e: cents += w * ww * np.array(e["cents"]); gain += w * ww * np.array(e["gain"])
    return cents, gain

def cal_file(values=None, T=None, use_trim=True):
    base = np.loadtxt(wc.CAL)
    if use_trim:
        c, g = trim(values, T); base[:, 1] += c; base[:, 3] += g
    f = tempfile.NamedTemporaryFile("w", suffix=".cal", delete=False)
    for row in base: f.write("%d %.2f %.2f %.2f %.2f\n" % (row[0], row[1], row[2], row[3], row[4] if len(row) > 4 else 0.0))
    f.close(); return f.name

def render(events, values=None, out=None, use_trim=True, extra=None, T=None):
    cf = cal_file(values, T, use_trim)
    try: return wc.render(events, out=out, params=dict(params(values), **(extra or {})), cal=cf)
    finally: os.unlink(cf)

VOICES = os.path.join(wc.ROOT, "voices")
def save_voice(name, values, note=""):
    """Store a voice with its own calibration (tuned from the trims as a starting point)."""
    import calibrate
    os.makedirs(VOICES, exist_ok=True)
    start = np.loadtxt(cal_file(values)); path = os.path.join(VOICES, name + ".cal")
    e = calibrate.calibrate(params(values), path, passes=3, start=start, quiet=True)
    json.dump({"name": name, "note": note, "controls": dict(DEFAULT, **values), "settings": params(values),
               "worst_tuning_cents": float(max(e[:3])), "worst_level_db": float(e[3])}, open(os.path.join(VOICES, name + ".json"), "w"), indent=1)
    return e
def render_voice(events, name, out=None, extra=None):
    v = json.load(open(os.path.join(VOICES, name + ".json")))
    return wc.render(events, out=out, params=dict(v["settings"], **(extra or {})), cal=os.path.join(VOICES, name + ".cal"))
