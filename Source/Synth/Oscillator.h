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
//
// ── PolyBLEP assumptions and limits ──────────────────────────────────────────
// PolyBLEP only works correctly when phaseInc < 0.5 (i.e. frequency < Nyquist).
// Above that, the two correction windows [0, dt] and [1-dt, 1] overlap and
// produce incorrect — sometimes extreme — values.  setFrequency() clamps
// phaseInc to 0.9999 to prevent a more severe failure: if phaseInc >= 1.0,
// the phase-wrap `if (phase >= 1.0) phase -= floor(phase)` subtracts correctly,
// but the output of the polyBlep evaluation before the wrap uses the raw
// (un-wrapped) phase, producing values proportional to (phase/phaseInc)² which
// can reach hundreds — enough to trigger the host's safety mute immediately.
// This failure mode was observed with MTS-ESP high-mapped notes + 48-st MPE
// pitchbend: a note near F#7 bent up +48 st → ~44 700 Hz > 44 100 Hz sample rate.
//
// ── Future improvements ───────────────────────────────────────────────────────
// • Above Nyquist the output is aliased but bounded.  A cleaner alternative
//   would be to silence the output entirely when hz >= sr/2 (just return 0).
// • Square wave uses a fixed 50% pulse width; the duty cycle is not modulatable.
// • No anti-aliasing for the Triangle waveform at very high pitches (though in
//   practice triangle aliases much less severely than saw/square).
// • Wavetable or MinBLEP would handle high pitches and complex waveforms better.
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
        // Guard against phaseInc >= 1 (frequency at or above the sample rate).
        // PolyBLEP assumes dt < 0.5 for well-behaved corrections; beyond Nyquist
        // the two correction windows overlap and can produce extreme values.
        // More critically, when phaseInc > 1 the single phase -= 1 wrap below
        // never fully reduces phase, so it grows without bound → NaN/inf → silence.
        // Clamping here is safe: anything above ~22 kHz is inaudible anyway.
        if (phaseInc > 0.9999f) phaseInc = 0.9999f;
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
        // Belt-and-suspenders: floor-based wrap handles any residual > 1 that
        // slips through (e.g. if phaseInc is somehow exactly 1 due to rounding).
        if (phase >= 1.0f) phase -= std::floor(phase);

        return out;
    }

    // Returns the current phase (0..1) after the last next() call.
    // Because next() increments phase BEFORE returning, getPhase() already holds
    // the phase that the NEXT call to next() will use — no additional advance
    // needed in noteStarted().  SynthVoice uses this for the legato phase handoff:
    // the dying voice publishes this value; the new voice passes it to reset()
    // to continue the waveform without a phase discontinuity (audible as a click).
    float getPhase() const { return phase; }

    void reset(float startPhase = 0.0f) { phase = startPhase; }

private:
    // PolyBLEP residual at a discontinuity. t = current phase, dt = phase increment.
    // Returns a value that, when subtracted from the naive waveform, removes the alias.
    //
    // Only valid when dt < 0.5 (frequency < Nyquist/2) and phase is in [0, 1).
    // Callers must ensure setFrequency() clamps phaseInc before this is reached.
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
