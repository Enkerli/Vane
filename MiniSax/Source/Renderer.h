#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "PresetIO.h"
#include "SuiteIO.h"

namespace minisax
{

// Result of rendering one test case, including the diagnostics that go into
// the render report (see docs/TRACEABILITY.md).
struct RenderResult
{
    std::vector<float> samples;
    int sampleRate = 48000;
    uint32_t noiseSeed = 0;
    float peak = 0.0f;
    bool clipped = false;      // peak at/over 0.999 before the WAV clamp
    bool silent = false;       // peak below the audibility floor
    bool hadNonFinite = false; // engine flushed a NaN/Inf (should never happen)
    std::vector<std::string> warnings;
};

// Event-to-envelope semantics implemented here (documented in README.md):
//   noteOn  -> gate steps to 1, pitch steps to the event's note/pitchHz
//   noteOff -> gate steps to 0
//   control -> breakpoint on the named parameter; curve "linear" ramps from
//              the previous breakpoint, otherwise steps at `time`
//   pitch   -> retune at `time`; curve "linear" glides over glideSeconds
//              (default below, per-event override), otherwise instant
class Renderer
{
public:
    // Default slur glide when a pitch event says curve:"linear" but gives no
    // glideSeconds.  Short enough to read as a slur, long enough to avoid a
    // hard retuning click.  (Raised from 60 ms in v0.3's smoothing pass.)
    static constexpr double defaultPitchGlideSeconds = 0.08;

    // Peak below this is reported as silent; matches the analyzer threshold.
    static constexpr float silenceFloor = 1.0e-4f;

    static RenderResult renderTest(const Preset& preset, const SuiteTest& test,
                                   int sampleRate, uint32_t noiseSeed);
};

// Derives a per-test deterministic seed from the render base seed and the
// test id, so reordering tests in a suite never changes any test's audio.
uint32_t testSeed(uint32_t baseSeed, const std::string& testId);

} // namespace minisax
