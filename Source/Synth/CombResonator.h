#pragma once
#include <vector>
#include <cmath>

// Damped feedback comb tuned to a frequency — Karplus-Strong-style resonator.
//
// Excited by the transient noise burst so the attack acquires the played note's
// pitch as it rings out (noisy -> pitched spectral flux).  This is the physics of
// articulation: an excitation feeding a resonant system that imposes pitch, which
// is the strongest auditory-fusion cue (shared harmonicity) between the attack
// and the note.  Inharmonic source in, tonal ring out.
//
// Audio-thread only: no locks/allocation after prepare().  One per voice.
class CombResonator {
public:
    void prepare(double sr) {
        sampleRate = sr;
        // Delay line long enough for the lowest musical note (~20 Hz) at this rate.
        buf.assign(static_cast<size_t>(sr / 20.0) + 8, 0.0f);
        reset();
    }

    void reset() noexcept {
        std::fill(buf.begin(), buf.end(), 0.0f);
        writeIdx = 0;
        lp       = 0.0f;
    }

    // Tune the comb to hz (delay = sampleRate / hz, fractional).
    void setTuning(float hz) noexcept {
        if (hz < 20.0f) hz = 20.0f;
        delay = static_cast<float>(sampleRate / static_cast<double>(hz));
        const float maxD = static_cast<float>(buf.size()) - 2.0f;
        if (delay > maxD) delay = maxD;
        if (delay < 2.0f) delay = 2.0f;
    }

    // Process one sample.
    //   x:        excitation input (the transient sample, or 0 once it ends —
    //             the comb keeps ringing).
    //   fb:       feedback amount [0, ~0.95]; higher = longer/stronger pitched ring.
    //   dampCoef: loop lowpass coefficient (1 = bright/metallic, ->0 = dark/string).
    float process(float x, float fb, float dampCoef) noexcept {
        const int N = static_cast<int>(buf.size());
        float rp = static_cast<float>(writeIdx) - delay;
        while (rp < 0.0f) rp += static_cast<float>(N);
        int   i0 = static_cast<int>(rp);
        float fr = rp - static_cast<float>(i0);
        int   i1 = i0 + 1; if (i1 >= N) i1 -= N;
        const float d = buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
        lp += dampCoef * (d - lp);                 // one-pole damping in the loop
        const float y = x + fb * lp;
        buf[(size_t) writeIdx] = y;
        if (++writeIdx >= N) writeIdx = 0;
        return y;
    }

private:
    std::vector<float> buf;
    double sampleRate = 44100.0;
    float  delay      = 100.0f;
    float  lp         = 0.0f;
    int    writeIdx   = 0;
};
