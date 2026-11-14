"""Shared helpers for the WindCore bench: build, MIDI to events, render, read audio, measure."""
import os, subprocess, tempfile, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BIN = os.path.join(ROOT, "build", "windcore-render")
CAL = os.path.join(ROOT, "cal.txt")
CURVES = os.path.join(ROOT, "curves.txt")   # the horn across the range; used by every render when present


def build():
    os.makedirs(os.path.dirname(BIN), exist_ok=True)
    src = os.path.join(ROOT, "cli", "windcore-render.cpp")
    hdr = os.path.join(ROOT, "WindCore.h")
    deps = [src, hdr] + [os.path.join(ROOT, f) for f in ("WindCharacter.h", "WindCoreData.h", "VoiceTracker.h") if os.path.exists(os.path.join(ROOT, f))]
    if os.path.exists(BIN) and os.path.getmtime(BIN) > max(os.path.getmtime(f) for f in deps):
        return
    subprocess.check_call(["clang++", "-std=c++17", "-O2", "-Wall", "-o", BIN + ".new", src])
    os.replace(BIN + ".new", BIN)


def midi_events(path, transpose=0):
    import mido
    ev, t = [], 0.0
    for m in mido.MidiFile(path):
        t += m.time
        if m.type == "note_on" and m.velocity > 0:
            ev.append((t, "on", m.note + transpose))
        elif m.type == "note_off" or (m.type == "note_on" and m.velocity == 0):
            ev.append((t, "off", m.note + transpose))
        elif m.type == "control_change" and m.control == 2:
            ev.append((t, "breath", m.value / 127.0))
        elif m.type == "pitchwheel":
            ev.append((t, "bend", m.pitch / 8192.0 * 2.0))
    ev.append((t + 0.6, "end", 0))
    return ev


def held(note, breath=0.6, dur=1.2, ramp=0.03):
    return [(0.05, "breath", 0.0), (0.05, "on", note), (0.05 + ramp, "breath", breath),
            (0.05 + dur, "breath", breath), (0.05 + dur + 0.03, "breath", 0.0),
            (0.05 + dur + 0.03, "off", note), (0.05 + dur + 0.3, "end", 0)]


def render(events, out=None, params=None, cal=True, rate=48000, probe=None, controls=None, curves=True, voice=None, voice_log=None):
    build()
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        for e in sorted(events, key=lambda e: e[0]):
            f.write(" ".join(str(v) for v in e) + "\n")
        evp = f.name
    tmp = out is None
    if tmp:
        out = tempfile.mktemp(suffix=".wav")
    cmd = [BIN, evp, out, "--rate", str(rate)]
    if isinstance(cal, str):
        cmd += ["--cal", cal]
    elif cal and os.path.exists(CAL):
        cmd += ["--cal", CAL]
    if probe:
        cmd += ["--probe", probe]
    if isinstance(curves, str):
        cmd += ["--curves", curves]
    elif curves and controls is None and os.path.exists(CURVES):
        cmd += ["--curves", CURVES]
    if voice:
        cmd += ["--voice", voice]
    if voice_log:
        cmd += ["--voice-log", voice_log]
    if controls is not None:
        cmd += ["--controls", ",".join("%s=%r" % kv for kv in controls.items()) or ","]
    for k, v in (params or {}).items():
        cmd.append(f"{k}={v}")
    subprocess.check_call(cmd)
    x = read_wav(out)[0]
    os.unlink(evp)
    if tmp:
        os.unlink(out)
    return x


def read_wav(path):
    with open(path, "rb") as f:
        d = f.read()
    import struct
    pos, fmt, rate, ch, bits = 12, 1, 48000, 1, 16
    while pos < len(d) - 8:
        cid, size = d[pos:pos + 4], struct.unpack("<I", d[pos + 4:pos + 8])[0]
        body = d[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt, ch, rate = struct.unpack("<HHI", body[:8]); bits = struct.unpack("<H", body[14:16])[0]
        elif cid == b"data":
            if fmt == 3:
                x = np.frombuffer(body, "<f4").astype(float)
            elif bits == 16:
                x = np.frombuffer(body, "<i2") / 32768.0
            elif bits == 24:
                b = np.frombuffer(body, np.uint8).reshape(-1, 3).astype(np.int32)
                v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
                x = np.where(v >= 1 << 23, v - (1 << 24), v) / 8388608.0
            else:
                x = np.frombuffer(body, "<i4") / 2147483648.0
            return x.reshape(-1, ch).mean(axis=1), rate
        pos += 8 + size + (size & 1)
    raise ValueError("no data chunk in " + path)


def write_wav16(path, x, rate=48000):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())


def f0_of(x, rate, guess):
    """Fundamental near guess (Hz) by autocorrelation with parabolic refinement."""
    x = x - x.mean()
    n = len(x)
    spec = np.fft.rfft(x * np.hanning(n), 1 << int(np.ceil(np.log2(2 * n))))
    ac = np.fft.irfft(abs(spec) ** 2)
    lo, hi = int(rate / (guess * 1.45)), int(rate / (guess / 1.45))
    k = lo + int(np.argmax(ac[lo:hi]))
    a, b, c = ac[k - 1], ac[k], ac[k + 1]
    k += 0.5 * (a - c) / (a - 2 * b + c)
    return rate / k


def harmonics(x, rate, f0, n=12):
    """Harmonic levels in dB (peak amplitude), by projection on exact frequencies."""
    t = np.arange(len(x)) / rate
    w = np.hanning(len(x)); w /= w.sum()
    out = []
    for k in range(1, n + 1):
        if k * f0 > rate * 0.45:
            out.append(-200.0); continue
        out.append(20 * np.log10(2 * abs(np.sum(x * w * np.exp(-2j * np.pi * k * f0 * t))) + 1e-12))
    return np.array(out)


def db(v):
    return 20 * np.log10(max(v, 1e-12))


def note_name(n):
    return ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"][n % 12] + str(n // 12 - 1)
