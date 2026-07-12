#!/usr/bin/env python3
"""
Analyze MiniSax renders into descriptor JSON + summary CSV.

Usage:
    python tools/analyze_renders.py --input renders/breathy_001/ --out analysis/breathy_001/

Descriptors: peak, RMS, attack time, spectral centroid, zero-crossing rate,
pitch estimate (autocorrelation), pitch stability, sustain amplitude
variation, clipping and silence detection.

Degrades gracefully: with numpy available all descriptors are computed; with
only the standard library, spectral centroid and pitch are reported as null
and the basic descriptors still come out.  If the input directory contains a
render_report.json (written by minisax-render), its provenance — preset id,
model version, engine commit, suite id, test id, parameters hash, noise seed —
is merged into each per-file report (see docs/TRACEABILITY.md).
"""
import argparse
import csv
import json
import math
import struct
import wave
from pathlib import Path

try:
    import numpy as np
    HAVE_NUMPY = True
except ImportError:  # stdlib-only fallback
    HAVE_NUMPY = False

SILENCE_PEAK = 1e-4
CLIP_PEAK = 0.999
SUSTAIN_VARIATION_WARN = 0.5
PITCH_MIN_HZ = 40.0
PITCH_MAX_HZ = 2000.0


# ── WAV loading ──────────────────────────────────────────────────────────────

def load_wav(path: Path):
    """Return (sample_rate, list-or-ndarray of float samples in [-1, 1], mono)."""
    if HAVE_NUMPY:
        try:
            from scipy.io import wavfile
            sr, raw = wavfile.read(path)
            x = raw.astype(np.float64)
            if x.ndim > 1:
                x = x.mean(axis=1)
            if np.issubdtype(raw.dtype, np.integer):
                x /= np.iinfo(raw.dtype).max
            return sr, x
        except ImportError:
            pass  # numpy without scipy: fall through to the wave module
    with wave.open(str(path), "rb") as w:
        sr = w.getframerate()
        n = w.getnframes()
        channels = w.getnchannels()
        width = w.getsampwidth()
        frames = w.readframes(n)
    if width != 2:
        raise ValueError(f"{path}: only 16-bit PCM supported by the fallback reader")
    ints = struct.unpack(f"<{n * channels}h", frames)
    if channels > 1:
        ints = [sum(ints[i:i + channels]) / channels for i in range(0, len(ints), channels)]
    samples = [v / 32768.0 for v in ints]
    return sr, np.asarray(samples) if HAVE_NUMPY else samples


# ── Descriptors ──────────────────────────────────────────────────────────────

def rms_envelope(x, frame: int, hop: int):
    env = []
    for start in range(0, max(1, len(x) - frame + 1), hop):
        chunk = x[start:start + frame]
        if HAVE_NUMPY:
            env.append(float(np.sqrt(np.mean(chunk * chunk) + 1e-12)))
        else:
            env.append((sum(v * v for v in chunk) / len(chunk) + 1e-12) ** 0.5)
    return env


def attack_time_ms(env, hop: int, sr: int):
    """Time from 10% to 90% of the envelope peak, in milliseconds."""
    if not env:
        return None
    peak = max(env)
    if peak < 1e-5:
        return None
    lo, hi = 0.1 * peak, 0.9 * peak
    lo_idx = hi_idx = None
    for i, v in enumerate(env):
        if lo_idx is None and v >= lo:
            lo_idx = i
        if lo_idx is not None and v >= hi:
            hi_idx = i
            break
    if lo_idx is None or hi_idx is None:
        return None
    return (hi_idx - lo_idx) * hop * 1000.0 / sr


def zero_crossing_rate(x):
    if len(x) < 2:
        return 0.0
    if HAVE_NUMPY:
        return float(np.mean(np.abs(np.diff(np.signbit(x))).astype(np.float64)))
    crossings = sum(1 for a, b in zip(x, x[1:]) if (a < 0) != (b < 0))
    return crossings / (len(x) - 1)


def spectral_centroid(x, sr: int, frame: int = 2048, hop: int = 512):
    if not HAVE_NUMPY:
        return None
    cents = []
    freqs = np.fft.rfftfreq(frame, 1.0 / sr)
    window = np.hanning(frame)
    for start in range(0, max(1, len(x) - frame + 1), hop):
        chunk = x[start:start + frame]
        if len(chunk) < frame:
            chunk = np.pad(chunk, (0, frame - len(chunk)))
        mag = np.abs(np.fft.rfft(chunk * window))
        total = float(np.sum(mag))
        if total > 1e-9:
            cents.append(float(np.sum(freqs * mag) / total))
    return float(np.mean(cents)) if cents else None


