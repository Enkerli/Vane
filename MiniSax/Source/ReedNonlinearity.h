#pragma once
#include <algorithm>

namespace minisax
{

// Pressure-controlled reed reflection table, in the STK ReedTable tradition:
//
//   reflection = clamp(offset + slope * pressureDiff, -1, 1)
//
// `offset` is the reed opening at rest (larger aperture / looser embouchure
// keeps the reed more open); `slope` is negative and describes how quickly
// increasing pressure difference closes the reed (stiffer reed = steeper).
// This is not acoustically complete, but it is stable and tunable — the
// clamped output bounds the waveguide loop gain (see docs/ARCHITECTURE.md).
class ReedNonlinearity
{
public:
    // Normalized 0..1 controls -> table coefficients.
    // Ranges bracket the STK Clarinet (offset 0.6 / slope -0.8) and Saxofony
    // (slope -0.44) defaults so the normalized middle lands in known-good
    // territory.
    void setControls(float reedAperture, float reedStiffness, float embouchure)
    {
        offset = restOffsetMin + restOffsetRange * std::clamp(reedAperture, 0.0f, 1.0f)
               + embouchureOffsetDepth * (std::clamp(embouchure, 0.0f, 1.0f) - 0.5f);
        slope = -(slopeMin + slopeRange * std::clamp(reedStiffness, 0.0f, 1.0f));
    }

    float reflection(float pressureDiff) const
    {
        return std::clamp(offset + slope * pressureDiff, -1.0f, 1.0f);
    }

    float getOffset() const { return offset; }
    float getSlope() const { return slope; }

    // Named constants (Coding rule: no unexplained magic numbers).
    //
    // Tuning rationale (v0.1 bring-up, verified with a breath-sweep probe):
    // the loop speaks when mouth pressure exceeds (1/R - offset) / (2|slope|)
    // and chokes (reed slammed shut) near (1 - offset) / |slope|, where R is
    // the bore reflection gain.  Lower offset widens the playable window;
    // slope only scales both ends.  Defaults (0.5/0.5/0.5) give offset 0.60,
    // slope -0.30 — close to STK Clarinet's 0.7 / -0.3 but with more
    // overblow headroom.
    static constexpr float restOffsetMin = 0.45f;          // aperture 0 -> offset 0.45
    static constexpr float restOffsetRange = 0.30f;        // aperture 1 -> offset 0.75
    static constexpr float embouchureOffsetDepth = 0.15f;  // tight/loose lip shifts rest opening
    static constexpr float slopeMin = 0.10f;               // softest reed
    static constexpr float slopeRange = 0.40f;             // stiffness 1 -> slope -0.5

private:
    float offset = 0.6f;
    float slope = -0.6f;
};

} // namespace minisax
