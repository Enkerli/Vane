#pragma once
#include <cmath>
#include <juce_core/juce_core.h>

// Band-limited oscillator using the PolyBLEP method.
// PolyBLEP corrects the discontinuities in naive saw and square waves by
// blending a polynomial correction at each transition point, suppressing
// the aliasing that would otherwise appear when the waveform folds back
// across the Nyquist frequency at high pitches.
//
// Sine and triangle are inherently alias-free at normal audio rates and
// need no correction. Noise uses a simple LCG — no pitch, no phasing.
class Oscillator {
public:
    enum class Waveform { Sine, Triangle, Saw, Square, Noise };

    void prepare(double sampleRate) {
        sr = static_cast<float>(sampleRate);
        phase = 0.0f;
        noiseState = 12345u;
    }

    void setFrequency(float hz) {
        phaseInc = hz / sr;
    }

    void setWaveform(Waveform w) { waveform = w; }

    float next() {
        float out = 0.0f;

        switch (waveform) {
            case Waveform::Sine:
                out = std::sin(phase * juce::MathConstants<float>::twoPi);
                break;

            case Waveform::Triangle:
                // Naive triangle — alias-free at audio rates
                out = 1.0f - 4.0f * std::abs(phase - 0.5f);
                break;

            case Waveform::Saw:
                out = 2.0f * phase - 1.0f;          // naive: 0→1 ramp, -1 at reset
                out -= polyBlep(phase, phaseInc);    // correct the discontinuity at 0/1
                break;

            case Waveform::Square: {
                float pw = 0.5f;
                out  =  (phase < pw) ? 1.0f : -1.0f;
                out +=  polyBlep(phase, phaseInc);           // rising edge at 0
                out -=  polyBlep(std::fmod(phase + 1.0f - pw, 1.0f), phaseInc); // falling edge at pw
                break;
            }

            case Waveform::Noise:
                noiseState = noiseState * 6364136223846793005ULL + 1442695040888963407ULL;
                out = static_cast<float>(static_cast<int32_t>(noiseState >> 33)) / 2147483648.0f;
                break;
        }

        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;

        return out;
    }

    // Returns the current phase (0..1). Used by SynthVoice to publish the phase
    // at end-of-block so the next voice can start at exactly the right position,
    // producing a seamless waveform with no click at legato note transitions.
    float getPhase() const { return phase; }

    void reset(float startPhase = 0.0f) { phase = startPhase; }

private:
    // PolyBLEP residual at a discontinuity. t = current phase, dt = phase increment.
    // Returns a value that, when subtracted from the naive waveform, removes the alias.
    static float polyBlep(float t, float dt) {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt) {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    Waveform waveform = Waveform::Saw;
    float sr       = 44100.0f;
    float phase    = 0.0f;
    float phaseInc = 0.0f;
    uint64_t noiseState = 12345u;
};
