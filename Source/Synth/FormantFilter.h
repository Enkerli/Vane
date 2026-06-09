#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <cstdint>

// FormantFilter — a 5-band vowel/formant resonator applied GLOBALLY on the summed
// post-mix output (after the per-voice SVF + VCA), in series with the synth.
//
// Global (not per-voice) on purpose: one persistent filter instance has no
// per-note reset, so mono legato stays seamless (a per-voice formant restarts
// cold each handoff → transient that destroys legato).  It's also the classic
// "master vowel filter" topology — a single resonant mouth over the whole sound.
//
// Topology: five RBJ constant-0 dB-peak band-pass biquads at the vowel's F1..F5,
// summed with per-formant gains.  vowelPos (0..1) morphs continuously across
// A–E–I–O–U (log-interp on frequency, linear on gain/bandwidth) — the "morphs
// across" behaviour of a proper vowel filter.  `reso` narrows the bands (talkbox
// bite); `amount` is dry→formant mix; `move` adds a slow random drift for life.
//
// Formant table = the classic CSound "tenor" sung-vowel values (5 formants:
// frequency / gain dB / bandwidth).  mode is scaffolded for Mute/Wah follow-ups.
//
// DSP note: outTrim/bite mapping are starting values; expect ear-tuning.
class FormantFilter {
public:
    enum class Mode { Vowel, Mute, Wah };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        // de-correlate the per-instance drift RNG (L/R, per voice) for stereo life
        rng = 0x2545F4914F6CDD1Dull ^ (std::uint64_t) reinterpret_cast<std::uintptr_t> (this);
        reset();
    }

    void reset()
    {
        for (auto& b : band) b = BiquadState{};
        drift = 0.0f;
    }

    // Articulatory vowel axes (all 0..1): open (close→open, drives F1), front
    // (back→front, drives F2), round (lip rounding, lowers F2/F3).  In Wah mode
    // only `open` is used (the sweep position).  Call once per block.
    void setParams (float open_, float front_, float round_,
                    float amount_, float reso_, float move_)
    {
        amount = juce::jlimit (0.0f, 1.0f, amount_);
        reso   = juce::jlimit (0.0f, 1.0f, reso_);
        move   = juce::jlimit (0.0f, 1.0f, move_);
        targetPos = juce::jlimit (0.0f, 1.0f, open_);
        front     = juce::jlimit (0.0f, 1.0f, front_);
        round     = juce::jlimit (0.0f, 1.0f, round_);
    }

    void setMode (Mode m) noexcept { mode = m; }

    // Per-sample.  Returns dry when amount==0 (and is effectively bypassable by
    // the caller when the stage is disabled).
    float process (float x)
    {
        if (amount <= 0.0001f) return x;

        // slow random drift (one-pole-smoothed S&H), depth = move
        if (move > 0.0001f)
        {
            // ~3 Hz update via a coarse counter keeps it cheap and slow
            if (++driftCtr >= driftPeriod)
            {
                driftCtr = 0;
                driftTarget = (whiteNoise() * 2.0f - 1.0f);
            }
            drift += (driftTarget - drift) * driftCoeff;
        }
        const float pos = juce::jlimit (0.0f, 1.0f, targetPos + drift * move * 0.5f);

        updateCoeffsIfMoved (pos);

        float wet;
        if (mode == Mode::Wah)
        {
            // single swept resonant band-pass (classic wah); auto-wah via move/mod.
            wet = band[0].process (x) * outTrim;
        }
        else   // Vowel (Mute reserved → falls back to vowel until implemented)
        {
            wet = 0.0f;
            for (int i = 0; i < N; ++i)
                wet += band[i].process (x) * gLin[i];
            wet *= outTrim;
        }

        return x + amount * (wet - x);
    }

