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

    // State transfer for seamless legato voice handoff.
    // The caller must prime coefficients (setResonance + setCutoff) BEFORE setState
    // so that the restored s1/s2 are interpreted under the correct g/k.
    void getState(float& s1Out, float& s2Out) const { s1Out = s1; s2Out = s2; }
    void setState(float s1In,   float s2In)         { s1 = s1In; s2 = s2In; }

    // cutoffHz: 20..20000, resonance: 0..1 (0 = no resonance, 1 = self-oscillation)
    // Convenience: sets both at once. Fine for one-shot calls; not for the sample loop.
    void setParameters(float cutoffHz, float resonance) {
        setResonance(resonance);   // updates k first
        setCutoff(cutoffHz);       // uses the fresh k
    }

    // Update only resonance (g unchanged). Call once per block.
    void setResonance(float resonance) {
        float q = 0.5f + resonance * 7.5f;
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // Update only cutoff (k unchanged). Call per-sample inside the render loop
    // for smooth, zipper-free filter sweeps driven by breath/mod/LFO.
    void setCutoff(float cutoffHz) {
        cutoffHz = juce::jlimit(20.0f, sr * 0.499f, cutoffHz);
        g  = std::tan(juce::MathConstants<float>::pi * cutoffHz / sr);
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