def pitch_track(x, sr: int, frame: int = 2048, hop: int = 1024):
    """Autocorrelation pitch per frame; returns (medianHz, stabilityCents)."""
    if not HAVE_NUMPY:
        return None, None
    lag_min = max(2, int(sr / PITCH_MAX_HZ))
    lag_max = min(frame - 2, int(sr / PITCH_MIN_HZ))
    if lag_max <= lag_min:
        return None, None
    pitches = []
    for start in range(0, max(1, len(x) - frame + 1), hop):
        chunk = x[start:start + frame]
        if float(np.sqrt(np.mean(chunk * chunk))) < SILENCE_PEAK:
            continue  # don't track pitch in silence
        chunk = chunk - np.mean(chunk)
        ac = np.correlate(chunk, chunk, mode="full")[frame - 1:]
        if ac[0] <= 0:
            continue
        # Unbiased normalization (finite-frame taper otherwise favors small
        # lags), then take the SMALLEST lag within 10% of the max: with sax
        # spectra H2 can rival H1 and a bare argmax octave-errs to T/2.
        norm = ac[:lag_max].astype(np.float64) / (len(chunk) - np.arange(lag_max))
        segment = norm[lag_min:lag_max]
        best = float(np.max(segment))
        # Voicing gate: periodic frames have a strong normalized peak.
        if best / (norm[0] + 1e-18) < 0.3:
            continue
        candidates = np.flatnonzero(segment >= 0.9 * best)
        lag = int(candidates[0]) + lag_min
        # keep the local maximum of the chosen peak
        while lag + 1 < lag_max and norm[lag + 1] > norm[lag]:
            lag += 1
        while lag - 1 > lag_min and norm[lag - 1] > norm[lag]:
            lag -= 1
        # Parabolic interpolation around the peak for sub-sample lag.
        if 1 <= lag < len(norm) - 1:
            a, b, c = norm[lag - 1], norm[lag], norm[lag + 1]
            denom = a - 2 * b + c
            if abs(denom) > 1e-12:
                offset = 0.5 * (a - c) / denom
                if abs(offset) <= 1.0:  # a sane peak refinement never moves further
                    lag = lag + offset
        if lag <= 0:
            continue
        pitch = sr / lag
        if PITCH_MIN_HZ <= pitch <= PITCH_MAX_HZ:
            pitches.append(pitch)
    if not pitches:
        return None, None
    median = float(np.median(pitches))
    cents = 1200.0 * np.log2(np.asarray(pitches) / median)
    return median, float(np.std(cents))


def harmonic_profile(x, sr: int, pitch_hz):
    """Harmonic levels H2..H8 in dB relative to H1, measured on the loudest
    0.5 s window.  Returns (profile list, h2RelH1Db, evenOddRatioDb).
    The even/odd energy ratio is the sax-vs-clarinet "oomph" metric: a
    quarter-wave bore sits far negative, a conical instrument near 0 dB."""
    if not HAVE_NUMPY or not pitch_hz:
        return None, None, None
    win = int(0.5 * sr)
    if len(x) < win:
        return None, None, None
    hop = 1024
    rms_best, i_best = -1.0, 0
    for i in range(0, len(x) - win, hop):
        r = float(np.sqrt(np.mean(x[i:i + win] ** 2)))
        if r > rms_best:
            rms_best, i_best = r, i
    seg = x[i_best:i_best + win]
    # Local pitch of THIS window (may be a different note than the file-wide
    # median): reuse the voiced-frame tracker, fall back to the global value.
    f0, _ = pitch_track(seg, sr)
    if not f0:
        f0 = pitch_hz
    mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / sr)
    def level(h):
        idx = int(np.argmin(np.abs(freqs - f0 * h)))
        return float(mag[max(0, idx - 3):idx + 4].max())
    h1 = level(1)
    if h1 <= 0:
        return None, None, None
    profile = [round(20.0 * math.log10(level(k) / h1 + 1e-12), 1) for k in range(2, 9)]
    even = sum(level(k) ** 2 for k in (2, 4, 6, 8))
    odd = sum(level(k) ** 2 for k in (3, 5, 7))
    even_odd = round(10.0 * math.log10((even + 1e-18) / (odd + 1e-18)), 1)
    return profile, profile[0], even_odd


