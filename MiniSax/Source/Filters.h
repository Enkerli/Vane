#pragma once
#include <algorithm>
#include <cmath>

namespace minisax
{

// One-pole lowpass: y += a * (x - y).  Used for bore losses and control smoothing.
class OnePole
{
public:
    void setCoefficient(float a) { coeff = std::clamp(a, 0.0f, 1.0f); }

    // Configure from a cutoff frequency (approximation valid for fc << sr).
    void setCutoff(float cutoffHz, float sampleRate)
    {
        const float x = std::exp(-2.0f * pi * cutoffHz / sampleRate);
        setCoefficient(1.0f - x);
    }

    float process(float x)
    {
        state += coeff * (x - state);
        return state;
    }

    void reset(float value = 0.0f) { state = value; }
    float getState() const { return state; }

    static constexpr float pi = 3.14159265358979f;

private:
    float coeff = 0.5f;
    float state = 0.0f;
};

// DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1].
// Waveguide loops accumulate DC (constant breath pressure component); this
// removes it before the bell filter.  R = 0.995 puts the highpass corner
// around 40 Hz at 48 kHz.
class DCBlocker
{
public:
    float process(float x)
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void reset() { x1 = y1 = 0.0f; }

    static constexpr float R = 0.995f;

private:
    float x1 = 0.0f;
    float y1 = 0.0f;
};

// Direct-form-I biquad with an RBJ high-shelf configuration, used as the
// bell/body filter.  bellBrightness 0..1 sweeps the shelf from cut to boost
// so the raw delay-line tone always passes through some spectral shaping.
class Biquad
{
public:
    // RBJ audio-EQ-cookbook peaking EQ.
    void setPeaking(float sampleRate, float centerHz, float gainDb, float q)
    {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * OnePole::pi * centerHz / sampleRate;
        const float cw = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);

        const float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cw) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cw) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    // RBJ audio-EQ-cookbook high shelf.
    void setHighShelf(float sampleRate, float cutoffHz, float gainDb, float slope = 1.0f)
    {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * OnePole::pi * cutoffHz / sampleRate;
        const float cw = std::cos(w0);
        const float sw = std::sin(w0);
        const float alpha = sw / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
        const float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

        const float a0 = (A + 1.0f) - (A - 1.0f) * cw + twoSqrtAAlpha;
        b0 = (A * ((A + 1.0f) + (A - 1.0f) * cw + twoSqrtAAlpha)) / a0;
        b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
        b2 = (A * ((A + 1.0f) + (A - 1.0f) * cw - twoSqrtAAlpha)) / a0;
        a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cw - twoSqrtAAlpha) / a0;
    }

    float process(float x)
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    void reset() { x1 = x2 = y1 = y2 = 0.0f; }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

} // namespace minisax
