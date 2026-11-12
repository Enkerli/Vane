"""Bring Vane's web panel and key map in line with ../character.json (controls, groups, default positions).
   Edits the web panel (VANE_WEBUI_DIR, default ../../music-suite/apps/vane) and Source/WebUI/WebVaneEditor.cpp in place."""
import json, os, re, wc, voice
cap = lambda s: s[0].upper() + s[1:]
C = voice.SPEC["controls"]; keys = ["Wind" + cap(c["id"]) for c in C]; pos = [float(voice.DEFAULT[c["id"]]) for c in C]
html = os.path.join(os.environ.get("VANE_WEBUI_DIR", os.path.join(os.path.dirname(os.path.dirname(wc.ROOT)), "music-suite", "apps", "vane")), "index.html"); s = open(html).read()
W = {"groups": voice.SPEC["groups"], "controls": [{"key": k, "label": c["label"], "group": c["group"], "lo": c["ends"][0], "hi": c["ends"][1], "says": c["says"], "def": round(p, 3)} for k, c, p in zip(keys, C, pos)]}
def sub(pat, new, text):
    out, n = re.subn(pat, lambda m: new, text, count=1, flags=re.S); assert n == 1, pat; return out
s = sub(r"const WIND=\{.*?\};\n", "const WIND=" + json.dumps(W, ensure_ascii=False) + ";\n", s)
s = sub(r"WindMouth:[0-9.]+,(?:Wind\w+:[0-9.]+,)*", ",".join("%s:%.3f" % kp for kp in zip(keys, pos)) + ",", s)
s = sub(r"WindMouth:v=>windFmt\('WindMouth',v\),(?: Wind\w+:v=>windFmt\('Wind\w+',v\),)*", " ".join("%s:v=>windFmt('%s',v)," % (k, k) for k in keys), s)
s = sub(r"WindMouth:\[0,1,\.01\],(?: Wind\w+:\[0,1,\.01\],)*", " ".join("%s:[0,1,.01]," % k for k in keys), s)
open(html, "w").write(s)
cpp = os.path.join(os.path.dirname(wc.ROOT), "Source", "WebUI", "WebVaneEditor.cpp"); s = open(cpp).read()
s = sub(r'\{ "WindMouth", "windMouth" \}(?:, \{ "Wind\w+", "wind\w+" \})*,', ", ".join('{ "%s", "w%s" }' % (k, k[1:]) for k in keys) + ",", s)
open(cpp, "w").write(s); print("web panel and key map updated: %d controls in %d groups" % (len(keys), len(W["groups"])))
