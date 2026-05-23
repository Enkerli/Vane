#pragma once
#include <cmath>
#include <juce_core/juce_core.h>

// Cytomic TPT state-variable filter.
// Stable at all resonances; correct under fast cutoff modulation (breath sweeps,
// pitch-coupled filtering) without the instability of the Zavalishin formulation.
//
// Reference: Andy Simper, "Solving the continuous SVF equations using trapezoidal
// integration and equivalent currents" (Cytomic, 2013).
class SVFilter {
public:
    enum class Mode { LP, BP, HP };

    void prepare(double sampleRate) {
        sr = static_cast<float>(sampleRate);
        reset();
    }

    void reset() { s1 = s2 = 0.0f; }

    // cutoffHz: 20..20000, resonance: 0..1 (0 = no resonance, 1 = self-oscillation)
    void setParameters(float cutoffHz, float resonance) {
        cutoffHz = juce::jlimit(20.0f, sr * 0.499f, cutoffHz);
        g = std::tan(juce::MathConstants<float>::pi * cutoffHz / sr);
        // Map resonance 0..1 → Q 0.5..8
        float q = 0.5f + resonance * 7.5f;
        k  = 1.0f / q;
        // Precompute coefficients (updated every block — cheap, avoids div in process())
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float process(float x, Mode mode) {
        float v3 = x - s2;
        float v1 = a1 * s1 + a2 * v3;
        float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;

        switch (mode) {
            case Mode::LP: return v2;
            case Mode::BP: return v1;
            case Mode::HP: return x - k * v1 - v2;
        }
        return v2;
    }

private:
    float sr = 44100.0f;
    float g  = 1.0f;    // tan(π·cutoff/sr)
    float k  = 1.0f;    // 1/Q
    float a1 = 1.0f;    // 1/(1+g·(g+k))
    float a2 = 0.0f;    // g·a1
    float a3 = 0.0f;    // g·a2
    float s1 = 0.0f;    // integrator state 1
    float s2 = 0.0f;    // integrator state 2
};
