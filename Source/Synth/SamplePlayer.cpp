#include "SamplePlayer.h"

void SamplePlayer::setSample(const float* data, int numSamples) noexcept
{
    playing      = false;
    sampleData   = data;
    totalSamples = numSamples;
    playhead     = 0.0f;
}

void SamplePlayer::trigger(float speedRatio) noexcept
{
    if (sampleData == nullptr || totalSamples < 2) return;
    speed    = speedRatio > 0.0f ? speedRatio : 1.0f;
    playhead = 0.0f;
    playing  = true;
}

void SamplePlayer::renderAdding(float* dest, int numSamples,
                                 float& envLevel, float decayCoeff,
                                 float gain) noexcept
{
    if (!playing || sampleData == nullptr || totalSamples < 2) return;

    // totalSamples - 1: the last valid index for a two-point interpolation read.
    const float limit = static_cast<float>(totalSamples - 1);

    for (int i = 0; i < numSamples; ++i) {
        envLevel *= decayCoeff;

        const int   idx  = static_cast<int>(playhead);
        const float frac = playhead - static_cast<float>(idx);

        // Linear interpolation.  idx < limit is guaranteed by the playhead
        // guard below, so idx+1 is always in bounds.
        const float s = sampleData[idx] + frac * (sampleData[idx + 1] - sampleData[idx]);
        dest[i] += s * envLevel * gain;

        playhead += speed;
        if (playhead >= limit) {
            playing = false;
            return;
        }
    }
}
