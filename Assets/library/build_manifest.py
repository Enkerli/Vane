#!/usr/bin/env python3
"""Generate library.json for the Vane factory wavetable library.
Run from the Vane repo root: python3 Assets/library/build_manifest.py
Reads the .wav files in this directory + their metadata from /tmp/vwl sidecars.
"""
import json, struct, os, numpy as np
from scipy.io import wavfile

TABLES = [
    {"id": "akwf_violin",         "file": "AKWF_violin.wav",         "title": "Violin",         "family": "strings",   "tags": ["dark_to_bright","bowed"],      "morphIntent": "spectral_enrichment"},
    {"id": "akwf_oboe",           "file": "AKWF_oboe.wav",           "title": "Oboe",           "family": "winds",     "tags": ["dark_to_bright","reed"],       "morphIntent": "spectral_enrichment"},
    {"id": "akwf_pluckalgo",      "file": "AKWF_pluckalgo.wav",      "title": "Pluck Algo",     "family": "plucked",   "tags": ["dark_to_bright","pluck"],      "morphIntent": "spectral_enrichment"},
    {"id": "akwf_stringbox",      "file": "AKWF_stringbox.wav",      "title": "String Box",     "family": "strings",   "tags": ["dark_to_bright","ensemble"],   "morphIntent": "spectral_enrichment"},
    {"id": "akwf_bw_perfectwaves","file": "AKWF_bw_perfectwaves.wav","title": "Perfect Waves",  "family": "analytic",  "tags": ["dark_to_bright","analytic"],   "morphIntent": "brightness_ramp"},
]

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

for t in TABLES:
    path = os.path.join(SCRIPT_DIR, t["file"])
    sr, data = wavfile.read(path)
    if data.ndim > 1: data = data[:, 0]
    d = data.astype(np.float64) / (np.abs(data).max() + 1e-9)
    fs = 2048
    t["frameCount"] = len(d) // fs
    t["frameLength"] = fs
    t["license"] = "CC0"
    t["source"] = "Adventure Kid Waveforms (AKWF)"
    t["licenseUrl"] = "https://adventurekid.se/akrt/waveforms/"

manifest = {
    "version": 1,
    "tables": TABLES
}

out = os.path.join(SCRIPT_DIR, "library.json")
with open(out, "w") as f:
    json.dump(manifest, f, indent=2)
print(f"Wrote {out} ({len(TABLES)} tables)")
