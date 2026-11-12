#pragma once
// Room: a small space around the instrument, as a close microphone hears it in a room.
// Early reflections (a few taps within 30 ms) and a diffuse tail from an eight-line feedback
// delay network (Householder matrix, per-line damping). Stereo out from a mono or stereo in.
// Mix 0 is a strict bypass: the input passes untouched and nothing is computed.
// No JUCE dependency; allocation only in prepare().
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

class Room
{
public:
    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const float scale = fs / 48000.0f;
        for (int k = 0; k < kLines; ++k)
        {
            len[k] = std::max (1, (int) std::lround (kLineMs[k] * 0.001f * 48000.0f * scale * kMaxSize));
            line[k].assign ((size_t) len[k] + 2, 0.0f);
        }
        early.assign ((size_t) std::lround (0.04f * fs) + 2, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        std::fill (early.begin(), early.end(), 0.0f);
        pos.fill (0); damp.fill (0.0f); ePos = 0;
    }

    /** mix 0..1 (wet share), size 0..1 (small booth to a large room), tone 0..1 (dark to bright). */
    void setParams (float mix, float size, float tone)
    {
        mixT = std::clamp (mix, 0.0f, 1.0f);
        const float sz = std::clamp (size, 0.0f, 1.0f);
        sizeF = 0.35f + 0.65f * sz;                                   // share of the longest delays used
        const float rt60 = 0.25f + 2.2f * sz * sz;                     // seconds
        for (int k = 0; k < kLines; ++k)
        {
            const float d = kLineMs[k] * 0.001f * kMaxSize * sizeF;    // seconds
            g[k] = std::pow (10.0f, -3.0f * d / rt60);
        }
        const float hz = 2500.0f + 9000.0f * std::clamp (tone, 0.0f, 1.0f) * std::clamp (tone, 0.0f, 1.0f);
        dampA = 1.0f - std::exp (-6.2831853f * hz / fs);
    }

    bool active() const { return mixT > 0.0005f || mix > 0.0005f; }

    void process (float* L, float* R, int n)
    {
        if (! active()) return;
        const int elen = (int) early.size();
        for (int i = 0; i < n; ++i)
        {
            mix += 0.001f * (mixT - mix);
            const float in = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
            // early reflections: taps on a short delay, spread left and right
            early[(size_t) ePos] = in;
            float eL = 0, eR = 0;
            for (int t = 0; t < kTaps; ++t)
            {
                const int d = std::min (elen - 1, (int) (kTapMs[t] * 0.001f * fs * sizeF));
                const float v = early[(size_t) ((ePos - d + elen) % elen)] * kTapG[t];
                if (t & 1) eR += v; else eL += v;
            }
            ePos = (ePos + 1) % elen;
            // diffuse tail: read, damp, mix through a Householder matrix, write back with the input
            std::array<float, kLines> o {};
            float sum = 0;
            for (int k = 0; k < kLines; ++k)
            {
                const int L_ = std::max (1, (int) (len[k] * sizeF));
                const float v = line[k][(size_t) ((pos[k] - L_ + (int) line[k].size()) % (int) line[k].size())];
                damp[k] += dampA * (v - damp[k]);
                o[k] = damp[k] * g[k];
                sum += o[k];
            }
            const float h = 2.0f / kLines * sum;
            float tL = 0, tR = 0;
            for (int k = 0; k < kLines; ++k)
            {
                const float back = o[k] - h + in * 0.35f + (k < 4 ? eL : eR) * 0.25f;
                line[k][(size_t) pos[k]] = back;
                pos[k] = (pos[k] + 1) % (int) line[k].size();
                if (k & 1) tR += o[k]; else tL += o[k];
            }
            const float wL = 0.6f * eL + 0.5f * tL, wR = 0.6f * eR + 0.5f * tR;
            const float dry = std::sqrt (1.0f - mix), wet = std::sqrt (mix);
            L[i] = dry * L[i] + wet * wL;
            if (R != nullptr) R[i] = dry * R[i] + wet * wR;
        }
    }

private:
    static constexpr int kLines = 8, kTaps = 6;
    static constexpr float kMaxSize = 1.6f;
    static constexpr float kLineMs[kLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.1f, 73.3f };
    static constexpr float kTapMs[kTaps]   = { 4.3f, 7.9f, 11.6f, 16.2f, 21.5f, 27.8f };
    static constexpr float kTapG[kTaps]    = { 0.62f, 0.55f, 0.43f, 0.38f, 0.29f, 0.24f };
    float fs = 48000;
    std::array<std::vector<float>, kLines> line;
    std::array<int, kLines> len {}, pos {};
    std::array<float, kLines> g {}, damp {};
    std::vector<float> early; int ePos = 0;
    float dampA = 0.3f, sizeF = 1.0f, mix = 0.0f, mixT = 0.0f;
};
