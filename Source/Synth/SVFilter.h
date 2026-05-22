#pragma once
#include <cmath>
#include <juce_core/juce_core.h>

// Topology-preserving state-variable filter (TPT-SVF).
// Produces LP, BP, and HP simultaneously from the same state.
// Stable at any resonance, handles fast cutoff modulation without blowing up —
// which makes it suitable for breath-driven filter sweeps without clicks.
//
// Reference: Vadim Zavalishin, "The Art of VA Filter Design" (2012).
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
        // Map resonance 0..1 → Q 0.5..20 (mild → near-oscillation)
        float q = 0.5f + resonance * 19.5f;
        k = 1.0f / q;
    }

    float process(float x, Mode mode) {
        // TPT-SVF topology (Zavalishin §3.10)
        float v3  = x - s2;
        float v1  = (g * v3 - k * s1) / (1.0f + g * (g + k));
        float v2  = s2 + g * v1;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;

        switch (mode) {
            case Mode::LP: return v2;
            case Mode::BP: return v1;
            case Mode::HP: return v3 - k * v1 - v2;
        }
        return v2;
    }

private:
    float sr = 44100.0f;
    float g  = 1.0f;   // tan(π * cutoff / sr)
    float k  = 1.0f;   // 1/Q
    float s1 = 0.0f;   // integrator state 1
    float s2 = 0.0f;   // integrator state 2
};
