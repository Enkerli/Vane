#pragma once
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>

// Mip-mapped single-cycle wavetable oscillator.
//
// Replaces the previous PolyBLEP analytical oscillator with the same public
// interface.  Every waveform except Noise is pre-rendered into a set of 11
// band-limited mip levels; the level is selected per note based on the current
// frequency so no harmonic ever exceeds Nyquist.
//
// ── Mip levels ────────────────────────────────────────────────────────────────
// Level k stores min(2^k, 1024) harmonics.  setFrequency() selects the
// highest k where 2^k ≤ floor(sr / (2·f)), meaning the top partial is always
// below Nyquist.  Level 0 → 1 harmonic (pure sine regardless of waveform),
// level 10 → 1024 harmonics (full bandwidth for low notes).
//
// ── Table format ──────────────────────────────────────────────────────────────
// s_tables[waveform][level][sample] — kTableSize=2048 samples + 1 guard point.
// The guard point (index kTableSize) is a copy of index 0 so that linear
// interpolation works seamlessly at the phase wrap without a branch.
//
// ── Thread safety ─────────────────────────────────────────────────────────────
// Table data is generated exactly once (the first call to prepare()), protected
// by a function-local std::once_flag inside prepare() in Oscillator.cpp.
// All subsequent calls are reads — no locking needed during audio rendering.
//
// ── Noise ─────────────────────────────────────────────────────────────────────
// Waveform::Noise bypasses the table entirely and uses the same LCG PRNG as
// before.  It is pitch-independent and does not use cachedMipLevel.
//
// ── Future improvements ───────────────────────────────────────────────────────
// • next() currently uses linear interpolation; cubic (4-point Hermite) would
//   reduce the noise floor by ~24 dB at the cost of 3 extra multiplies.
// • Phase distortion (PWM for square → pulse, or PD warp in general) should be
//   applied before the table lookup so the mip selection remains correct.
// • When Waveform::Noise is in use, setFrequency() still updates cachedMipLevel
//   unnecessarily — harmless but could be guarded.
class Oscillator {
public:
    enum class Waveform { Sine, Triangle, Saw, Square, Noise };

    // Thread-safe lazy initialisation of the shared table data (once per process).
    // Also resets phase and noise state for this instance.
    void prepare(double sampleRate);

    // Update oscillator frequency and select the appropriate mip level.
    // hz is clamped to [1, sr·0.9999] to prevent phase-increment blow-up.
    void setFrequency(float hz) {
        hz = std::max(hz, 1.0f);
        phaseInc = juce::jlimit(0.0f, 0.9999f, hz / sr);

        // Highest mip level k where 2^k ≤ floor(sr / (2·hz)).
        // At high frequencies (near Nyquist) this converges to level 0 (1 harmonic
        // = pure sine).  At low frequencies it reaches level 10 (1024 harmonics).
        int maxHarm = static_cast<int>(sr * 0.5f / hz);
        if (maxHarm < 1) maxHarm = 1;
        int k = 0;
        while (k < kNumMipLevels - 1 && (1 << (k + 1)) <= maxHarm)
            ++k;
        cachedMipLevel = k;
    }

    void setWaveform(Waveform w) { waveform = w; }

    // Advance by one sample and return the output value.
    // Hot path — kept inline to let the compiler fold it into the voice loop.
    float next() {
        float out;

        if (waveform == Waveform::Noise) {
            // 64-bit LCG — same as the previous PolyBLEP oscillator.
            noiseState = noiseState * 6364136223846793005ULL + 1442695040888963407ULL;
            out = static_cast<float>(static_cast<int32_t>(noiseState >> 33)) / 2147483648.0f;
        } else {
            // Linear interpolation in the selected mip-level table.
            float       idx  = phase * static_cast<float>(kTableSize);
            int         i0   = static_cast<int>(idx);
            if (i0 >= kTableSize) i0 = kTableSize - 1;   // safety: phase should be < 1
            float       frac = idx - static_cast<float>(i0);
            const float* tbl = s_tables[static_cast<int>(waveform)][cachedMipLevel];
            out = tbl[i0] + frac * (tbl[i0 + 1] - tbl[i0]);
        }

        phase += phaseInc;
        if (phase >= 1.0f) phase -= std::floor(phase);   // handles phaseInc ≤ 1

        return out;
    }

    // Returns the phase that will be used on the NEXT call to next() (i.e. the
    // phase after the last advance).  SynthVoice reads this at the dying voice
    // to hand the exact phase to the new legato voice via reset().
    float getPhase() const { return phase; }

    // Set the starting phase (0..1).  Called at note-on for legato handoff.
    void reset(float startPhase = 0.0f) { phase = startPhase; }

private:
    // ── Table generation (Oscillator.cpp) ────────────────────────────────────
    static void initTables();

    // ── Table dimensions ─────────────────────────────────────────────────────
    static constexpr int kTableSize    = 2048;  // power of 2 for cheap index masking
    static constexpr int kNumMipLevels = 11;    // levels 0..10 → 1..1024 harmonics
    static constexpr int kNumWaveforms = 4;     // Sine=0 Tri=1 Saw=2 Sqr=3 (Noise=PRNG)

    // Shared table storage.  C++17 inline static — one definition, all TUs share it.
    // +1 guard point per level: tbl[kTableSize] = tbl[0] for wrap-safe interpolation.
    // Zero-initialised; filled by initTables() on first prepare().
    inline static float s_tables[kNumWaveforms][kNumMipLevels][kTableSize + 1] {};

    // ── Per-instance state ────────────────────────────────────────────────────
    Waveform waveform       = Waveform::Saw;
    float    sr             = 44100.0f;
    float    phase          = 0.0f;
    float    phaseInc       = 0.0f;
    int      cachedMipLevel = 0;
    uint64_t noiseState     = 12345u;
};
