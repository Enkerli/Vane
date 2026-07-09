#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace minisax
{

// Linearly interpolated fractional delay line.
//
// Convention: read() returns the signal delayed by `delaySamples` relative to
// the *next* sample written.  In the waveguide loop we read() first, then
// write(), which yields y[n] = x[n - d] for a delay of d samples.
// Linear interpolation is the documented v0.1 choice (see docs/ARCHITECTURE.md);
// allpass or higher-order interpolation can replace it later.
class FractionalDelay
{
public:
    void prepare(int maxDelaySamples)
    {
        // +4 guard samples so interpolation never reads past the write head.
        buffer.assign(static_cast<size_t>(maxDelaySamples) + 4, 0.0f);
        writeIndex = 0;
        setDelay(static_cast<float>(maxDelaySamples) * 0.5f);
    }

    void clear()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
    }

    // Delay is clamped to [minDelay, size-2] so reads stay in bounds even when
    // pitch envelopes push it around at run time.
    void setDelay(float delaySamples)
    {
        const float maxDelay = static_cast<float>(buffer.size()) - 2.0f;
        delay = std::clamp(delaySamples, minDelay, maxDelay);
    }

    float getDelay() const { return delay; }

    float read() const
    {
        const auto size = static_cast<int>(buffer.size());
        float readPos = static_cast<float>(writeIndex) - delay;
        while (readPos < 0.0f)
            readPos += static_cast<float>(size);
        const int i0 = static_cast<int>(readPos);
        const float frac = readPos - static_cast<float>(i0);
        const int i1 = (i0 + 1) % size;
        return buffer[static_cast<size_t>(i0)] * (1.0f - frac)
             + buffer[static_cast<size_t>(i1)] * frac;
    }

    void write(float x)
    {
        buffer[static_cast<size_t>(writeIndex)] = x;
        writeIndex = (writeIndex + 1) % static_cast<int>(buffer.size());
    }

    static constexpr float minDelay = 2.0f;

private:
    std::vector<float> buffer;
    int writeIndex = 0;
    float delay = 2.0f;
};

} // namespace minisax
