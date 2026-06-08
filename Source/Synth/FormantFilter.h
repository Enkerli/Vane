#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <cstdint>

// FormantFilter — a 3-band vowel/formant resonator that runs in series AFTER the
// per-voice SVF (osc → noise → fold → SVF → *formant* → VCA).  Per voice, so a
// single held note behaves like a talkbox "mouth" while a rotating chord becomes
// a vocal ensemble (each note its own vowel).
//
// Topology: three RBJ constant-0 dB-peak band-pass biquads at the vowel's F1/F2/F3,
// summed with per-formant gains.  vowelPos (0..1) morphs continuously across
// A–E–I–O–U (log-interp on frequency, linear on gain/bandwidth).  `reso` narrows
// the bands (talkbox bite); `amount` is dry→formant mix; `move` adds a slow,
// per-instance random drift to vowelPos for life/movement.
//
// mode is scaffolded for the follow-ups in this thread (sordina Mute, Wah);
// only Vowel is implemented now — Mute/Wah will reuse the same biquad bank.
//
// DSP note: gains/Q below are sensible starting values; final balance wants
// ear-tuning on the target (can't audition here).  Output is conservatively
// trimmed so high `reso` + low F1 vowels don't clip.
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

        float wet = 0.0f;
        for (int i = 0; i < N; ++i)
            wet += band[i].process (x) * gLin[i];
        wet *= outTrim;

        return x + amount * (wet - x);
    }

private:
    static constexpr int N = 3;

    // ── Vowel formant table (F1..F3): frequency Hz, gain dB, bandwidth Hz ──────
    // Neutral "tenor/bass" sung-vowel values (classic CSound-style table, F1–F3).
    struct Vowel { float f[N]; float gDb[N]; float bw[N]; };
    static constexpr int kNumVowels = 5;   // A E I O U
    static const Vowel& vowels (int i)
    {
        static const Vowel V[kNumVowels] = {
            /* A */ {{ 800.f, 1150.f, 2800.f }, {  0.f,  -6.f, -12.f }, {  80.f, 110.f, 160.f }},
            /* E */ {{ 400.f, 1700.f, 2600.f }, {  0.f,  -8.f, -14.f }, {  70.f, 120.f, 160.f }},
            /* I */ {{ 320.f, 2100.f, 2900.f }, {  0.f, -12.f, -16.f }, {  60.f, 120.f, 160.f }},
            /* O */ {{ 450.f,  800.f, 2830.f }, {  0.f, -10.f, -16.f }, {  70.f, 100.f, 160.f }},
            /* U */ {{ 325.f,  700.f, 2530.f }, {  0.f, -16.f, -20.f }, {  60.f, 100.f, 160.f }},
        };
        return V[juce::jlimit (0, kNumVowels - 1, i)];
    }

    void updateCoeffsIfMoved (float pos)
    {
        // morph A→E→I→O→U
        const float seg = pos * (kNumVowels - 1);
        const int   i0  = juce::jlimit (0, kNumVowels - 2, (int) seg);
        const float t   = seg - (float) i0;
        const auto& a = vowels (i0);
        const auto& b = vowels (i0 + 1);

        // re-tune only on meaningful movement (avoids per-sample tan/cos cost)
        if (std::abs (pos - lastPos) < 1.0e-4f && reso == lastReso) return;
        lastPos = pos; lastReso = reso;

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

    // makeup so summed formants sit near unity without clipping at high reso
    static constexpr float outTrim = 1.6f;

    // drift (movement)
    std::uint64_t rng = 0x9E3779B97F4A7C15ull;
    float drift = 0.0f, driftTarget = 0.0f;
    static constexpr float driftCoeff = 0.02f;   // one-pole smoothing toward target
    int driftCtr = 0;
    static constexpr int driftPeriod = 16000;    // ~3 S&H updates/sec @ 48k
};
