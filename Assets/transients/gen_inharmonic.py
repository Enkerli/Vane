#!/usr/bin/env python3
"""Generate CC0 inharmonic (noise-based) transient samples for Vane.
All are synthesised from filtered noise + fast envelopes — no harmonic series,
so they layer cleanly under any oscillator pitch and need no pitch-tracking.
Run from repo root: python3 Assets/transients/gen_inharmonic.py
"""
import numpy as np
from scipy import signal
from scipy.io import wavfile
import os

SR = 48000
rng = np.random.default_rng(20260602)
HERE = os.path.dirname(os.path.abspath(__file__))

def noise(n):           # white noise, deterministic
    return rng.standard_normal(n)

def bp(x, lo, hi):      # bandpass
    sos = signal.butter(4, [lo/(SR/2), hi/(SR/2)], 'bandpass', output='sos')
    return signal.sosfilt(sos, x)

def hp(x, f):
    sos = signal.butter(4, f/(SR/2), 'highpass', output='sos'); return signal.sosfilt(sos, x)

def lp(x, f):
    sos = signal.butter(4, f/(SR/2), 'lowpass', output='sos'); return signal.sosfilt(sos, x)

def env(n, atk_ms, dec_ms):
    a = int(SR*atk_ms/1000); d = n - a
    e = np.ones(n)
    if a > 0: e[:a] = np.linspace(0, 1, a)
    e[a:] = np.exp(-np.arange(d) / (SR*dec_ms/1000))
    return e

def finish(x, dur_ms):
    n = int(SR*dur_ms/1000); x = x[:n]
    # 3ms tail fade to zero (avoid click), normalise to 0.85
    f = int(SR*0.003); x = x.copy(); x[-f:] *= np.linspace(1, 0, f)
    x *= 0.85/(np.abs(x).max()+1e-9)
    return np.clip(np.round(x*32768), -32768, 32767).astype(np.int16)

def write(name, x16):
    p = os.path.join(HERE, name); wavfile.write(p, SR, x16)
    a = np.abs(x16.astype(float)/32768); fl = int(np.argmax(a>0.1))
    print(f'{name:20s} {len(x16)/SR*1000:5.0f}ms peak={a.max():.2f} firstLoud@{fl/SR*1000:.1f}ms')

# 1. Tongue "tut" — bandpass noise, very fast, wind articulation
n=int(SR*0.09); x=bp(noise(n),700,2200)*env(n,1,28); write('gen_tongue.wav', finish(x,90))

# 2. Key click — broadband, ultra short, mechanical
n=int(SR*0.02); x=hp(noise(n),1500)*env(n,0.2,5);   write('gen_click.wav', finish(x,18))

# 3. Air chiff — highpassed breath noise, soft onset
n=int(SR*0.13); x=hp(noise(n),2500)*env(n,5,75);    write('gen_chiff.wav', finish(x,130))

# 4. Wood knock — lowpass resonant noise, woody thunk
n=int(SR*0.10); x=lp(bp(noise(n),300,1400),1800)*env(n,0.5,40); write('gen_knock.wav', finish(x,95))

# 5. Pick noise — bright bandpass burst, string friction
n=int(SR*0.05); x=bp(noise(n),2500,6000)*env(n,0.3,16); write('gen_pick.wav', finish(x,45))

# 6. Reed buzz — gritty mid noise (soft-clipped), reedy bite (inharmonic)
n=int(SR*0.10); x=bp(noise(n),900,3000); x=np.tanh(x*3.0); x*=env(n,2,42); write('gen_buzz.wav', finish(x,95))
