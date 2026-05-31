#include "Oscillator.h"
#include <mutex>
#include <cmath>
#include <algorithm>

// ── prepare ───────────────────────────────────────────────────────────────────
//
// Initialises per-instance state and triggers table generation exactly once
// across all plugin instances (via a function-local std::once_flag).
// Thread-safe per C++11: static-local construction is guaranteed atomic, and
// std::call_once serialises concurrent callers until initTables() returns.
void Oscillator::prepare(double sampleRate) {
    sr         = static_cast<float>(sampleRate);
    phase      = 0.0f;
    pmPhase    = 0.0f;
    noiseState = 12345u;

    static std::once_flag flag;
    std::call_once(flag, initTables);

    // Default the morph engine to the shared built-in wavetable.  The processor
    // can later point voices at a loaded table via SynthVoice/Oscillator.
    wt = &Wavetable::builtInDefault();
}

// ── initTables ────────────────────────────────────────────────────────────────
//
// Populates s_tables[waveform][level][sample] with band-limited waveforms.
//
// ── Waveform Fourier series ───────────────────────────────────────────────────
//
//  Sine     (w=0): h=1 only, amplitude=1.  All mip levels are identical.
//
//  Triangle (w=1): odd harmonics only.
//                  a_h = (-1)^((h-1)/2) / h²    (h = 1, 3, 5, …)
//                  h=1: +1   h=3: −1/9   h=5: +1/25  …
//
//  Sawtooth (w=2): all harmonics.
//                  a_h = 1/h   (gives a falling-ramp shape; same spectral content
//                  as a rising ramp — the sign difference is normalised away)
//
//  Square   (w=3): odd harmonics only.
//                  a_h = 1/h   (h = 1, 3, 5, …)
//
//  Each table is normalised to peak ±1 after accumulation so that the
//  exact scaling factors (2/π, 4/π, 8/π²) have no audible effect.
//
// ── Angle accumulation ────────────────────────────────────────────────────────
//
// Instead of calling std::sin(2π·h·i/N) for every (h, i) pair — O(N·H) calls —
// we use a (sin, cos) rotation pair that advances by ω = 2π·h/N per sample:
//
//   sin((i+1)ω) = sin(iω)·cos(ω) + cos(iω)·sin(ω)
//   cos((i+1)ω) = cos(iω)·cos(ω) − sin(iω)·sin(ω)
//
// Two sin/cos calls per harmonic (for ω itself) instead of N calls.  Numerical
// drift over 2048 steps is negligible (< 1e-6 amplitude error per table).
// Per-harmonic amplitude that DEFINES each morph frame.  Frames 0..3 are the
// analytic waveforms; the band-limited mip generator in initTables() sums these
// up to each level's harmonic cap, so every frame is anti-aliased for free.
// New wavetable frames (Step 2) append cases here — or, later, a data-driven
// spectrum table — and the rest of the pipeline needs no changes.
static float frameHarmonicAmp(int frame, int h) {
    switch (frame) {
        case 0:  // Sine — fundamental only
            return (h == 1) ? 1.0f : 0.0f;
        case 1:  // Triangle — odd harmonics, alternating sign, 1/h²
            if (h % 2 == 1) { int ni = (h - 1) / 2; return (ni % 2 == 0 ? 1.0f : -1.0f) / static_cast<float>(h * h); }
            return 0.0f;
        case 2:  // Sawtooth — all harmonics, 1/h
            return 1.0f / static_cast<float>(h);
        case 3:  // Square — odd harmonics only, 1/h
            return (h % 2 == 1) ? 1.0f / static_cast<float>(h) : 0.0f;
        default:
            return 0.0f;
    }
}

void Oscillator::initTables() {
    constexpr float twoPi = 2.0f * 3.14159265358979f;

    for (int k = 0; k < kNumMipLevels; ++k) {
        // Level k stores min(2^k, kTableSize/2) harmonics.
        // 2^k is safe for k ≤ 10 (max 1024), and kTableSize/2 = 1024 caps it.
        int numHarmonics = std::min(1 << k, kTableSize / 2);

        for (int w = 0; w < kNumFrames; ++w) {
            float* tbl = s_tables[w][k];

            // Zero fill (including guard point slot)
            for (int i = 0; i <= kTableSize; ++i) tbl[i] = 0.0f;

            for (int h = 1; h <= numHarmonics; ++h) {
                const float amp = frameHarmonicAmp(w, h);
                if (amp == 0.0f) continue;

                // ── Angle accumulation for sin(2π·h·i / kTableSize) ──────
                const float omega    = twoPi * static_cast<float>(h)
                                             / static_cast<float>(kTableSize);
                const float sinOmega = std::sin(omega);
                const float cosOmega = std::cos(omega);

                float s = 0.0f;   // sin(0) = 0
                float c = 1.0f;   // cos(0) = 1

                for (int i = 0; i < kTableSize; ++i) {
                    tbl[i] += amp * s;

                    // Advance by ω: (s, c) → (s·cosω + c·sinω, c·cosω − s·sinω)
                    float ns = s * cosOmega + c * sinOmega;
                    float nc = c * cosOmega - s * sinOmega;
                    s = ns;
                    c = nc;
                }
            }

            // ── Normalise to peak ±1 ──────────────────────────────────────
            float peak = 0.0f;
            for (int i = 0; i < kTableSize; ++i)
                peak = std::max(peak, std::abs(tbl[i]));
            if (peak > 1e-6f) {
                const float inv = 1.0f / peak;
                for (int i = 0; i < kTableSize; ++i)
                    tbl[i] *= inv;
            }

            // ── Guard point ────────────────────────────────────────────────
            // Mirrors tbl[0] into tbl[kTableSize] so that linear interpolation
            // at the very end of the table (i0 = kTableSize−1, frac > 0) blends
            // correctly into the start of the next cycle without a special case.
            tbl[kTableSize] = tbl[0];
        }
    }
}
