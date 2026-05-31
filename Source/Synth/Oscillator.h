#pragma once
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>
#include "Wavetable.h"

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

    // Point the morph engine at a wavetable (shared, owned elsewhere).  nullptr →
    // fall back to the built-in analytic morph.  Same kTableSize/mip layout, so
    // the existing pitch-based mip selection and phase handoff are unaffected —
    // legato, MPE and MTS-ESP keep working unchanged.
    void setWavetable(const Wavetable* w) { wt = w; }

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

    // Continuous-morph wavetable read with optional phase-distortion pulse width
    // and FM-approximation inharmonicity.
    //
    // morphPos — 0.0..3.0, crossfades through the waveform sequence:
    //   0 = Sine   1 = Triangle   2 = Square   3 = Sawtooth
    // pw — 0.0..1.0, phase distortion duty cycle; 0.5 = identity (no warp).
    //   < 0.5: narrow pulse / faster first-half traversal of the table.
    //   > 0.5: wide pulse  / slower first-half traversal.
    // inharm — 0.0..1.0, inharmonicity (FM index).  0 = harmonic (no modulation).
    //   The table read position is phase-modulated by a sine running at a NON-
    //   integer ratio (kInharmRatio = √2) to the carrier, so the sidebands fall
    //   at f0·(1 ± k·√2) — inharmonic, giving bell/metallic timbres that still
    //   track pitch.  inharm scales the modulation depth (index) 0 → kInharmMaxDepth.
    //
    // Implementation:
    //   1. PD warp: piecewise-linear remap of phase → warpedPhase.
    //   2. PM: add inharm-scaled sine modulator to the read position.
    //   3. Mip select: cachedMipLevel from the last setFrequency() call.
    //   4. Crossfade: lerp between the two adjacent mip-level tables.
    //
    // Note on aliasing: both the PD warp and the FM widen the spectrum beyond the
    // band-limited table, so extreme PW or high inharm can alias above Nyquist.
    // The mip tables limit the carrier and the downstream filter tames the rest;
    // a future improvement would be to drop the carrier a mip level when inharm is
    // high, and/or oversample (same trade-off noted for the wavefolder).
    float nextMorphed(float morphPos, float pw, float inharm) {
        // ── PD warp ────────────────────────────────────────────────────────────
        // Clamp pw so neither branch divides by zero.
        pw = (pw < 0.001f) ? 0.001f : (pw > 0.999f ? 0.999f : pw);
        float wp;
        if (phase < pw)
            wp = phase * (0.5f / pw);
        else
            wp = 0.5f + (phase - pw) * (0.5f / (1.0f - pw));

        // ── Inharmonicity (phase modulation) ────────────────────────────────────
        // Modulate the read position by a √2-ratio sine.  The modulator phase
        // always advances (cheap) so it stays coherent; the std::sin and the
        // position shift are gated behind a non-zero index.
        if (inharm > 1.0e-4f) {
            const float m = std::sin(juce::MathConstants<float>::twoPi * pmPhase);
            wp += inharm * kInharmMaxDepth * m;
            wp -= std::floor(wp);                 // wrap to [0,1)
        }
        pmPhase += kInharmRatio * phaseInc;
        if (pmPhase >= 1.0f) pmPhase -= std::floor(pmPhase);

        // ── Select the two morph frames + their band-limited tables ────────────
        // A loaded wavetable's frames are scanned in their own order across
        // morphPos 0..3 (0 = first frame, 3 = last).  Without one, fall back to
        // the analytic morph (Sine→Tri→Square→Saw via kMorphOrder).
        const float* tbA;
        const float* tbB;
        float        blend;
        const float m01 = (morphPos < 0.0f) ? 0.0f : (morphPos > 1.0f ? 1.0f : morphPos);  // normalised
        if (wt != nullptr && wt->numFrames() > 0) {
            const int nf = wt->numFrames();
            float pos = m01 * static_cast<float>(nf - 1);        // → 0..nf-1
            int iA = static_cast<int>(pos);
            if (iA > nf - 2) iA = (nf >= 2 ? nf - 2 : 0);
            blend = (nf >= 2) ? pos - static_cast<float>(iA) : 0.0f;
            tbA = wt->table(iA, cachedMipLevel);
            tbB = wt->table((nf >= 2 ? iA + 1 : iA), cachedMipLevel);
        } else {
            float pos = m01 * static_cast<float>(kNumMorphFrames - 1);   // → 0..3 across the analytic set
            int   iA  = static_cast<int>(pos);
            if (iA > kNumMorphFrames - 2) iA = kNumMorphFrames - 2;      // iA+1 always valid
            blend = pos - static_cast<float>(iA);
            tbA = s_tables[kMorphOrder[iA]][cachedMipLevel];
            tbB = s_tables[kMorphOrder[iA + 1]][cachedMipLevel];
        }

        // ── Table lookup (linear interpolation, guard-point safe) ──────────────
        float       idx  = wp * static_cast<float>(kTableSize);
        int         i0   = static_cast<int>(idx);
        if (i0 >= kTableSize) i0 = kTableSize - 1;
        float       frac = idx - static_cast<float>(i0);
        float sA = tbA[i0] + frac * (tbA[i0 + 1] - tbA[i0]);
        float sB = tbB[i0] + frac * (tbB[i0 + 1] - tbB[i0]);

        float out = sA + blend * (sB - sA);

        phase += phaseInc;
        if (phase >= 1.0f) phase -= std::floor(phase);

        return out;
    }

    // ── Wavefolder ──────────────────────────────────────────────────────────────
    // Reflective triangle fold applied to an arbitrary input sample (not just this
    // oscillator's output — SynthVoice folds the post-noise-blend signal pre-filter).
    //
    // drive — multiplies the input before folding.  drive ≤ 1 is transparent
    //   (returns x unchanged, since the input is bounded to [-1,1]); higher drive
    //   reflects the signal back inside [-1, 1] every time it would exceed the
    //   rails, multiplying harmonic content ("West-Coast"/Serge-style timbre).
    //   Output is always bounded to [-1, 1].  Callers map a 0..1 "amount" knob to
    //   drive via foldDrive() — see below.
    //
    // Closed-form triangle (no audio-thread loops): for the scaled input s,
    //   y = ((s − 1) mod 4) ∈ [0,4) ;  fold = |y − 2| − 1
    // which is exactly the reflective fold and is the identity on [-1, 1] at
    // drive = 1, so the control is continuous as drive leaves 1.
    //
    // Aliasing note: the fold corners are non-differentiable, so high drive can
    // alias.  The downstream filter tames most of it; a future improvement would
    // be to 2× oversample just this stage (mirroring the cubic-interp TODO above).
    static float wavefold(float x, float drive) {
        if (drive <= 1.0001f) return x;               // transparent (no fold)
        const float s = x * drive;
        float y = std::fmod(s - 1.0f, 4.0f);
        if (y < 0.0f) y += 4.0f;                       // wrap into [0,4)
        return std::fabs(y - 2.0f) - 1.0f;
    }

    // Maps a 0..1 fold amount to the wavefolder drive.  EXPONENTIAL by design:
    // the perceived fold brightness grows ≈ logarithmically with drive (each unit
    // of drive adds a roughly fixed number of fold-overs, but the ear hears
    // harmonic richness on a log scale).  An exponential amount→drive map cancels
    // that, so brightness grows ~linearly with the amount knob — the audible
    // change is spread evenly across the control instead of bunching near the
    // bottom (the symptom of a linear drive, worst on near-sinusoidal inputs
    // whose peaks cross the first fold threshold abruptly).
    //   amount 0 → drive 1 (transparent) ; amount 1 → drive kFoldDriveMax.
    // Called once per block (not per sample) — std::pow is too costly per sample.
    static float foldDrive(float amount) {
        constexpr float kFoldDriveMax = 8.0f;
        return std::pow(kFoldDriveMax, amount);
    }

    // Returns the phase that will be used on the NEXT call to next() (i.e. the
    // phase after the last advance).  SynthVoice reads this at the dying voice
    // to hand the exact phase to the new legato voice via reset().
    float getPhase() const { return phase; }

    // Set the starting phase (0..1).  Called at note-on for legato handoff.
    void reset(float startPhase = 0.0f) { phase = startPhase; }

    // Inharmonicity FM modulator phase — published/restored for legato handoff so
    // the modulator stays continuous across a mono-legato voice change.
    float getPmPhase() const { return pmPhase; }
    void  setPmPhase(float p) { pmPhase = p; }

