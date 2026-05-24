#pragma once
#include <cmath>

// One-pole lag filter used wherever we need to prevent zippering.
//
// ── Call-rate awareness (easy to get wrong) ───────────────────────────────────
// process() is called once per audio block from ModMatrix::evaluate(), NOT once
// per sample.  The time constant must be calibrated to the actual call rate:
//
//   coeff = exp(-samplesPerStep / (sampleRate * timeConstant_s))
//
// With samplesPerStep = 1 but a blockSize of 512, the exponent is 512× smaller
// than it should be — the slew becomes 512× slower than intended.  The attack
// would take ~8 seconds instead of 5 ms.  This was the original bug before
// prepare() gained the samplesPerStep parameter.
//
// Voice-source routes (MPE pressure, slide) use per-voice Slewers cloned from
// the route's attack/release config via ModMatrix::initVoiceSlewers().  CC routes
// use the shared Slewer on the ModRoute struct itself.  Both must be initialised
// with the same blockSize so the time constants match.
//
// ── Limitations / future improvements ────────────────────────────────────────
// • The slew rate is fixed at note-on; it cannot be modulated in real time.
// • Asymmetric attack/release is done by choosing the coefficient based on
//   direction.  No "hold" phase is supported (would need an extra state).
// • Slew is linear in the abstract "value" space, which is correct for
//   filter-cutoff offsets (-1..+1) but not for exponential parameters.
//   In practice this is fine because routes already use bounded ±1 ranges.
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
