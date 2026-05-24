#pragma once
#include <cmath>
#include <juce_core/juce_core.h>

// Cytomic TPT (Topology-Preserving Transform) state-variable filter.
// Chosen over the Zavalishin "Virtual Analog" formulation for two reasons:
//   1. Stability at resonance → 1 (self-oscillation threshold): the Zavalishin
//      form can become unstable with fast cutoff changes at high resonance.
//   2. Correct per-sample coefficient updates: g and k are independent so
//      setCutoff() can be called every sample without recomputing k, keeping
//      the breath-driven filter sweep tight and zipper-free.
//
// Coefficient ordering is critical and non-obvious:
//   setResonance() first — computes k, then a1 using the OLD g.
//   setCutoff()    next  — computes g, then a1/a2/a3 using the fresh k.
// Reversing the order or calling only setCutoff() leaves a1 stale.  In the
// render loop, setResonance() runs once per block and setCutoff() once per sample.
//
// Reference: Andy Simper, "Solving the continuous SVF equations using trapezoidal
// integration and equivalent currents" (Cytomic, 2013).
//
// ── Future improvements ───────────────────────────────────────────────────────
// • Notch output is available as (x - k*v1) — currently not exposed as a mode.
// • Allpass is (x - 2*k*v1) — also not exposed.
// • No soft-saturation on the feedback path; resonance at 1.0 is clean sine,
//   not the harmonically rich self-oscillation real analogue filters produce.
// • setCutoff() clamps at sr * 0.499 (not 0.5) to avoid tan(π/2) → ±∞, but a
//   small guard margin rather than the exact Nyquist makes coefficient blow-up
//   impossible even under floating-point rounding.
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
            // Notch = x - k*v1, Allpass = x - 2*k*v1 — not yet exposed as modes.
        }
        return v2;  // unreachable — all enum cases handled; compiler requires this
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
