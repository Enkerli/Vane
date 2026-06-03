#pragma once

// One-shot PCM sample player for per-voice transient layering.
// Audio-thread only: no locks, no allocations after setSample().
//
// Typical use: call setSample() once when the sample is chosen, then
// trigger() in noteStarted() and renderAdding() each block.
// The caller owns the decay envelope state (envLevel) so it persists
// across sub-blocks without storing additional state here.
class SamplePlayer {
public:
    // Assign sample data (not owned — caller must keep alive while playing).
    // Stops any current playback and resets the playhead.
    void setSample(const float* data, int numSamples) noexcept;

    // Start playback.
    //   speedRatio:  1.0 = original pitch; 2.0 = one octave up.
    //   startSample: playhead start offset (per-trigger round-robin variation).
    void trigger(float speedRatio = 1.0f, int startSample = 0) noexcept;

    void stop() noexcept { playing = false; }
    bool isPlaying() const noexcept { return playing; }

    // Additively mix this sample into dest[0..numSamples-1].
    //   envLevel:   caller's current decay envelope value (modified in-place).
    //   decayCoeff: per-sample multiplicative envelope decay
    //               (e.g. std::exp(-1 / (decayMs * sr * 0.001f))).
    //   gain:       overall gain applied on top of the envelope.
    // Stops playing and returns early when the end of the sample is reached.
    void renderAdding(float* dest, int numSamples,
                      float& envLevel, float decayCoeff,
                      float gain) noexcept;

private:
    const float* sampleData   = nullptr;
    int          totalSamples = 0;
    float        playhead     = 0.0f;
    float        speed        = 1.0f;
    bool         playing      = false;
};
