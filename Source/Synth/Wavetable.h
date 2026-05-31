#pragma once
#include <vector>
#include <array>
#include <cstddef>
#include <juce_core/juce_core.h>

// ── Band-limited multi-frame wavetable ────────────────────────────────────────
//
// A wavetable is N single-cycle frames.  The morph sweeps across frames; reading
// any frame at a played pitch must not alias.  We anti-alias exactly the way the
// analytic Oscillator does — by storing each frame as a set of mip levels, where
// level k contains at most 2^k harmonics — but here the harmonics come from an
// *arbitrary* imported frame via a DFT rather than a hard-coded spectrum.
//
//   build():  raw single-cycle PCM frame  →  DFT (harmonics)  →  per-mip additive
//             reconstruction capped at that level's harmonic count.
//
// This is the foundation of WT import (Stage 2a).  It is deliberately decoupled
// from the audio path: it owns no oscillator state, just the table data.  The
// oscillator will hold a pointer to one of these and read it (Stage 2b).
//
// Cost note: build() uses a direct O(N²) DFT — fine for tests and small tables.
// The real .wav loader (Stage 2c) will swap in juce::dsp::FFT so a 256-frame
// table builds in well under a second.
class Wavetable {
public:
    static constexpr int kTableSize    = 2048;  // samples per single cycle
    static constexpr int kNumMipLevels = 11;    // level k → ≤ 2^k harmonics (0..10)
    static constexpr int kMaxHarmonics = kTableSize / 2;   // Nyquist cap = 1024

    int numFrames() const { return (int) framesData.size(); }
    bool isValid()  const { return ! framesData.empty(); }

    // Pointer to (frame, level), length kTableSize + 1 (guard point = [0] copy).
    const float* table (int frame, int level) const {
        return framesData[(size_t) frame].levels[(size_t) level].data();
    }

    // Linear-interpolated read of a frame/level at phase 0..1 (guard-point safe).
    // Used by tests and as the per-sample primitive the oscillator will mirror.
    float read (int frame, int level, float phase) const {
        const float* t = table (frame, level);
        float idx  = phase * (float) kTableSize;
        int   i0   = (int) idx;
        if (i0 >= kTableSize) i0 = kTableSize - 1;
        float frac = idx - (float) i0;
        return t[i0] + frac * (t[i0 + 1] - t[i0]);
    }

    // Build from raw single-cycle frames (each exactly kTableSize samples, any
    // amplitude).  DC is removed; each (frame, level) is normalised to peak ±1.
    // phaseAlign gives all frames a common per-harmonic phase so the morph never
    // cancels (magnitudes/timbre untouched).  Returns false on bad input.
    bool build (const std::vector<std::vector<float>>& rawFrames, bool phaseAlign = false);

    // A built-in "harmonic stack" table: frame k = the first (k+1) harmonics of a
    // sawtooth (1/h).  A genuinely simplest→complex sweep (sine → fuller saw) and
    // a sensible default before any file is imported.
    static Wavetable makeHarmonicStack (int numFrames = 16);

    // The 4 analytic waveforms in morph order (Sine, Triangle, Square, Saw) as a
    // 4-frame table — reproduces the legacy morph for comparison/fallback.
    static Wavetable makeAnalyticDefault();

    // Process-wide default wavetable, built once (thread-safe static).  All
    // oscillators read this until a file is loaded.  Currently the harmonic stack.
    static const Wavetable& builtInDefault();

    // ── WAV interchange ───────────────────────────────────────────────────────
    // The de-facto WT format: a mono .wav of single-cycle frames concatenated,
    // frameSize samples each (Serum-style 2048 is the common convention).
    //
    // loadFromWav: total/frameSize frames; each is linear-resampled to kTableSize
    //   if frameSize differs, then band-limited via build().  Channel 0 only.
    //   Returns an invalid Wavetable on read failure / size mismatch.
    // saveToWav:   writes each frame's full-bandwidth cycle (top mip), kTableSize
    //   samples, as mono 32-bit float — round-trips through loadFromWav.
    static Wavetable loadFromWav (const juce::File& file, int frameSize = kTableSize,
                                  bool phaseAlign = false);
    bool saveToWav (const juce::File& file) const;

private:
    struct Frame {
        std::array<std::vector<float>, kNumMipLevels> levels;  // each kTableSize+1
    };
    std::vector<Frame> framesData;
};
