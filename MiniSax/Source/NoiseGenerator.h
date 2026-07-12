#pragma once
#include <cstdint>

namespace minisax
{

// Deterministic xorshift32 noise in [-1, 1].
// Every render seeds this explicitly and records the seed in the render
// report, so any WAV can be reproduced bit-exactly (see docs/TRACEABILITY.md).
class NoiseGenerator
{
public:
    explicit NoiseGenerator(uint32_t seedValue = defaultSeed) { seed(seedValue); }

    void seed(uint32_t seedValue)
    {
        // xorshift must never hold state 0 (it would stay 0 forever).
        state = seedValue != 0 ? seedValue : defaultSeed;
    }

    uint32_t nextUint()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float nextFloat() // uniform in [-1, 1]
    {
        return static_cast<float>(nextUint()) * (2.0f / 4294967295.0f) - 1.0f;
    }

    static constexpr uint32_t defaultSeed = 0x4D53414Cu; // "MSAL" — arbitrary nonzero tag

private:
    uint32_t state = defaultSeed;
};

} // namespace minisax
