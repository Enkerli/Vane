#!/usr/bin/env python3
"""Generate HarmonicStack.wav — the built-in default wavetable as a standard
single-cycle WT file (Serum-style: N frames x 2048 samples, mono 32-bit float).
Frame k = sum of the first (k+1) sawtooth harmonics (1/h), each frame peak-±1.
Matches Wavetable::makeHarmonicStack so Vane's loader round-trips it."""
import numpy as np
from scipy.io import wavfile

FRAME = 2048
N = 16
i = np.arange(FRAME)
frames = []
for k in range(N):
    cyc = np.zeros(FRAME)
    for h in range(1, k + 2):
        cyc += np.sin(2 * np.pi * h * i / FRAME) / h
    peak = np.max(np.abs(cyc))
    if peak > 1e-6:
        cyc /= peak
    frames.append(cyc.astype(np.float32))

data = np.concatenate(frames).astype(np.float32)
wavfile.write("Assets/HarmonicStack.wav", 44100, data)
print(f"wrote Assets/HarmonicStack.wav  {N} frames x {FRAME}  ({data.size} samples, float32)")