private:
    // ── Table generation (Oscillator.cpp) ────────────────────────────────────
    static void initTables();

    // ── Table dimensions ─────────────────────────────────────────────────────
    static constexpr int kTableSize    = 2048;  // power of 2 for cheap index masking
    static constexpr int kNumMipLevels = 11;    // levels 0..10 → 1..1024 harmonics
    static constexpr int kNumWaveforms = 4;     // analytic frames via the Waveform enum: Sine0 Tri1 Saw2 Sqr3
    static constexpr int kNumFrames    = 4;     // total morph frames (== waveforms today; grows in Step 2)

    // Morph scan order — frame indices into s_tables, length kNumMorphFrames.
    // The morph param spans 0..kNumMorphFrames-1.  Sine→Tri→Square→Saw today.
    static constexpr int kNumMorphFrames = 4;
    static constexpr int kMorphOrder[kNumMorphFrames] = { 0, 1, 3, 2 };

    // Shared table storage.  C++17 inline static — one definition, all TUs share it.
    // +1 guard point per level: tbl[kTableSize] = tbl[0] for wrap-safe interpolation.
    // Zero-initialised; filled by initTables() on first prepare().
    inline static float s_tables[kNumFrames][kNumMipLevels][kTableSize + 1] {};

    // ── Inharmonicity (FM) constants ──────────────────────────────────────────
    // kInharmRatio — modulator : carrier frequency ratio.  √2 is irrational, so
    //   the FM sidebands never line up with the harmonic series → maximally
    //   inharmonic (bell/metallic).  A future param could expose this for
    //   variety (small-integer ratios give more "harmonic" FM).
    // kInharmMaxDepth — read-position shift (in cycles) at inharm = 1.0.  0.5 of a
    //   cycle ≈ index π — a strong but musical maximum.
    static constexpr float kInharmRatio    = 1.41421356f;
    static constexpr float kInharmMaxDepth = 0.5f;

    // ── Per-instance state ────────────────────────────────────────────────────
    Waveform waveform       = Waveform::Saw;
    float    sr             = 44100.0f;
    float    phase          = 0.0f;
    float    phaseInc       = 0.0f;
    float    pmPhase        = 0.0f;   // inharmonicity modulator phase (free-running)
    int      cachedMipLevel = 0;
    uint64_t noiseState     = 12345u;
    const Wavetable* wt     = nullptr;   // active morph wavetable (nullptr = analytic)
};