private:
    static constexpr int N = 5;

    // log-interpolate a frequency by t∈[0,1] between lo and hi
    static float lerpHz (float lo, float hi, float t)
    {
        return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, t));
    }

    void updateCoeffsIfMoved (float pos)   // pos = the (drifted) `open` axis
    {
        // re-tune only on meaningful movement (avoids per-sample tan/cos cost)
        if (std::abs (pos - lastPos) < 1.0e-4f && front == lastFront && round == lastRound
            && reso == lastReso && mode == lastMode) return;
        lastPos = pos; lastFront = front; lastRound = round; lastReso = reso; lastMode = mode;

        if (mode == Mode::Wah)
        {
            // one resonant band swept 300 Hz → 3 kHz (log); Bite → Q.
            band[0].setBandpass (300.0f * std::pow (10.0f, juce::jlimit (0.0f, 1.0f, pos)),
                                 2.0f + reso * 18.0f, sr);
            return;
        }

        // ── Articulatory vowel space (the IPA chart, continuously) ────────────
        // open → F1 (close 270 Hz → open 850 Hz); front → F2 (back 700 → front
        // 2300); rounding pulls F2/F3 down (so front+round = /y/, the French "u").
        // F4/F5 are fixed "voice character" (tenor-ish).  Gains: F1 strongest,
        // rounding darkens F2.  Corners → i / a / u / y, etc.
        const float f1 = lerpHz (270.0f,  850.0f, pos);
        const float f2 = lerpHz (700.0f, 2300.0f, front) * (1.0f - 0.22f * round);
        const float f3 = 2550.0f * (1.0f - 0.10f * round);
        const float fHz[N]    = { f1, f2, f3, 3200.0f, 3580.0f };
        const float bwBase[N] = { 80.0f, 90.0f, 120.0f, 130.0f, 140.0f };
        const float gDb[N]    = { 0.0f, -7.0f - 6.0f * round, -9.0f, -12.0f, -22.0f };

        const float bwScale = 1.0f - 0.65f * reso;   // Bite narrows the bands
        for (int i = 0; i < N; ++i)
        {
            const float bw = bwBase[i] * bwScale;
            band[i].setBandpass (fHz[i], juce::jmax (0.5f, fHz[i] / juce::jmax (1.0f, bw)), sr);
            gLin[i] = std::pow (10.0f, gDb[i] / 20.0f);
        }
    }

    // ── RBJ band-pass, constant 0 dB peak gain ────────────────────────────────
    struct BiquadState {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        void setBandpass (float f, float q, float sr)
        {
            f = juce::jlimit (20.0f, sr * 0.49f, f);
            const float w0 = juce::MathConstants<float>::twoPi * f / sr;
            const float cw = std::cos (w0), sw = std::sin (w0);
            const float alpha = sw / (2.0f * q);
            const float a0 = 1.0f + alpha;
            b0 =  alpha / a0;
            b1 =  0.0f;
            b2 = -alpha / a0;
            a1 = (-2.0f * cw) / a0;
            a2 = (1.0f - alpha) / a0;
        }
        float process (float x)
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    float whiteNoise()
    {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return (float) ((rng >> 40) & 0xFFFFFF) / (float) 0xFFFFFF;
    }

    std::array<BiquadState, N> band;
    float gLin[N] { 1, 1, 1 };

    float sr = 48000.0f;
    float amount = 0.0f, reso = 0.5f, move = 0.0f;
    float front = 0.5f, round = 0.0f;
    float targetPos = 0.0f, lastPos = -1.0f, lastFront = -1.0f, lastRound = -1.0f, lastReso = -1.0f;
    Mode  mode = Mode::Vowel, lastMode = Mode::Vowel;

    // makeup so the summed (band-passed) formants sit near unity; 5 bands sum to
    // more than 3, so trim lower than before.  Tune by ear on the target.
    static constexpr float outTrim = 1.1f;

    // drift (movement)
    std::uint64_t rng = 0x9E3779B97F4A7C15ull;
    float drift = 0.0f, driftTarget = 0.0f;
    static constexpr float driftCoeff = 0.02f;   // one-pole smoothing toward target
    int driftCtr = 0;
    static constexpr int driftPeriod = 16000;    // ~3 S&H updates/sec @ 48k
};