def analyze_file(path: Path) -> dict:
    sr, x = load_wav(path)
    frame = max(256, int(0.02 * sr))
    hop = max(128, int(0.005 * sr))
    env = rms_envelope(x, frame, hop)

    if HAVE_NUMPY:
        peak = float(np.max(np.abs(x))) if len(x) else 0.0
        mean_rms = float(np.sqrt(np.mean(x * x) + 1e-12)) if len(x) else 0.0
    else:
        peak = max((abs(v) for v in x), default=0.0)
        mean_rms = (sum(v * v for v in x) / len(x) + 1e-12) ** 0.5 if x else 0.0

    sustain = env[len(env) // 3: 2 * len(env) // 3] if len(env) >= 6 else env
    if sustain:
        mean_s = sum(sustain) / len(sustain)
        var_s = (sum((v - mean_s) ** 2 for v in sustain) / len(sustain)) ** 0.5
        sustain_var = var_s / (mean_s + 1e-12)
    else:
        sustain_var = 0.0

    pitch_hz, pitch_stability_cents = pitch_track(x, sr)
    profile, h2_rel_h1, even_odd = harmonic_profile(x, sr, pitch_hz)

    warnings = [w for w, active in {
        "silent_or_nearly_silent": peak < SILENCE_PEAK,
        "clipping_or_near_clipping": peak >= CLIP_PEAK,
        "high_sustain_variation": sustain_var > SUSTAIN_VARIATION_WARN,
        "descriptors_degraded_no_numpy": not HAVE_NUMPY,
    }.items() if active]

    return {
        "file": path.name,
        "sampleRate": int(sr),
        "durationSeconds": len(x) / sr,
        "peak": peak,
        "rms": mean_rms,
        "attackMs": attack_time_ms(env, hop, sr),
        "spectralCentroidHz": spectral_centroid(x, sr),
        "zeroCrossingRate": zero_crossing_rate(x),
        "pitchHz": pitch_hz,
        "pitchStabilityCents": pitch_stability_cents,
        "harmonicProfileDb": profile,
        "h2RelH1Db": h2_rel_h1,
        "evenOddRatioDb": even_odd,
        "sustainAmplitudeVariation": sustain_var,
        "isSilent": bool(peak < SILENCE_PEAK),
        "isClipped": bool(peak >= CLIP_PEAK),
        "warnings": warnings,
    }


# ── Provenance ───────────────────────────────────────────────────────────────

def load_provenance(input_dir: Path) -> dict:
    """Map wav filename -> provenance dict, from render_report.json if present."""
    report_path = input_dir / "render_report.json"
    if not report_path.exists():
        return {}
    report = json.loads(report_path.read_text(encoding="utf-8"))
    base = {
        "presetId": report.get("preset", {}).get("id"),
        "presetPath": report.get("preset", {}).get("path"),
        "modelVersion": report.get("preset", {}).get("modelVersion"),
        "parametersHash": report.get("preset", {}).get("parametersHash"),
        "engineVersion": report.get("engine", {}).get("version"),
        "engineGitCommit": report.get("engine", {}).get("gitCommit"),
        "suiteId": report.get("suite", {}).get("id"),
    }
    result = {}
    for t in report.get("tests", []):
        result[t.get("file")] = {
            **base,
            "testId": t.get("testId"),
            "noiseSeed": t.get("noiseSeed"),
            "renderWarnings": t.get("warnings", []),
        }
    return result


CSV_COLUMNS = [
    "file", "presetId", "suiteId", "testId", "parametersHash", "engineGitCommit",
    "sampleRate", "durationSeconds", "peak", "rms", "attackMs",
    "spectralCentroidHz", "zeroCrossingRate", "pitchHz", "pitchStabilityCents",
    "h2RelH1Db", "evenOddRatioDb",
    "sustainAmplitudeVariation", "isSilent", "isClipped", "warnings",
]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", required=True, help="Directory containing WAV renders")
    ap.add_argument("--out", required=True, help="Output analysis directory")
    args = ap.parse_args()

    input_dir = Path(args.input)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    provenance = load_provenance(input_dir)
    if not HAVE_NUMPY:
        print("note: numpy not available - spectral centroid and pitch will be null")

    rows = []
    for wav_path in sorted(input_dir.glob("*.wav")):
        result = analyze_file(wav_path)
        result["provenance"] = provenance.get(wav_path.name, {})
        (out_dir / f"{wav_path.stem}.json").write_text(
            json.dumps(result, indent=2) + "\n", encoding="utf-8")
        rows.append(result)
        warn = f"  warnings: {', '.join(result['warnings'])}" if result["warnings"] else ""
        print(f"analyzed {wav_path.name}{warn}")

    if rows:
        with (out_dir / "summary.csv").open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=CSV_COLUMNS)
            writer.writeheader()
            for row in rows:
                flat = {**row, **{k: row["provenance"].get(k) for k in
                                  ("presetId", "suiteId", "testId", "parametersHash", "engineGitCommit")}}
                flat["warnings"] = ";".join(row["warnings"])
                writer.writerow({k: flat.get(k) for k in CSV_COLUMNS})

    print(f"Analyzed {len(rows)} WAV file(s) into {out_dir}")


if __name__ == "__main__":
    main()
