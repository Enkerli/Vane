#pragma once
#include <cmath>

// One-pole lag filter used wherever we need to prevent zippering.
//
// IMPORTANT — call rate awareness:
//   process() must be told how many samples pass between calls via prepare().
//   If called once per sample, samplesPerStep = 1 (default, true sample-rate slewing).
//   If called once per block (e.g. from ModMatrix::evaluate), samplesPerStep = blockSize.
//   Getting this wrong makes the slew 256× too slow (the original bug).
class Slewer {
public:
    // samplesPerStep: how many samples elapse between successive process() calls.
    void prepare(double sampleRate, int samplesPerStep = 1) {
        sr   = static_cast<float>(sampleRate);
        step = std::max(1, samplesPerStep);
        recomputeCoeffs();
    }

    void setRates(float newAttackMs, float newReleaseMs) {
        attackMs  = newAttackMs;
        releaseMs = newReleaseMs;
        recomputeCoeffs();
    }

    void reset(float value = 0.0f) { current = value; }

    float process(float target) {
        float coeff = (target > current) ? attackCoeff : releaseCoeff;
        current += (1.0f - coeff) * (target - current);
        return current;
    }

    float getValue() const { return current; }

private:
    void recomputeCoeffs() {
        attackCoeff  = makeCoeff(attackMs);
        releaseCoeff = makeCoeff(releaseMs);
    }

    float makeCoeff(float ms) const {
        if (ms <= 0.0f) return 0.0f;
        // exp(-samplesPerStep / (sampleRate * timeConstant_seconds))
        return std::exp(-static_cast<float>(step) / (sr * ms * 0.001f));
    }

    float sr           = 44100.0f;
    int   step         = 1;
    float attackMs     = 5.0f;
    float releaseMs    = 30.0f;
    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;
    float current      = 0.0f;
};
