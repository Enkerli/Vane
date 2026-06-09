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

    // vowelPos/amount/reso/move all 0..1.  Call once per block (cheap); the heavy
    // coefficient update only re-runs when the morphed vowel actually moves.
    void setParams (float vowelPos_, float amount_, float reso_, float move_)
    {
        amount = juce::jlimit (0.0f, 1.0f, amount_);
        reso   = juce::jlimit (0.0f, 1.0f, reso_);
        move   = juce::jlimit (0.0f, 1.0f, move_);
        targetPos = juce::jlimit (0.0f, 1.0f, vowelPos_);
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

    // ── Vowel formant table (F1..F5): frequency Hz, gain dB, bandwidth Hz ──────
    // Classic CSound "tenor" sung-vowel values — the widely-used reference set
    // that gives convincing, recognisable vowels.
    struct Vowel { float f[N]; float gDb[N]; float bw[N]; };
    static constexpr int kNumVowels = 5;   // A E I O U
    static const Vowel& vowels (int i)
    {
        static const Vowel V[kNumVowels] = {
            /* A */ {{ 650.f, 1080.f, 2650.f, 2900.f, 3250.f }, { 0.f,  -6.f,  -7.f,  -8.f, -22.f }, { 80.f,  90.f, 120.f, 130.f, 140.f }},
            /* E */ {{ 400.f, 1700.f, 2600.f, 3200.f, 3580.f }, { 0.f, -14.f, -12.f, -14.f, -20.f }, { 70.f,  80.f, 100.f, 120.f, 120.f }},
            /* I */ {{ 290.f, 1870.f, 2800.f, 3250.f, 3540.f }, { 0.f, -15.f, -18.f, -20.f, -30.f }, { 40.f,  90.f, 100.f, 120.f, 120.f }},
            /* O */ {{ 400.f,  800.f, 2600.f, 2800.f, 3000.f }, { 0.f, -10.f, -12.f, -12.f, -26.f }, { 70.f,  80.f, 100.f, 130.f, 135.f }},
            /* U */ {{ 350.f,  600.f, 2700.f, 2900.f, 3300.f }, { 0.f, -20.f, -17.f, -14.f, -26.f }, { 40.f,  60.f, 100.f, 120.f, 120.f }},
        };
        return V[juce::jlimit (0, kNumVowels - 1, i)];
    }

    void updateCoeffsIfMoved (float pos)
    {
        // re-tune only on meaningful movement (avoids per-sample tan/cos cost)
        if (std::abs (pos - lastPos) < 1.0e-4f && reso == lastReso && mode == lastMode) return;
        lastPos = pos; lastReso = reso; lastMode = mode;

        if (mode == Mode::Wah)
        {
            // one resonant band swept 300 Hz → 3 kHz (log); Bite → Q.
            const float f = 300.0f * std::pow (10.0f, juce::jlimit (0.0f, 1.0f, pos));
            const float q = 2.0f + reso * 18.0f;
            band[0].setBandpass (f, q, sr);
            return;
        }

        // morph A→E→I→O→U
        const float seg = pos * (kNumVowels - 1);
        const int   i0  = juce::jlimit (0, kNumVowels - 2, (int) seg);
        const float t   = seg - (float) i0;
        const auto& a = vowels (i0);
        const auto& b = vowels (i0 + 1);

        // reso narrows bandwidth (higher Q / more bite): scale bw down to ~35%.
        const float bwScale = 1.0f - 0.65f * reso;

        for (int i = 0; i < N; ++i)
        {
            const float f  = std::exp (juce::jmap (t, std::log (a.f[i]),  std::log (b.f[i])));
            const float g  = juce::jmap (t, a.gDb[i], b.gDb[i]);
            const float bw = juce::jmap (t, a.bw[i],  b.bw[i]) * bwScale;
            const float q  = juce::jmax (0.5f, f / juce::jmax (1.0f, bw));
            band[i].setBandpass (f, q, sr);
            gLin[i] = std::pow (10.0f, g / 20.0f);
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
    float targetPos = 0.0f, lastPos = -1.0f, lastReso = -1.0f;
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
