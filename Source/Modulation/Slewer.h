#pragma once
#include <cmath>

// One-pole lag filter used wherever we need to prevent zippering.
// setRates(attackMs, releaseMs) configures separate slew speeds for
// rising and falling signals — critical for breath-controlled parameters
// where attack should track quickly but release can be slower.
class Slewer {
public:
    void prepare(double sampleRate) {
        sr = static_cast<float>(sampleRate);
        attackCoeff  = makeCoeff(attackMs);
        releaseCoeff = makeCoeff(releaseMs);
    }

    void setRates(float newAttackMs, float newReleaseMs) {
        attackMs     = newAttackMs;
        releaseMs    = newReleaseMs;
        attackCoeff  = makeCoeff(attackMs);
        releaseCoeff = makeCoeff(releaseMs);
    }

    void reset(float value = 0.0f) { current = value; }

    float process(float target) {
        float coeff = (target > current) ? attackCoeff : releaseCoeff;
        current += (1.0f - coeff) * (target - current);
        return current;
    }

    float getValue() const { return current; }

private:
    float makeCoeff(float ms) const {
        if (ms <= 0.0f) return 0.0f;
        return std::exp(-1.0f / (sr * ms * 0.001f));
    }

    float sr           = 44100.0f;
    float attackMs     = 5.0f;
    float releaseMs    = 30.0f;
    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;
    float current      = 0.0f;
};
